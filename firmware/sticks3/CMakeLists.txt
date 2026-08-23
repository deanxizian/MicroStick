cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS components)
set(PROJECT_VER "1.4.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Restrict discovery to the product component and its explicit dependencies.
# Product code initializes no network interface or service; ESP-IDF may still
# resolve low-level Wi-Fi/coexistence components transitively for Bluetooth.
idf_build_set_property(MINIMAL_BUILD ON)
project(microstick_sticks3)
