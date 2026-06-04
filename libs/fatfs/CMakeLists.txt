cmake_minimum_required(VERSION 3.15)
project(fatfs C)

# FatFS R0.14b уже лежит внутри pico-sdk (tinyusb/lib/fatfs).
# Используем его напрямую, без скачивания.
set(FATFS_SRC "${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source")

add_library(fatfs STATIC
    ${FATFS_SRC}/ff.c
    ${FATFS_SRC}/ffunicode.c   # нужен при FF_USE_LFN != 0 (включён в bundled ffconf.h)
)

# Наш ffconf.h подставляется первым и переопределяет настройки по умолчанию.
target_include_directories(fatfs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FATFS_SRC}
)
