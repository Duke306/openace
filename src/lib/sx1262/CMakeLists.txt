cmake_minimum_required(VERSION 3.10.0)

project(sx1262 VERSION 0.0.0 LANGUAGES CXX)
set(MSG_PREFIX "${PROJECT_NAME} |")

set(MODULE_SOURCE_FILES
    ace/src/radio_base.cpp
    ace/src/sx1262.cpp
    ace/src/sx1262_hal.cpp
    ace/src/lr2021.cpp
    ace/src/lr2021_hal.cpp
    ace/src/rxdataframequeue.cpp

    # Driver from Semtech
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x.c
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x_lr_fhss.c
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x_driver_version.c

    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_system.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_radio_common.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_radio_fifo.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_radio_fsk.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_radio_lora.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_regmem.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_workarounds.c
    usp/smtc_rac_lib/radio_drivers/lr20xx_driver/src/lr20xx_driver_version.c
)

set(MODULE_TARGET_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/usp/smtc_rac_lib/radio_drivers/lr20xx_driver/inc
)

set(MODULE_TARGET_LINK
    utils
)

set(PICO_EXTRA_LIBS
    hardware_spi
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../gatas_module.cmake)
