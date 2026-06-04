/* FatFs configuration for 06-sd project.
 * Revision ID must match FF_DEFINED in ff.h (R0.14b = 80286). */

#define FFCONF_DEF  80286

/* ── Function set ──────────────────────────────────────────────────────────── */
#define FF_FS_READONLY  0   /* read/write */
#define FF_FS_MINIMIZE  0   /* all API functions enabled */
#define FF_USE_FIND     0
#define FF_USE_MKFS     0   /* no format needed */
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND   0
#define FF_USE_CHMOD    0
#define FF_USE_LABEL    0
#define FF_USE_FORWARD  0
#define FF_USE_STRFUNC  0

/* ── Locale ────────────────────────────────────────────────────────────────── */
#define FF_CODE_PAGE    437 /* U.S. */
#define FF_USE_LFN      0   /* no long file names — 001.wav fits in 8.3 */
#define FF_MAX_LFN      12
#define FF_LFN_UNICODE  0
#define FF_LFN_BUF      12
#define FF_SFN_BUF      12
#define FF_FS_RPATH     0

/* ── Drive/volume ──────────────────────────────────────────────────────────── */
#define FF_VOLUMES          1   /* single SD card */
#define FF_STR_VOLUME_ID    0
#define FF_MULTI_PARTITION  0
#define FF_MIN_SS           512
#define FF_MAX_SS           512
#define FF_LBA64            0
#define FF_MIN_GPT          0x10000000
#define FF_USE_TRIM         0

/* ── System ────────────────────────────────────────────────────────────────── */
#define FF_FS_TINY      0
#define FF_FS_EXFAT     0
#define FF_FS_NORTC     1   /* no RTC — use fixed timestamp */
#define FF_NORTC_MON    1
#define FF_NORTC_MDAY   1
#define FF_NORTC_YEAR   2026
#define FF_FS_NOFSINFO  0
#define FF_FS_LOCK      0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT   1000
