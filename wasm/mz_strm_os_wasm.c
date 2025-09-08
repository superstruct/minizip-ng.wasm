/* mz_strm_os_wasm.c -- Stream for WASM/Emscripten file I/O
   Copyright (C) 2025 Superstruct Ltd, New Zealand
   Licensed under the same terms as minizip-ng
   
   This implements WASM-specific stream operations using Emscripten file system APIs
*/

#ifdef __EMSCRIPTEN__

#include "mz.h"
#include "mz_strm.h"
#include "mz_strm_os.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <emscripten/emscripten.h>
#include <emscripten/console.h>

/***************************************************************************/

static mz_stream_vtbl mz_stream_os_wasm_vtbl = {
    mz_stream_os_open,
    mz_stream_os_is_open,
    mz_stream_os_read,
    mz_stream_os_write,
    mz_stream_os_tell,
    mz_stream_os_seek,
    mz_stream_os_close,
    mz_stream_os_error,
    mz_stream_os_create,
    mz_stream_os_delete,
    NULL,
    NULL
};

/***************************************************************************/

typedef struct mz_stream_os_s {
    mz_stream   stream;
    FILE        *file;
    int32_t     error;
    char        filename[PATH_MAX];
} mz_stream_os;

/***************************************************************************/

int32_t mz_stream_os_open(void *stream, const char *path, int32_t mode) {
    mz_stream_os *os = (mz_stream_os *)stream;
    const char *fopen_mode = NULL;
    char resolved_path[PATH_MAX];
    
    if (!os || !path)
        return MZ_PARAM_ERROR;

    /* Resolve WASM-specific path handling */
    if (strncmp(path, "/cdn-", 5) == 0 || strncmp(path, "/idbfs-", 7) == 0) {
        /* These are virtual paths handled by WASM filesystem */
        strncpy(resolved_path, path, sizeof(resolved_path) - 1);
        resolved_path[sizeof(resolved_path) - 1] = 0;
    } else {
        /* Regular path resolution */
        if (realpath(path, resolved_path) == NULL) {
            /* If realpath fails, use the original path */
            strncpy(resolved_path, path, sizeof(resolved_path) - 1);
            resolved_path[sizeof(resolved_path) - 1] = 0;
        }
    }

    /* Determine file open mode */
    if (mode & MZ_OPEN_MODE_READWRITE) {
        if (mode & MZ_OPEN_MODE_APPEND)
            fopen_mode = "a+b";
        else if (mode & MZ_OPEN_MODE_CREATE)
            fopen_mode = "w+b";
        else
            fopen_mode = "r+b";
    } else if (mode & MZ_OPEN_MODE_WRITE) {
        if (mode & MZ_OPEN_MODE_APPEND)
            fopen_mode = "ab";
        else if (mode & MZ_OPEN_MODE_CREATE)
            fopen_mode = "wb";
        else
            return MZ_OPEN_ERROR;
    } else if (mode & MZ_OPEN_MODE_READ) {
        fopen_mode = "rb";
    } else {
        return MZ_PARAM_ERROR;
    }

    os->file = fopen(resolved_path, fopen_mode);
    if (!os->file) {
        os->error = errno;
        emscripten_console_logf("mz_stream_os_open: Failed to open %s (mode: %s, errno: %d)", 
                                resolved_path, fopen_mode, errno);
        return MZ_OPEN_ERROR;
    }

    strncpy(os->filename, resolved_path, sizeof(os->filename) - 1);
    os->filename[sizeof(os->filename) - 1] = 0;
    os->error = 0;
    
    emscripten_console_logf("mz_stream_os_open: Successfully opened %s", resolved_path);
    return MZ_OK;
}

int32_t mz_stream_os_is_open(void *stream) {
    mz_stream_os *os = (mz_stream_os *)stream;
    
    if (!os)
        return MZ_PARAM_ERROR;
        
    return (os->file != NULL) ? MZ_OK : MZ_OPEN_ERROR;
}

int32_t mz_stream_os_read(void *stream, void *buf, int32_t size) {
    mz_stream_os *os = (mz_stream_os *)stream;
    int32_t bytes_read = 0;
    
    if (!os || !buf || !os->file)
        return MZ_PARAM_ERROR;

    bytes_read = (int32_t)fread(buf, 1, (size_t)size, os->file);
    if (bytes_read < size && ferror(os->file)) {
        os->error = errno;
        return MZ_READ_ERROR;
    }
    
    return bytes_read;
}

int32_t mz_stream_os_write(void *stream, const void *buf, int32_t size) {
    mz_stream_os *os = (mz_stream_os *)stream;
    int32_t bytes_written = 0;
    
    if (!os || !buf || !os->file)
        return MZ_PARAM_ERROR;

    bytes_written = (int32_t)fwrite(buf, 1, (size_t)size, os->file);
    if (bytes_written < size) {
        os->error = errno;
        return MZ_WRITE_ERROR;
    }
    
    return bytes_written;
}

int64_t mz_stream_os_tell(void *stream) {
    mz_stream_os *os = (mz_stream_os *)stream;
    int64_t position = 0;
    
    if (!os || !os->file)
        return MZ_PARAM_ERROR;

    position = ftello64(os->file);
    if (position == -1) {
        os->error = errno;
        return MZ_TELL_ERROR;
    }
    
    return position;
}

int32_t mz_stream_os_seek(void *stream, int64_t offset, int32_t origin) {
    mz_stream_os *os = (mz_stream_os *)stream;
    int32_t fseek_origin = 0;
    
    if (!os || !os->file)
        return MZ_PARAM_ERROR;

    switch (origin) {
        case MZ_SEEK_CUR:
            fseek_origin = SEEK_CUR;
            break;
        case MZ_SEEK_END:
            fseek_origin = SEEK_END;
            break;
        case MZ_SEEK_SET:
            fseek_origin = SEEK_SET;
            break;
        default:
            return MZ_PARAM_ERROR;
    }

    if (fseeko64(os->file, offset, fseek_origin) != 0) {
        os->error = errno;
        return MZ_SEEK_ERROR;
    }
    
    return MZ_OK;
}

int32_t mz_stream_os_close(void *stream) {
    mz_stream_os *os = (mz_stream_os *)stream;
    int32_t closed = 0;
    
    if (!os)
        return MZ_PARAM_ERROR;

    if (os->file) {
        closed = fclose(os->file);
        os->file = NULL;
        
        if (closed != 0) {
            os->error = errno;
            return MZ_CLOSE_ERROR;
        }
    }

    os->error = 0;
    return MZ_OK;
}

int32_t mz_stream_os_error(void *stream) {
    mz_stream_os *os = (mz_stream_os *)stream;
    
    if (!os)
        return MZ_PARAM_ERROR;
        
    return os->error;
}

void *mz_stream_os_create(void **stream) {
    mz_stream_os *os = NULL;

    os = (mz_stream_os *)MZ_ALLOC(sizeof(mz_stream_os));
    if (os) {
        memset(os, 0, sizeof(mz_stream_os));
        os->stream.vtbl = &mz_stream_os_wasm_vtbl;
    }
    
    if (stream)
        *stream = os;
    
    return os;
}

void mz_stream_os_delete(void **stream) {
    mz_stream_os *os = NULL;
    
    if (!stream)
        return;
        
    os = (mz_stream_os *)*stream;
    if (os) {
        mz_stream_os_close(os);
        MZ_FREE(os);
        *stream = NULL;
    }
}

void *mz_stream_os_get_interface(void) {
    return (void *)&mz_stream_os_wasm_vtbl;
}

/* WASM-specific stream functions */

int32_t mz_stream_os_wasm_load_from_url(void *stream, const char *url) {
    mz_stream_os *os = (mz_stream_os *)stream;
    char temp_path[PATH_MAX];
    int32_t result = MZ_OK;
    
    if (!os || !url)
        return MZ_PARAM_ERROR;
    
    /* Generate temporary file path */
    snprintf(temp_path, sizeof(temp_path), "/tmp/minizip_url_%u.tmp", 
             (unsigned int)time(NULL));
    
    /* Use Emscripten's async wget to download file */
    result = EM_ASM_INT({
        var url = UTF8ToString($0);
        var path = UTF8ToString($1);
        
        try {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', url, false); // Synchronous for simplicity
            xhr.responseType = 'arraybuffer';
            xhr.send();
            
            if (xhr.status === 200) {
                var data = new Uint8Array(xhr.response);
                Module.FS.writeFile(path, data);
                return 0; // MZ_OK
            } else {
                console.error('HTTP error:', xhr.status);
                return -1; // Error
            }
        } catch (e) {
            console.error('Download failed:', e);
            return -1; // Error
        }
    }, url, temp_path);
    
    if (result != 0)
        return MZ_INTERNAL_ERROR;
    
    /* Open the downloaded file */
    return mz_stream_os_open(stream, temp_path, MZ_OPEN_MODE_READ);
}

int32_t mz_stream_os_wasm_save_to_downloads(void *stream, const char *filename) {
    mz_stream_os *os = (mz_stream_os *)stream;
    
    if (!os || !filename || !os->file)
        return MZ_PARAM_ERROR;
    
    /* Use Emscripten's file download API */
    EM_ASM({
        var filename = UTF8ToString($0);
        var filepath = UTF8ToString($1);
        
        try {
            var data = Module.FS.readFile(filepath);
            var blob = new Blob([data], {type: 'application/octet-stream'});
            
            if (typeof Module.saveAs === 'function') {
                Module.saveAs(blob, filename);
            } else {
                // Fallback download method
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = filename;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
            }
        } catch (e) {
            console.error('Download failed:', e);
        }
    }, filename, os->filename);
    
    return MZ_OK;
}

#endif /* __EMSCRIPTEN__ */