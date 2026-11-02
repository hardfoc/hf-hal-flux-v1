#!/usr/bin/env bash
# HAL boundary lint (ADR 0001, architecture-port design §3.2 rule 4, §6.3).
#
# Rule: ESP-IDF, FreeRTOS and Esp* wrapper code appears only in the platform
# binding of its MCU family, lib/managers/bsp_bind/<family>/. Everything else
# in the HAL library (lib/api, lib/managers, lib/managers/bsp_bind/board, the
# family-neutral lib/managers/bsp_bind/*.h) is MCU-free:
#
#   1. no ESP-IDF / FreeRTOS / Esp* / WS2812-RMT #include (indented #  include
#      under #if nests included);
#   2. no #include of a family directory (bsp_bind/esp32s3/, bsp_bind/host/)
#      from outside bsp_bind/;
#   3. no ESP-IDF or Esp* type names in public headers (comments stripped), so
#      a forward declaration cannot smuggle a family type into an API.
#
# Known debt is listed in tools/lint/hal_boundary_allowlist.txt, one path per
# line. The list may only shrink:
#   - a listed file that no longer violates fails the lint (remove the line);
#   - with --base <git-ref> (CI passes the PR base), a path that is not listed
#     at <git-ref> fails the lint (new debt needs an ADR, not a list entry).
#
# Usage: tools/lint/lint_hal_boundary.sh [--base <git-ref>]
# Exits non-zero on any violation.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

ALLOWLIST_REL="tools/lint/hal_boundary_allowlist.txt"
BASE_REF=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --base) BASE_REF="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

SCAN_AREAS=(lib/api lib/managers)

# Family directories: the only places allowed to use the patterns below.
is_family_file() {
  case "$1" in
    lib/managers/bsp_bind/esp32s3/*|lib/managers/bsp_bind/host/*|lib/managers/bsp_bind/stm32*/*) return 0 ;;
  esac
  return 1
}

INC='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]'
INCLUDE_PATTERNS=(
  "${INC}esp_"                       # esp_system.h, esp_err.h, esp_heap_caps.h, ...
  "${INC}(driver|soc|hal|rom|esp32s3|esp_private)/"
  "${INC}(freertos/|FreeRTOS\.h|task\.h|queue\.h|semphr\.h|event_groups\.h)"
  "${INC}(sdkconfig\.h|nvs\.h|nvs_flash\.h)"
  "${INC}[^\">]*mcu/esp32/"           # hf-core Esp* wrappers by path
  "${INC}Esp[A-Z][A-Za-z0-9_]*\.h"    # ... or by bare name
  "${INC}[^\">]*hf-ws2812-rmt-driver/" # WS2812 RMT driver (ESP32 RMT peripheral)
  "${INC}(ws2812_(cpp|control|effects)|led_strip)" # (ws2812_version.h is a plain version macro)
  "${INC}[^\">]*bsp_bind/(esp32s3|host|stm32[a-z0-9]*)/"
)

# Type names that belong to a family (checked in headers only, comments stripped).
TYPE_PATTERN='\b(Esp[A-Z][A-Za-z0-9_]*|esp_err_t|gpio_num_t|spi_host_device_t|adc_unit_t|adc_channel_t|uart_port_t|twai_[a-z_]+_t|rmt_[a-z_]+_t|nvs_handle_t|TaskHandle_t|QueueHandle_t|SemaphoreHandle_t|TickType_t|BaseType_t|WS2812[A-Za-z]*)\b'

declare -a ALLOWED=()
if [[ -f "${ALLOWLIST_REL}" ]]; then
  while IFS= read -r line || [[ -n "${line}" ]]; do
    line="${line%%#*}"
    line="$(echo "${line}" | xargs)"
    [[ -n "${line}" ]] && ALLOWED+=("${line}")
  done < "${ALLOWLIST_REL}"
fi

is_allowed() {
  local f="$1" a
  for a in "${ALLOWED[@]+"${ALLOWED[@]}"}"; do
    [[ "${f}" == "${a}" ]] && return 0
  done
  return 1
}

strip_comments() {
  # Remove /* */ and // comments; keep line count.
  perl -0777 -pe 's{/\*.*?\*/}{ my $c = $&; $c =~ s/[^\n]//g; $c }gse; s{//[^\n]*}{}g' "$1"
}

violations=0
declare -a VIOLATING=()

file_violates() {
  local f="$1" pat hits found=1
  for pat in "${INCLUDE_PATTERNS[@]}"; do
    if hits="$(grep -En "${pat}" "${f}")"; then
      echo "  ${f}:"
      echo "${hits}" | sed 's/^/    /'
      found=0
    fi
  done
  case "${f}" in
    *.h|*.hpp)
      if hits="$(strip_comments "${f}" | grep -En "${TYPE_PATTERN}")"; then
        echo "  ${f} (family type in a header):"
        echo "${hits}" | sed 's/^/    /'
        found=0
      fi
      ;;
  esac
  return ${found}
}

while IFS= read -r f; do
  [[ -z "${f}" ]] && continue
  is_family_file "${f}" && continue
  if report="$(file_violates "${f}")"; then
    VIOLATING+=("${f}")
    if is_allowed "${f}"; then
      echo "allowlisted debt: ${f}"
    else
      echo "VIOLATION (HAL boundary): ${f}"
      echo "${report}"
      violations=$((violations + 1))
    fi
  fi
done < <(find "${SCAN_AREAS[@]}" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cpp' -o -name '*.ipp' \) | sort)

# Shrink-only allowlist, part 1: every listed file must still violate.
for a in "${ALLOWED[@]+"${ALLOWED[@]}"}"; do
  still=0
  for v in "${VIOLATING[@]+"${VIOLATING[@]}"}"; do
    [[ "${v}" == "${a}" ]] && still=1
  done
  if [[ ${still} -eq 0 ]]; then
    echo "STALE ALLOWLIST ENTRY: ${a} no longer violates (or no longer exists); remove it from ${ALLOWLIST_REL}"
    violations=$((violations + 1))
  fi
done

# Shrink-only allowlist, part 2: no entry may be added relative to the base.
if [[ -n "${BASE_REF}" ]]; then
  base_list="$(git show "${BASE_REF}:${ALLOWLIST_REL}" 2>/dev/null || true)"
  for a in "${ALLOWED[@]+"${ALLOWED[@]}"}"; do
    if ! printf '%s\n' "${base_list}" | sed 's/#.*//' | awk '{$1=$1};1' | grep -Fxq "${a}"; then
      echo "ALLOWLIST GREW: ${a} is not listed at ${BASE_REF}; the allowlist may only shrink"
      violations=$((violations + 1))
    fi
  done
fi

if [[ ${violations} -gt 0 ]]; then
  echo
  echo "FAIL: ${violations} HAL boundary problem(s)."
  echo "      ESP-IDF, FreeRTOS, Esp* and WS2812-RMT code belongs in lib/managers/bsp_bind/<family>/,"
  echo "      behind lib/managers/bsp_bind/FluxPlatformBind.h. See docs/architecture/bindings.md."
  exit 1
fi

echo "OK: HAL boundary clean (${#ALLOWED[@]} allowlisted file(s); family code only under lib/managers/bsp_bind/<family>/)."
