#include "core/storage.h"

#include <stdlib.h>

#include "core/log.h"
#include "main/util.h"


int open_file_storage(struct file_storage* fstorage, size_t size, const char* filename)
{
    /* ! Take ownership of filename ! */
    fstorage->filename = filename;
    fstorage->size = size;
    fstorage->first_access = 1;

    /* allocate memory for holding data */
    fstorage->data = malloc(fstorage->size);
    if (fstorage->data == NULL) {
        return -1;
    }

    return read_from_file(fstorage->filename, fstorage->data, fstorage->size);
}

int open_rom_file_storage(struct file_storage* fstorage, const char* filename)
{
    fstorage->data = NULL;
    fstorage->size = 0;
    fstorage->filename = NULL;
    fstorage->first_access = 1;

    file_status_t err = load_file(filename, (void**)&fstorage->data, &fstorage->size);

    if (err == file_ok) {
        /* ! take ownsership of filename ! */
        fstorage->filename = filename;
    }

    return err;
}

void close_file_storage(struct file_storage* fstorage)
{
    if(fstorage->data)
    {
        free((void*)fstorage->data);
        fstorage->data = NULL;
    }

    if(fstorage->filename)
    {
        free((void*)fstorage->filename);
        fstorage->filename = NULL;
    }
}


static uint8_t* file_storage_data(const void* storage)
{
    const struct file_storage* fstorage = (const struct file_storage*)storage;
    return fstorage->data;
}

static size_t file_storage_size(const void* storage)
{
    const struct file_storage* fstorage = (const struct file_storage*)storage;
    return fstorage->size;
}

static void file_storage_save(void* storage, size_t start, size_t size)
{
    
    struct file_storage* fstorage = (struct file_storage*)storage;

    file_status_t err;

    /* On first save access ignore start/size and write full storage content,
     * otherwise write only updated chunk */
    if (fstorage->first_access) {
        fstorage->first_access = 0;
        err = write_to_file(fstorage->filename, fstorage->data, fstorage->size);
    }
    else {
        err = write_chunk_to_file(fstorage->filename, fstorage->data + start, size, start);
    }

    switch(err)
    {
    case file_open_error:
        core_log(CORE_LOG_WARNING, "Couldn't open storage file '%s' for writing", fstorage->filename);
        break;
    case file_write_error:
        core_log(CORE_LOG_WARNING, "Failed to write storage file '%s'", fstorage->filename);
        break;
    default:
        break;
    }
}

static void file_storage_parent_save(void* storage, size_t start, size_t size)
{
    struct file_storage* fstorage = (struct file_storage*)((struct file_storage*)storage)->filename;
    file_storage_save(fstorage, start, size);
}

static void dummy_save(void* storage, size_t start, size_t size)
{
    /* do nothing */
}


const struct storage_backend_interface g_ifile_storage =
{
    file_storage_data,
    file_storage_size,
    file_storage_save
};


const struct storage_backend_interface g_ifile_storage_ro =
{
    file_storage_data,
    file_storage_size,
    dummy_save
};

const struct storage_backend_interface g_isubfile_storage =
{
    file_storage_data,
    file_storage_size,
    file_storage_parent_save
};
