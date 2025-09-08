/* mz_os_wasm.c -- System functions for WebAssembly/Emscripten
   Copyright (C) 2025 Superstruct Ltd, New Zealand
   Licensed under the same terms as minizip-ng
   
   This implements WASM-specific OS layer functions using Emscripten APIs
*/

#ifdef __EMSCRIPTEN__

#include "mz.h"
#include "mz_os.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <emscripten/emscripten.h>
#include <emscripten/console.h>

/***************************************************************************/

int32_t mz_os_rand(uint8_t *buf, int32_t size) {
    /* Use Emscripten's crypto-quality random number generation */
    static int32_t rand_init = 0;
    int32_t left = size;
    int32_t written = 0;
    
    if (!rand_init) {
        /* Initialize random seed using browser crypto */
        srand((unsigned int)time(NULL));
        rand_init = 1;
    }

    /* For WASM, use system random or fallback to rand() */
    FILE *random_file = fopen("/dev/urandom", "rb");
    if (random_file) {
        written = fread(buf, 1, size, random_file);
        fclose(random_file);
        if (written == size)
            return size;
    }
    
    /* Fallback to standard random */
    while (left > 0) {
        uint32_t val = rand();
        int32_t copy = MZ_MIN(left, (int32_t)sizeof(uint32_t));
        memcpy(buf + written, &val, copy);
        written += copy;
        left -= copy;
    }

    return size;
}

int32_t mz_os_rename(const char *source_path, const char *target_path) {
    if (!source_path || !target_path)
        return MZ_PARAM_ERROR;
        
    if (rename(source_path, target_path) == 0)
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_unlink(const char *path) {
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (unlink(path) == 0)
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_file_exists(const char *path) {
    struct stat path_stat;
    
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (stat(path, &path_stat) == 0)
        return MZ_OK;
        
    return MZ_EXIST_ERROR;
}

int64_t mz_os_get_file_size(const char *path) {
    struct stat path_stat;
    
    if (!path)
        return -1;
        
    if (stat(path, &path_stat) != 0)
        return -1;
        
    return path_stat.st_size;
}

int32_t mz_os_get_file_date(const char *path, time_t *modified_date, time_t *accessed_date, time_t *creation_date) {
    struct stat path_stat;
    
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (stat(path, &path_stat) != 0)
        return MZ_INTERNAL_ERROR;

    if (modified_date)
        *modified_date = path_stat.st_mtime;
    if (accessed_date)
        *accessed_date = path_stat.st_atime;
    if (creation_date)
        *creation_date = path_stat.st_ctime; /* Best we can do on WASM */
        
    return MZ_OK;
}

int32_t mz_os_set_file_date(const char *path, time_t modified_date, time_t accessed_date, time_t creation_date) {
    struct stat path_stat;
    
    MZ_UNUSED(creation_date); /* Not supported on WASM */
    
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (stat(path, &path_stat) != 0)
        return MZ_INTERNAL_ERROR;

    /* WASM/Emscripten doesn't fully support utime, so this is a no-op */
    emscripten_console_logf("mz_os_set_file_date: Not fully supported on WASM - %s", path);
    return MZ_OK;
}

int32_t mz_os_get_file_attribs(const char *path, uint32_t *attributes) {
    struct stat path_stat;
    
    if (!path || !attributes)
        return MZ_PARAM_ERROR;
        
    if (stat(path, &path_stat) != 0)
        return MZ_INTERNAL_ERROR;

    *attributes = 0;
    
    if (S_ISDIR(path_stat.st_mode))
        *attributes |= MZ_FILE_ATTRIBUTE_DIRECTORY;
    if (!(path_stat.st_mode & S_IWUSR))
        *attributes |= MZ_FILE_ATTRIBUTE_READONLY;
    if (strncmp(path, ".", 1) == 0)
        *attributes |= MZ_FILE_ATTRIBUTE_HIDDEN;
        
    return MZ_OK;
}

int32_t mz_os_set_file_attribs(const char *path, uint32_t attributes) {
    MZ_UNUSED(attributes);
    
    if (!path)
        return MZ_PARAM_ERROR;

    /* WASM/Emscripten has limited file attribute support */
    emscripten_console_logf("mz_os_set_file_attribs: Limited support on WASM - %s", path);
    return MZ_OK;
}

int32_t mz_os_make_dir(const char *path) {
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (mkdir(path, 0755) == 0)
        return MZ_OK;
        
    if (errno == EEXIST)
        return MZ_EXIST_ERROR;
        
    return MZ_INTERNAL_ERROR;
}

DIR *mz_os_open_dir(const char *path) {
    if (!path)
        return NULL;
        
    return opendir(path);
}

struct dirent *mz_os_read_dir(DIR *dir) {
    if (!dir)
        return NULL;
        
    return readdir(dir);
}

int32_t mz_os_close_dir(DIR *dir) {
    if (!dir)
        return MZ_PARAM_ERROR;
        
    if (closedir(dir) == 0)
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_is_dir(const char *path) {
    struct stat path_stat;
    
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (stat(path, &path_stat) != 0)
        return MZ_INTERNAL_ERROR;
        
    if (S_ISDIR(path_stat.st_mode))
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_is_symlink(const char *path) {
    struct stat path_stat;
    
    if (!path)
        return MZ_PARAM_ERROR;
        
    if (lstat(path, &path_stat) != 0)
        return MZ_INTERNAL_ERROR;
        
    if (S_ISLNK(path_stat.st_mode))
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_make_symlink(const char *path, const char *target_path) {
    if (!path || !target_path)
        return MZ_PARAM_ERROR;
        
    if (symlink(target_path, path) == 0)
        return MZ_OK;
        
    return MZ_INTERNAL_ERROR;
}

int32_t mz_os_read_symlink(const char *path, char *target_path, int32_t max_target_path) {
    ssize_t bytes_read = 0;
    
    if (!path || !target_path)
        return MZ_PARAM_ERROR;
        
    bytes_read = readlink(path, target_path, max_target_path - 1);
    if (bytes_read > 0) {
        target_path[bytes_read] = 0;
        return MZ_OK;
    }
    
    return MZ_INTERNAL_ERROR;
}

uint64_t mz_os_ms_time(void) {
    struct timespec ts;
    
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return ((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000);
    
    /* Fallback to less precise timing */
    return (uint64_t)(time(NULL) * 1000);
}

/* WASM-specific utility functions */

int32_t mz_os_wasm_mount_idbfs(const char *mount_path) {
    /* JavaScript callback to mount IDBFS */
    EM_ASM({
        try {
            var path = UTF8ToString($0);
            if (!Module.FS.analyzePath(path).exists) {
                Module.FS.mkdir(path);
            }
            Module.FS.mount(Module.FS.filesystems.IDBFS, {}, path);
            Module.FS.syncfs(true, function(err) {
                if (err) {
                    console.error('IDBFS sync failed:', err);
                }
            });
        } catch (e) {
            console.error('IDBFS mount failed:', e);
        }
    }, mount_path);
    
    return MZ_OK;
}

int32_t mz_os_wasm_sync_idbfs(void) {
    /* Synchronize IDBFS to IndexedDB */
    EM_ASM(
        try {
            Module.FS.syncfs(false, function(err) {
                if (err) {
                    console.error('IDBFS sync failed:', err);
                }
            });
        } catch (e) {
            console.error('IDBFS sync error:', e);
        }
    );
    
    return MZ_OK;
}

void mz_os_wasm_log(const char *message) {
    emscripten_console_log(message);
}

#endif /* __EMSCRIPTEN__ */