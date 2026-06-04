cmake_minimum_required(VERSION 3.15)

project(protocol)

# Создаем статическую библиотеку
add_library(${PROJECT_NAME} STATIC
    "src/protocol-task.c"
)

# Публичные заголовки (которые будут видны тем, кто подключит библиотеку)
target_include_directories(${PROJECT_NAME} PUBLIC
    "include"    
)
