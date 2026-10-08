#include "ff.h"
#include <string.h>
#include "record_replay/motion_record_replay.h"

#define FILE_SLOTS 96U
#define FILE_BYTES (24U + MOTION_RECORD_REPLAY_MAX_SAMPLES * sizeof(MotionSample))
typedef struct {
    char path[48];
    unsigned length;
    unsigned char data[FILE_BYTES];
} MemoryFile;
static MemoryFile files[FILE_SLOTS];
int fail_write, fail_sync, fail_rename, fail_read, corrupt_write;
int fail_rename_after = -1;
unsigned reads, writes, unsafe_storage_writes, invalid_storage_paths;
int harness_storage_write_allowed(void);

static int valid_path(const char *path)
{
    if (path == NULL || strncmp(path, "0:/MOTION", 9U) != 0 ||
        (path[9] != '\0' && path[9] != '/' && strcmp(path, "0:/MOTION.BIN") != 0) || strstr(path, "..") != NULL ||
        strlen(path) >= sizeof(files[0].path)) {
        ++invalid_storage_paths;
        return 0;
    }
    return 1;
}

static unsigned find_file(const char *path)
{
    unsigned index;
    for (index = 0U; index < FILE_SLOTS; ++index)
        if (files[index].path[0] && strcmp(files[index].path, path) == 0) return index;
    return FILE_SLOTS;
}

void fatfs_mock_reset(void)
{
    memset(files, 0, sizeof(files));
    fail_write = fail_sync = fail_rename = fail_read = corrupt_write = 0;
    fail_rename_after = -1;
    reads = writes = unsafe_storage_writes = invalid_storage_paths = 0U;
}

int fatfs_mock_exists(const char *path)
{
    return valid_path(path) && find_file(path) != FILE_SLOTS;
}

const unsigned char *fatfs_mock_data(const char *path, unsigned *length)
{
    unsigned slot;
    if (!valid_path(path) || length == NULL || (slot = find_file(path)) == FILE_SLOTS) return NULL;
    *length = files[slot].length;
    return files[slot].data;
}

FRESULT f_open(FIL *file, const char *path, unsigned char mode)
{
    unsigned slot;
    if (file == NULL || !valid_path(path)) return FR_DISK_ERR;
    slot = find_file(path);
    if ((mode & FA_CREATE_NEW) && slot != FILE_SLOTS) return FR_EXIST;
    if (slot == FILE_SLOTS && !(mode & FA_WRITE)) return FR_NO_FILE;
    if (slot == FILE_SLOTS) {
        for (slot = 0U; slot < FILE_SLOTS && files[slot].path[0]; ++slot) {}
        if (slot == FILE_SLOTS) return FR_DISK_ERR;
        strcpy(files[slot].path, path);
        files[slot].length = 0U;
    }
    file->slot = slot;
    file->position = 0U;
    file->length = files[slot].length;
    file->mode = mode;
    return FR_OK;
}

FRESULT f_read(FIL *file, void *buffer, UINT length, UINT *received)
{
    unsigned available;
    ++reads;
    *received = 0U;
    if (fail_read || file->slot >= FILE_SLOTS || !(file->mode & FA_READ)) return FR_DISK_ERR;
    available = files[file->slot].length - file->position;
    if (length > available) length = available;
    memcpy(buffer, files[file->slot].data + file->position, length);
    file->position += length;
    *received = length;
    return FR_OK;
}

FRESULT f_write(FIL *file, const void *buffer, UINT length, UINT *written)
{
    ++writes;
    *written = 0U;
    if (!harness_storage_write_allowed()) {
        ++unsafe_storage_writes;
        return FR_DISK_ERR;
    }
    if (fail_write || file->slot >= FILE_SLOTS || !(file->mode & FA_WRITE) ||
        length > FILE_BYTES - file->position) return FR_DISK_ERR;
    memcpy(files[file->slot].data + file->position, buffer, length);
    if (corrupt_write && length > 24U) files[file->slot].data[file->position] ^= 1U;
    file->position += length;
    if (file->position > files[file->slot].length) files[file->slot].length = file->position;
    file->length = files[file->slot].length;
    *written = length;
    return FR_OK;
}

FRESULT f_close(FIL *file)
{
    file->slot = FILE_SLOTS;
    return FR_OK;
}

FRESULT f_sync(FIL *file)
{
    return !fail_sync && file->slot < FILE_SLOTS ? FR_OK : FR_DISK_ERR;
}

FRESULT f_stat(const char *path, FILINFO *info)
{
    unsigned slot;
    if (!valid_path(path)) return FR_DISK_ERR;
    if ((slot = find_file(path)) == FILE_SLOTS) return FR_NO_FILE;
    info->fsize = files[slot].length;
    return FR_OK;
}

FRESULT f_mkdir(const char *path)
{
    return valid_path(path) && strcmp(path, "0:/MOTION") == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_unlink(const char *path)
{
    unsigned slot;
    if (!valid_path(path)) return FR_DISK_ERR;
    if ((slot = find_file(path)) == FILE_SLOTS) return FR_NO_FILE;
    memset(&files[slot], 0, sizeof(files[slot]));
    return FR_OK;
}

FRESULT f_rename(const char *source, const char *destination)
{
    unsigned slot;
    if (!valid_path(source) || !valid_path(destination) || fail_rename ||
        fail_rename_after == 0 || find_file(destination) != FILE_SLOTS ||
        (slot = find_file(source)) == FILE_SLOTS) return FR_DISK_ERR;
    if (fail_rename_after > 0) --fail_rename_after;
    strcpy(files[slot].path, destination);
    return FR_OK;
}
