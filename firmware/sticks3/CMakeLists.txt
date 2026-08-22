cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS components)
set(PROJECT_VER "1.2.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Restrict discovery to the product component and its explicit dependencies.
# This keeps the firmware independent of unrelated ESP-IDF components such as
# Wi-Fi and MQTT, which are intentionally absent from the runtime.
idf_build_set_property(MINIMAL_BUILD ON)
project(microstick_sticks3)
