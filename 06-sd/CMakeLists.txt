cmake_minimum_required(VERSION 3.15)

set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)

include(pico_sdk_import.cmake)

project(06_sd)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdio-task/stdio-task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/led-task/led-task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sd-task/sd-task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wav-task/wav-task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rec-task/rec-task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/diskio.c
)

add_subdirectory("../libs/protocol"  "${PROJECT_BINARY_DIR}/protocol")
add_subdirectory("../libs/sd-driver" "${PROJECT_BINARY_DIR}/sd-driver")
add_subdirectory("../libs/fatfs"     "${PROJECT_BINARY_DIR}/fatfs")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_adc
    hardware_dma
    pico_multicore
    protocol
    sd_driver
    fatfs
    m       # sinf() из libm
)

pico_set_linker_script(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/memmap_rp2040.ld
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

pico_add_extra_outputs(${PROJECT_NAME})
