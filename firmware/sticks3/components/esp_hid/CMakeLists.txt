idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    return()
endif()

set(idf_esp_hid_dir "$ENV{IDF_PATH}/components/esp_hid")
set(srcs
    "${idf_esp_hid_dir}/src/esp_hidd.c"
    "${idf_esp_hid_dir}/src/esp_hidh.c"
    "${idf_esp_hid_dir}/src/esp_hid_common.c"
)

if(CONFIG_BT_ENABLED)
    if(CONFIG_BT_BLUEDROID_ENABLED)
        list(APPEND srcs
            "src/ble_hidd.c"
            "${idf_esp_hid_dir}/src/ble_hidh.c"
            "${idf_esp_hid_dir}/src/bt_hidh.c"
            "${idf_esp_hid_dir}/src/bt_hidd.c"
        )
    endif()
    if(CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND srcs
            "${idf_esp_hid_dir}/src/nimble_hidd.c"
            "${idf_esp_hid_dir}/src/nimble_hidh.c"
        )
    endif()
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${idf_esp_hid_dir}/include"
    PRIV_INCLUDE_DIRS "${idf_esp_hid_dir}/private"
    REQUIRES esp_event bt
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
