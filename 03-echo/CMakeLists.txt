cmake_minimum_required(VERSION 3.15)


# Задаем параметр с используемой платой и микроконтроллером для Pico SDK - специфика сборки под Pico
set(PICO_BOARD pico)
set(PICO_PLATFORM rp2040)

# Добавляем файл, автоматически подтягивающий Pico SDK в наш проект - специфика сборки под Pico
include(pico_sdk_import.cmake)

# Создаем проект. Аргументом указывается имя проекта - типовая строчка для любого CMakeLists.txt
project(03_echo)

# Вызываем функцию инициализации Pico SDK - специфика сборки под Pico
pico_sdk_init()

# Добавляем в проект файлы с исходным кодом - типовая строчка для любого CMakeLists.txt
add_executable(${PROJECT_NAME}
	${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

# Указываем на какие библиотеки ссылается наш код - типовая строчка для любого CMakeLists.txt
target_link_libraries(${PROJECT_NAME}
	pico_stdlib
)

# Указываем файл, в котором находится линкер скрипт - специфика сборки под МК
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/memmap_rp2040.ld)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 1)
# Добавляем задачи генерации бинарника в разных форматах - специфика сборки под Pico
pico_add_extra_outputs(${PROJECT_NAME})