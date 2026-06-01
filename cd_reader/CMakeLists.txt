cmake_minimum_required(VERSION 3.15)
set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)
include(pico_sdk_import.cmake)
project(sd_reader C CXX ASM)
pico_sdk_init()

add_subdirectory(C:/Repositories/no-OS-FatFs-SD-SPI-RPi-Pico/FatFs_SPI FatFs_SPI)

add_executable(${PROJECT_NAME}
    main.c
    hw_config.c
)

target_link_libraries(${PROJECT_NAME}
    hardware_spi
    pico_stdlib
    FatFs_SPI
)

pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/memmap_rp2040.ld)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_add_extra_outputs(${PROJECT_NAME})