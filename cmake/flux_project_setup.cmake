###############################################################################
#  flux_project_setup.cmake
#
#  HAL-owned project-level setup. Must be `include()`d from the parent
#  application's root `CMakeLists.txt` BEFORE
#  `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` because
#  `EXCLUDE_COMPONENTS` is consumed by IDF when it builds the component
#  graph during that include.
#
#  Why this lives in the HAL, not in the app:
#  - Excluding `esp_wifi` / `wpa_supplicant` / `wifi_provisioning` is a
#    HAL-level decision: Flux V1 ships with UART / CAN / USB-Serial-JTAG
#    only and the HAL's CommChannelsManager exposes no Wi-Fi surface.
#  - Apps should never have to learn which IDF components the HAL does
#    not need; the policy belongs next to the manager that owns the
#    feature surface.
#  - This makes adding/removing IDF components a one-line change in the
#    HAL repo, never in the app.
###############################################################################

# Flux V1 uses UART / CAN / USB-Serial-JTAG only — no Wi-Fi stack.
# Excluding these IDF components removes WPA / LWIP Wi-Fi glue from the
# link (smaller flash + RAM vs CONFIG_ESP_WIFI_ENABLED, which stays
# forced "y" for Wi-Fi-capable SoCs in IDF Kconfig).
list(APPEND EXCLUDE_COMPONENTS
    esp_wifi
    wpa_supplicant
    wifi_provisioning
)

# Re-export so consumers that need to inspect the policy (e.g. test
# harnesses that build subsets of components) can read it from a
# canonical name without re-encoding the list.
set(FLUX_HAL_EXCLUDED_IDF_COMPONENTS
    esp_wifi
    wpa_supplicant
    wifi_provisioning
    CACHE INTERNAL "IDF components excluded by Flux V1 HAL policy")
