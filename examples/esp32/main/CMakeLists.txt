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

set(MAIN_REQUIRES
    hf-hal-flux-v1
)

idf_component_register(
    SRCS "${APP_SOURCE_FILE}"
    INCLUDE_DIRS "."
    REQUIRES ${MAIN_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)

if(BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wpedantic -O0 -g3 -DDEBUG
    )
else()
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wpedantic -O2 -g -DNDEBUG
    )
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "EXAMPLE_TYPE_${APP_TYPE}=1"
)
