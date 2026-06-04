cmake_minimum_required(VERSION 3.15)

project(sd_driver)

add_library(${PROJECT_NAME} STATIC
    "src/sd-driver.c"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "include"
)
