#ifndef N64EMU_STORAGE_H
#define N64EMU_STORAGE_H

#include <stddef.h>
#include <stdint.h>

struct storage_backend_interface
{
    /* Returns a pointer to storage data
     */
    uint8_t* (*data)(const void* storage);

    /* Returns the size of the storage
     */
    size_t (*size)(const void* storage);

    /* Notify the storage backend that data should be persisted
     */
    void (*save)(void* storage, size_t start, size_t size);
};

struct file_storage
{
    uint8_t* data;
    size_t size;
    const char* filename;
    int first_access;
};

#ifdef __cplusplus
extern "C" {
#endif

/* open_file_storage takes ownership of filename even if reading fails.
 * The data buffer remains available for formatting a new save.
 * open_rom_file_storage takes ownership only on success.
 * close_file_storage releases both data and filename.
 */
int open_file_storage(struct file_storage* storage, size_t size, const char* filename);
int open_rom_file_storage(struct file_storage* storage, const char* filename);
void close_file_storage(struct file_storage* storage);

extern const struct storage_backend_interface g_ifile_storage;
extern const struct storage_backend_interface g_ifile_storage_ro;
extern const struct storage_backend_interface g_isubfile_storage;

#ifdef __cplusplus
}
#endif

#endif
