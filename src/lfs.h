
#ifdef __cplusplus
extern "C" {
#endif
    #include "littlefs/lfs.h"
    #include "types.h"
    static int lfs_read(const struct lfs_config *c, lfs_block_t block,
                    lfs_off_t off, void *buffer, lfs_size_t size);
    static int lfs_prog(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, const void *buffer, lfs_size_t size);
    static int lfs_erase(const struct lfs_config *c, lfs_block_t block);
    static int lfs_sync(const struct lfs_config *c);

    void initLFS(void);
    bool readSettings(SETTINGS_T *p);
    bool writeSettings(SETTINGS_T *p);

    bool hasCACert(void);
    int  readCACert(char *buf, size_t bufsz);
    bool writeCACert(const char *pem, size_t len);
    bool deleteCACert(void);
    int  caCertSize(void);

    // E-lazy streaming cursor over the CA bundle (ca.pem)
    bool caBundleOpen(void);
    int  caBundleRead(void *buf, size_t n);
    void caBundleClose(void);

#ifdef __cplusplus
} //extern "C"
#endif