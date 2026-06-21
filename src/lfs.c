
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "littlefs/lfs.h"
#include "pico/printf.h"
#include "lfs.h"

static const char settings_fname[] = "settings.cfg";
static const char ca_fname[] = "ca.pem";   // PEM CA bundle for TLS verification

lfs_t lfs_volume;
lfs_file_t lfs_file;

#define LFS_DISK_BLOCKS 128

#define LFS_DISK_SIZE (LFS_DISK_BLOCKS * FLASH_SECTOR_SIZE)
#define LFS_LOOKAHEAD_SIZE (LFS_DISK_BLOCKS / 8)

static char lfs_read_buffer[FLASH_PAGE_SIZE] __attribute__((aligned (4))) ;
static char lfs_prog_buffer[FLASH_PAGE_SIZE] __attribute__((aligned (4))) ;
static char lfs_lookahead_buffer[LFS_LOOKAHEAD_SIZE] __attribute__((aligned (4))) ;
static struct lfs_config cfg;

static int lfs_read(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)(c);
    memcpy(buffer,
           (void *)XIP_NOCACHE_NOALLOC_BASE +
               (PICO_FLASH_SIZE_BYTES - LFS_DISK_SIZE) +
               (block * FLASH_SECTOR_SIZE) +
               off,
           size);
    return LFS_ERR_OK;
}

static int lfs_prog(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)(c);
    uint32_t flash_offs = (PICO_FLASH_SIZE_BYTES - LFS_DISK_SIZE) +
                          (block * FLASH_SECTOR_SIZE) +
                          off;
    // Disable interrupts during the flash op: while flash is being programmed
    // XIP is unavailable, so any ISR executing from flash (e.g. the cyw43
    // background WiFi IRQ under pico_cyw43_arch_lwip_threadsafe_background)
    // would hard-fault and hang the chip. Symptom: AT&W froze the modem.
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offs, (const uint8_t*)buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)(c);
    uint32_t flash_offs = (PICO_FLASH_SIZE_BYTES - LFS_DISK_SIZE) +
                          (block * FLASH_SECTOR_SIZE);
    uint32_t ints = save_and_disable_interrupts();   // see lfs_prog note
    flash_range_erase(flash_offs, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int lfs_sync(const struct lfs_config *c)
{
    (void)(c);
    return LFS_ERR_OK;
}

void initLFS(void)
{
    memset((void *)&cfg, 0, sizeof(cfg));
    cfg = (struct lfs_config) {
        .read = lfs_read,
        .prog = lfs_prog,
        .erase = lfs_erase,
        .sync = lfs_sync,
        .read_size = 1,
        .prog_size = FLASH_PAGE_SIZE,
        .block_size = FLASH_SECTOR_SIZE,
        .block_count = LFS_DISK_SIZE / FLASH_SECTOR_SIZE,
        .block_cycles = 100,
        .cache_size = FLASH_PAGE_SIZE,
        .lookahead_size = LFS_LOOKAHEAD_SIZE,
        .read_buffer = lfs_read_buffer,
        .prog_buffer = lfs_prog_buffer,
        .lookahead_buffer = lfs_lookahead_buffer,
    };
    int err = lfs_mount(&lfs_volume, &cfg);
    if (err)
    {
        // Maybe first boot. Attempt format.
        // lfs_format returns -84 here, but still succeeds
        printf("Formatting - please wait\n");
        lfs_format(&lfs_volume, &cfg);
        err = lfs_mount(&lfs_volume, &cfg);
        if (err)
            printf("?Unable to format lfs (%d)", err);
        else
            printf("Formatting done\n");
    }

}

bool readSettings(SETTINGS_T *p) {
   bool ok = false;
   uint8_t file_buffer[FLASH_PAGE_SIZE];
   struct lfs_file_config file_config = {
        .buffer = file_buffer,
    };
   if(lfs_file_opencfg(&lfs_volume, &lfs_file, settings_fname, LFS_O_RDONLY, &file_config) == LFS_ERR_OK){
        if(lfs_file_read(&lfs_volume, &lfs_file, p, sizeof(SETTINGS_T)) == sizeof(SETTINGS_T))
            ok = true;
        lfs_file_close(&lfs_volume, &lfs_file);
   }
   return ok;
}

bool writeSettings(SETTINGS_T *p) {
   bool ok = false;
   uint8_t file_buffer[FLASH_PAGE_SIZE];
   struct lfs_file_config file_config = {
        .buffer = file_buffer,
    };
   if(lfs_file_opencfg(&lfs_volume, &lfs_file, settings_fname, LFS_O_RDWR | LFS_O_CREAT, &file_config) == LFS_ERR_OK){
        if(lfs_file_write(&lfs_volume, &lfs_file, p, sizeof(SETTINGS_T)) == sizeof(SETTINGS_T)){
            ok = true;
        }
        lfs_file_close(&lfs_volume, &lfs_file);
   }
   return ok;
}

// ── TLS CA certificate (PEM) stored as a LittleFS file ──
// The CA is provisioned out-of-band in the LittleFS image; these helpers let
// the TLS path load it and the AT layer report/clear it.

bool hasCACert(void) {
   struct lfs_info info;
   if( lfs_stat(&lfs_volume, ca_fname, &info) == LFS_ERR_OK )
      return info.size > 0;
   return false;
}

// Read the CA into buf (NUL-terminated). Returns the byte count (excluding the
// NUL), or -1 if absent/unreadable. mbedTLS PEM parsing needs the NUL included,
// so callers pass (returned length + 1) as the buffer length.
int readCACert(char *buf, size_t bufsz) {
   int n = -1;
   uint8_t file_buffer[FLASH_PAGE_SIZE];
   struct lfs_file_config file_config = { .buffer = file_buffer };
   if( bufsz == 0 ) return -1;
   if( lfs_file_opencfg(&lfs_volume, &lfs_file, ca_fname, LFS_O_RDONLY, &file_config) == LFS_ERR_OK ) {
      lfs_ssize_t r = lfs_file_read(&lfs_volume, &lfs_file, buf, bufsz - 1);
      if( r >= 0 ) { buf[r] = '\0'; n = (int)r; }
      lfs_file_close(&lfs_volume, &lfs_file);
   }
   return n;
}

bool writeCACert(const char *pem, size_t len) {
   bool ok = false;
   uint8_t file_buffer[FLASH_PAGE_SIZE];
   struct lfs_file_config file_config = { .buffer = file_buffer };
   if( lfs_file_opencfg(&lfs_volume, &lfs_file, ca_fname,
                        LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC, &file_config) == LFS_ERR_OK ) {
      if( lfs_file_write(&lfs_volume, &lfs_file, pem, len) == (lfs_ssize_t)len )
         ok = true;
      lfs_file_close(&lfs_volume, &lfs_file);
   }
   return ok;
}

bool deleteCACert(void) {
   return lfs_remove(&lfs_volume, ca_fname) == LFS_ERR_OK;
}

int caCertSize(void) {
   struct lfs_info info;
   if( lfs_stat(&lfs_volume, ca_fname, &info) == LFS_ERR_OK )
      return (int)info.size;
   return 0;
}