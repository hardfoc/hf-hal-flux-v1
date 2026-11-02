# Testing and CI

What runs on a pull request, what each check means, and how to run it on your machine first. All workflows are in
[`.github/workflows`](../.github/workflows); most call reusable workflows in
[hf-general-ci-tools](https://github.com/hardfoc/hf-general-ci-tools) or
[hf-espidf-ci-tools](https://github.com/hardfoc/hf-espidf-ci-tools).

## Workflows

| Workflow | File | Runs on | Checks |
| --- | --- | --- | --- |
| ESP32 Examples Build CI | `esp32-examples-build-ci.yml` | push, pull request | Builds every app with `ci_enabled: true` in `app_config.yml`, Debug and Release, with ESP-IDF release/v5.5 |
| C++ Lint CI | `ci-cpp-lint.yml` | push, pull request | clang-format and clang-tidy with `_config/.clang-format` and `_config/.clang-tidy` |
| C++ Analysis CI | `ci-cpp-analysis.yml` | push, pull request | cppcheck on `lib/managers` and `lib/api` (C++20) |
| Docs Publish CI | `ci-docs-publish.yml` | push to main, `v*` tags, pull request | `hardfoc-docs check` (figures and generated tables current), strict MkDocs build, Doxygen under `api/`, offline link check of the built site, Lighthouse budget, phone and desktop screenshots (artifact `docs-screenshots`); publishes on main and tags |
| Docs Link Check CI | `ci-docs-linkcheck.yml` | push, pull request | lychee over the Markdown files, config `_config/lychee.toml` |
| Markdown Lint CI | `ci-markdown-lint.yml` | push, pull request | markdownlint, config `_config/.markdownlint.json` |
| YAML Lint CI | `ci-yaml-lint.yml` | push, pull request | yamllint, config `_config/.yamllint` |
| HAL Boundary CI | `ci-hal-boundary.yml` | push, pull request | `tools/lint/lint_hal_boundary.sh` (no ESP-IDF, FreeRTOS or `Esp*` code outside `lib/managers/bsp_bind/<family>/`; shrink-only allowlist checked against the PR base); host build of the managers against `bsp_bind/host` |
| Release | `release.yml` | `v*` tags | Builds the release artifacts |

The examples are integration tests: they run on a board, not in CI. CI proves they compile; a person flashes them.
There are no host unit tests in this repository yet.

## Run the checks locally

```bash
# Build one example (needs ESP-IDF, see Get started)
cd examples/esp32 && ./scripts/build_app.sh flux_api_test Release && cd ../..

# HAL boundary lint and the host build of the managers (no ESP-IDF needed)
tools/lint/lint_hal_boundary.sh
cmake -S tests/host -B build/host -G Ninja && cmake --build build/host

# Format check
clang-format --style=file:_config/.clang-format --dry-run --Werror lib/managers/*.cpp lib/managers/*.h

# Docs: drift checks, then the strict site build (the pin map needs lib/core and hf-pincfg)
python3 -m pip install "git+https://github.com/hardfoc/hf-general-ci-tools@v1#subdirectory=packages/hardfoc-mkdocs-theme"
hardfoc-docs gen --check
hardfoc-docs figures --check
mkdocs build --strict

# Markdown and YAML lint
npx markdownlint-cli2 --config _config/.markdownlint.json "docs/**/*.md" README.md
yamllint -c _config/.yamllint .github/workflows mkdocs.yml
```

## When the docs checks fail

| Message | Fix |
| --- | --- |
| `generated region is stale` | The pin map, `app_config.yml` or a manager's `#include`s changed. Run `hardfoc-docs gen` and commit |
| `... is stale (run hardfoc-docs figures)` | A figure's source data changed. Run `hardfoc-docs figures`, look at the SVGs, commit |
| `contains a link ... not found` | A page links to a file that does not exist. Fix the link |
| `image(s) have no alt text` | Every image needs `alt` text |
