#!/usr/bin/env bash
# HAL static-allocation lint (ADR 0005, architecture-port design §3.4 "Static allocation", §6.3 lint_rt_alloc).
#
# Rule: the HAL library (lib/api, lib/managers including bsp_bind/<family>/ and
# bsp_bind/board/) owns every object in fixed storage: members, std::array,
# std::optional, static slots. It never allocates from the heap itself and uses
# no heap-backed container or callable:
#
#   make_unique, make_shared, unique_ptr, shared_ptr, `new T` (placement new is
#   allowed), malloc/calloc/realloc, std::vector, std::string, std::function,
#   std::map, std::unordered_map, std::list, std::deque.
#
# Comments are ignored. The host test family (bsp_bind/host/) is exempt: it is
# test scaffolding over the hf-core mocks, which use std::vector and friends.
# ESP-IDF drivers that allocate internally (SPI master, TWAI, UART, RMT) do so
# inside the binding's Create* calls during EnsureInitialized(); that is outside
# what a source lint can see and is covered by ADR 0005.
#
# Usage: tools/lint/lint_hal_alloc.sh      Exits non-zero on any violation.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

PATTERN='\b(make_unique|make_unique_nothrow|make_shared|unique_ptr|shared_ptr|malloc|calloc|realloc)\b|\bnew[[:space:]]+[A-Za-z_:]|std::(vector|string|function|map|unordered_map|list|deque)\b'

strip_comments() {
  perl -0777 -pe 's{/\*.*?\*/}{ my $c = $&; $c =~ s/[^\n]//g; $c }gse; s{//[^\n]*}{}g; s{"(?:[^"\\\n]|\\.)*"}{""}g' "$1"
}

violations=0
while IFS= read -r f; do
  if hits="$(strip_comments "${f}" | grep -En "${PATTERN}")"; then
    echo "VIOLATION (HAL static allocation): ${f}"
    echo "${hits}" | sed 's/^/    /'
    violations=$((violations + 1))
  fi
done < <(find lib/api lib/managers -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.ipp' \) \
           -not -path 'lib/managers/bsp_bind/host/*' | sort)

if [[ ${violations} -gt 0 ]]; then
  echo
  echo "FAIL: ${violations} file(s) use the heap or a heap-backed type."
  echo "      Hold objects as members, std::optional or std::array slots (docs/adr/0005-no-heap-after-init.md)."
  exit 1
fi
echo "OK: no heap allocation or heap-backed type in lib/api or lib/managers (bsp_bind/host exempt)."
