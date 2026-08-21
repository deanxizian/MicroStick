cmake_minimum_required(VERSION 3.16)
project(microstick_usage_protocol_tests LANGUAGES C)

add_executable(microstick_usage_protocol_test
    usage_protocol_test.c
    ../usage_protocol.c
    ../usage_snapshot.c
)
target_include_directories(microstick_usage_protocol_test PRIVATE ..)
target_compile_features(microstick_usage_protocol_test PRIVATE c_std_11)
target_compile_options(microstick_usage_protocol_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME microstick_usage_protocol_test COMMAND microstick_usage_protocol_test)
