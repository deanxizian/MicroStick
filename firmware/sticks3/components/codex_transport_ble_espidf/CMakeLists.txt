cmake_minimum_required(VERSION 3.16)

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS "codex_ble_espidf.c" "microstick_keyboard_hid.c"
        INCLUDE_DIRS "include"
        REQUIRES codex_control
        PRIV_REQUIRES bt esp_hid
    )
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
else()
    project(microstick_keyboard_hid LANGUAGES C)
    add_library(microstick_keyboard_hid STATIC microstick_keyboard_hid.c)
    target_include_directories(microstick_keyboard_hid PUBLIC include)
    target_compile_features(microstick_keyboard_hid PUBLIC c_std_11)
    target_compile_options(microstick_keyboard_hid PRIVATE -Wall -Wextra -Werror)
    enable_testing()
    add_executable(microstick_keyboard_hid_test test/microstick_keyboard_hid_test.c)
    target_link_libraries(microstick_keyboard_hid_test PRIVATE microstick_keyboard_hid)
    target_compile_options(microstick_keyboard_hid_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME microstick_keyboard_hid_test COMMAND microstick_keyboard_hid_test)
endif()
