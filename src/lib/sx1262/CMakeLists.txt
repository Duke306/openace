cmake_minimum_required(VERSION 3.10.0)

project(sx1262 VERSION 0.0.0 LANGUAGES CXX)
set(MSG_PREFIX "${PROJECT_NAME} |")

set(MODULE_SOURCE_FILES
    ace/src/sx1262.cpp
    ace/src/sx1262_hal.cpp

    ace/src/lr2021.cpp
    ace/src/lr2021_hal.cpp

    ace/src/rxdataframequeue.cpp

    # Driver from Semtech
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x.c 
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x_lr_fhss.c
    usp/smtc_rac_lib/radio_drivers/sx126x_driver/src/sx126x_driver_version.c
)

set(MODULE_TARGET_LINK
    utils
)

set(PICO_EXTRA_LIBS
    hardware_spi
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../gatas_module.cmake)
