cmake_minimum_required(VERSION 3.15)

project(bme280_driver)

# Создаем статическую библиотеку
add_library(${PROJECT_NAME} STATIC
    "src/bme280-driver.c"
)

# Публичные заголовки (которые будут видны тем, кто подключит библиотеку)
target_include_directories(${PROJECT_NAME} PUBLIC
    "include"    
)
