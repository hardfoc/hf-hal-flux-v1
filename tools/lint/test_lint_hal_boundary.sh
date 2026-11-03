#!/usr/bin/env bash
# Self-test of tools/lint/lint_hal_boundary.sh: run it on small fixture trees and check that each
# rule fires (and that clean code passes). CI runs this next to the lint itself.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

failures=0

# fixture <name> <file under lib/> <content>: a tree with the lint and one source file.
fixture() {
  local dir="${WORK}/$1" rel="$2" content="$3"
  mkdir -p "${dir}/tools/lint" "${dir}/lib/api" "${dir}/lib/managers" "$(dirname "${dir}/${rel}")"
  cp "${HERE}/lint_hal_boundary.sh" "${dir}/tools/lint/"
  : > "${dir}/tools/lint/hal_boundary_allowlist.txt"
  printf '%s\n' "${content}" > "${dir}/${rel}"
  echo "${dir}"
}

expect() {
  local want="$1" name="$2" rel="$3" content="$4" pattern="${5:-}"
  local dir out rc=0
  dir="$(fixture "${name}" "${rel}" "${content}")"
  out="$(bash "${dir}/tools/lint/lint_hal_boundary.sh" 2>&1)" || rc=$?
  if [[ "${want}" == fail && ${rc} -eq 0 ]]; then
    echo "FAIL ${name}: the lint passed, it must fail"; failures=$((failures + 1)); return
  fi
  if [[ "${want}" == pass && ${rc} -ne 0 ]]; then
    echo "FAIL ${name}: the lint failed, it must pass"; echo "${out}"; failures=$((failures + 1)); return
  fi
  if [[ -n "${pattern}" ]] && ! grep -q "${pattern}" <<< "${out}"; then
    echo "FAIL ${name}: no '${pattern}' in the lint output"; echo "${out}"; failures=$((failures + 1)); return
  fi
  echo "ok   ${name}"
}

# Rule 4, lock discipline: raw driver accessors in lib/ fail.
expect fail raw-getdriver-arrow lib/managers/Foo.cpp 'void f(H* h) { auto* d = h->GetDriver(); (void)d; }' \
  "raw driver access"
expect fail raw-getdriver-dot lib/api/Foo.cpp 'void f(H& h) { h.GetDriver()->Run(); }' "raw driver access"
expect fail raw-getsensor lib/managers/Foo.cpp 'void f(H* h) { h->GetSensor(); }' "raw driver access"
expect fail raw-via-spi lib/managers/Foo.cpp 'void f(H* h) { h->driverViaSpi(); }' "raw driver access"
expect fail handler-getter lib/managers/Foo.h 'struct M { Max22200Handler* GetMax22200Handler() noexcept; };' \
  "raw driver access"
# ... but the locked paths and comments do not.
expect pass with-driver lib/managers/Foo.cpp 'void f(H* h) { h->WithDriver([](auto& d) { return d.Run(); }); }'
expect pass comment-only lib/managers/Foo.cpp '// never call h->GetDriver() here: it skips the mutex'
# The existing boundary rule still fires.
expect fail esp-include lib/managers/Foo.cpp '#include "esp_log.h"' "VIOLATION"

if [[ ${failures} -gt 0 ]]; then
  echo "${failures} lint self-test(s) failed"
  exit 1
fi
echo "OK: lint self-tests passed"
