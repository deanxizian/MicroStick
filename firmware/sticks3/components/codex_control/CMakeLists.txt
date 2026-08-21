cmake_minimum_required(VERSION 3.16)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "codex_control.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES json
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
else()
    project(codex_control LANGUAGES C)

    set(CODEX_CONTROL_CJSON_DIR "" CACHE PATH "Directory containing cJSON.c and cJSON.h")
    if(NOT CODEX_CONTROL_CJSON_DIR AND DEFINED ENV{IDF_PATH})
        set(CODEX_CONTROL_CJSON_DIR "$ENV{IDF_PATH}/components/json/cJSON")
    endif()
    if(NOT EXISTS "${CODEX_CONTROL_CJSON_DIR}/cJSON.c")
        message(FATAL_ERROR "Set CODEX_CONTROL_CJSON_DIR to portable cJSON sources")
    endif()

    add_library(codex_control STATIC
        codex_control.c
        "${CODEX_CONTROL_CJSON_DIR}/cJSON.c"
    )
    target_include_directories(codex_control
        PUBLIC include
        PRIVATE "${CODEX_CONTROL_CJSON_DIR}"
    )
    target_compile_features(codex_control PUBLIC c_std_11)
    target_compile_options(codex_control PRIVATE -Wall -Wextra -Werror)

    enable_testing()
    add_executable(codex_control_test test/codex_control_test.c)
    target_link_libraries(codex_control_test PRIVATE codex_control)
    target_compile_options(codex_control_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME codex_control_test COMMAND codex_control_test)
endif()
