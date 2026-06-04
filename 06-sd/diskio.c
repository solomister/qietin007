#include "ff.h"       /* должен быть первым — определяет BYTE, LBA_t и др. */
#include "diskio.h"
#include "sd-task/sd-task.h"
#include "sd-driver.h"
#include "stdio.h"

/* Единственный физический диск — SD-карта (pdrv = 0). */

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;

    return (sd_task_ensure_init() == SD_OK) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0)
        return STA_NOINIT;

    const sd_t *sd = sd_task_card();
    return (sd && sd->type != SD_TYPE_UNKNOWN) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;

    const sd_t *sd = sd_task_card();
    for (UINT i = 0; i < count; i++) {
        sd_err_t err = sd_read_block(sd, (uint32_t)(sector + i), buff + i * 512);
        if (err != SD_OK) {
            printf("[diskio] disk_read sector %lu failed: %s\n",
                   (unsigned long)(sector + i), sd_err_str(err));
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;

    const sd_t *sd = sd_task_card();
    for (UINT i = 0; i < count; i++) {
        sd_err_t err = sd_write_block(sd, (uint32_t)(sector + i), buff + i * 512);
        if (err != SD_OK) {
            printf("[diskio] disk_write sector %lu failed: %s\n",
                   (unsigned long)(sector + i), sd_err_str(err));
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
