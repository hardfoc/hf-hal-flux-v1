if(NOT DEFINED APP_TYPE)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(APP_TYPE "${DEFAULT_APP_TYPE}")
    else()
        message(FATAL_ERROR "APP_TYPE not defined. Please use build_app.sh to build this project.")
    endif()
endif()

if(NOT DEFINED APP_SOURCE_FILE)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(APP_SOURCE_FILE "${DEFAULT_SOURCE_FILE}")
    else()
        message(FATAL_ERROR "APP_SOURCE_FILE not defined. Please use build_app.sh to build this project.")
    endif()
endif()

message(STATUS "Building app type: ${APP_TYPE}")
message(STATUS "Using source file: ${APP_SOURCE_FILE}")

# Check if source file is in a subdirectory and collect all sources
get_filename_component(APP_SOURCE_DIR "${APP_SOURCE_FILE}" DIRECTORY)
set(APP_SOURCES "${APP_SOURCE_FILE}")
set(APP_INCLUDE_DIRS ".")

if(APP_SOURCE_DIR)
    # Source is in a subdirectory — include all .cpp files from that directory
    file(GLOB SUBDIR_SOURCES "${APP_SOURCE_DIR}/*.cpp")
    list(APPEND APP_SOURCES ${SUBDIR_SOURCES})
    list(REMOVE_DUPLICATES APP_SOURCES)
    list(APPEND APP_INCLUDE_DIRS "${APP_SOURCE_DIR}")
    message(STATUS "App subdirectory: ${APP_SOURCE_DIR}")
    message(STATUS "All sources: ${APP_SOURCES}")
endif()

set(MAIN_REQUIRES
    hf-hal-flux-v1
    esp_timer               # Required by TestFramework.h
    esp_driver_gpio         # Required by TestFramework.h (driver/gpio.h)
)

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS ${APP_INCLUDE_DIRS}
    REQUIRES ${MAIN_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)

if(BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -O0 -g3 -DHF_DEBUG=1
    )
else()
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -O2 -g -DNDEBUG
    )
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "EXAMPLE_TYPE_${APP_TYPE}=1"
)
