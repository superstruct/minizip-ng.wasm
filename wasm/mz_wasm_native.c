/* mz_wasm_native.c -- WASM-native filesystem and CDN loading features
   Copyright (C) 2025 Superstruct Ltd, New Zealand
   Licensed under the same terms as minizip-ng
   
   This implements WASM-native patterns including IDBFS persistent storage,
   async CDN loading, and progressive resource management
*/

#ifdef __EMSCRIPTEN__

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_wasm_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <emscripten/emscripten.h>
#include <emscripten/console.h>
#include <emscripten/fetch.h>

/***************************************************************************/

/* WASM-native virtual file system paths */
#define MZ_WASM_ARCHIVE_CACHE   "/archive-cache"
#define MZ_WASM_TEMP_DIR        "/archive-temp"  
#define MZ_WASM_CDN_DIR         "/cdn-archives"
#define MZ_WASM_PACKAGES_DIR    "/archive-packages"

/* Cache management */
static int32_t g_cache_initialized = 0;
static int64_t g_cache_max_size = 100 * 1024 * 1024; /* 100MB default */
static int64_t g_cache_current_size = 0;

/***************************************************************************/

/* Initialize WASM-native file system directories */
int32_t mz_wasm_native_init_filesystem(void) {
    if (g_cache_initialized)
        return MZ_OK;
    
    /* Create virtual directories */
    EM_ASM({
        try {
            var dirs = [
                UTF8ToString($0), // archive-cache
                UTF8ToString($1), // archive-temp  
                UTF8ToString($2), // cdn-archives
                UTF8ToString($3)  // archive-packages
            ];
            
            dirs.forEach(function(dir) {
                if (!Module.FS.analyzePath(dir).exists) {
                    Module.FS.mkdir(dir);
                    console.log('Created WASM directory:', dir);
                }
            });
        } catch (e) {
            console.error('Failed to create WASM directories:', e);
        }
    }, MZ_WASM_ARCHIVE_CACHE, MZ_WASM_TEMP_DIR, MZ_WASM_CDN_DIR, MZ_WASM_PACKAGES_DIR);
    
    g_cache_initialized = 1;
    emscripten_console_log("minizip-ng: WASM-native filesystem initialized");
    return MZ_OK;
}

/* Mount IDBFS for persistent archive caching */
int32_t mz_wasm_native_mount_persistent_cache(void) {
    int32_t result;
    
    mz_wasm_native_init_filesystem();
    
    result = EM_ASM_INT({
        try {
            var cachePath = UTF8ToString($0);
            
            // Mount IDBFS for persistent storage
            Module.FS.mount(Module.FS.filesystems.IDBFS, {}, cachePath);
            
            // Synchronize with IndexedDB
            Module.FS.syncfs(true, function(err) {
                if (err) {
                    console.error('IDBFS sync failed:', err);
                } else {
                    console.log('IDBFS persistent cache mounted successfully');
                }
            });
            
            return 0; // MZ_OK
        } catch (e) {
            console.error('IDBFS mount failed:', e);
            return -1; // MZ_INTERNAL_ERROR
        }
    }, MZ_WASM_ARCHIVE_CACHE);
    
    return (result == 0) ? MZ_OK : MZ_INTERNAL_ERROR;
}

/* Sync cached archives to persistent storage */
int32_t mz_wasm_native_sync_cache(void) {
    EM_ASM({
        try {
            Module.FS.syncfs(false, function(err) {
                if (err) {
                    console.error('Cache sync failed:', err);
                } else {
                    console.log('Archive cache synchronized');
                }
            });
        } catch (e) {
            console.error('Cache sync error:', e);
        }
    });
    
    return MZ_OK;
}

/* Load archive from CDN with caching */
int32_t mz_wasm_native_load_from_cdn(const char *url, const char *cache_key, char *local_path, int32_t max_path) {
    char cached_path[PATH_MAX];
    char temp_path[PATH_MAX];
    struct stat file_stat;
    int32_t result;
    
    if (!url || !cache_key || !local_path)
        return MZ_PARAM_ERROR;
    
    mz_wasm_native_init_filesystem();
    
    /* Check if already cached */
    snprintf(cached_path, sizeof(cached_path), "%s/%s.zip", MZ_WASM_ARCHIVE_CACHE, cache_key);
    
    if (stat(cached_path, &file_stat) == 0) {
        /* Archive is cached, return cached path */
        strncpy(local_path, cached_path, max_path - 1);
        local_path[max_path - 1] = 0;
        emscripten_console_logf("minizip-ng: Using cached archive %s", cache_key);
        return MZ_OK;
    }
    
    /* Download from CDN */
    snprintf(temp_path, sizeof(temp_path), "%s/%s_temp.zip", MZ_WASM_TEMP_DIR, cache_key);
    
    result = EM_ASM_INT({
        var url = UTF8ToString($0);
        var tempPath = UTF8ToString($1);
        var cachedPath = UTF8ToString($2);
        
        return new Promise(function(resolve, reject) {
            console.log('Downloading archive from CDN:', url);
            
            fetch(url)
                .then(response => {
                    if (!response.ok) {
                        throw new Error('HTTP ' + response.status);
                    }
                    return response.arrayBuffer();
                })
                .then(buffer => {
                    try {
                        var data = new Uint8Array(buffer);
                        
                        // Save to temp file first
                        Module.FS.writeFile(tempPath, data);
                        
                        // Move to cache
                        Module.FS.rename(tempPath, cachedPath);
                        
                        console.log('Archive cached successfully:', cachedPath);
                        resolve(0); // MZ_OK
                    } catch (e) {
                        console.error('Failed to save archive:', e);
                        reject(-1); // MZ_INTERNAL_ERROR
                    }
                })
                .catch(error => {
                    console.error('CDN download failed:', error);
                    reject(-1); // MZ_INTERNAL_ERROR
                });
        }).then(result => result).catch(error => error);
    }, url, temp_path, cached_path);
    
    if (result == 0) {
        strncpy(local_path, cached_path, max_path - 1);
        local_path[max_path - 1] = 0;
        mz_wasm_native_sync_cache(); /* Persist to IndexedDB */
        return MZ_OK;
    }
    
    return MZ_INTERNAL_ERROR;
}

/* Load archive package (collection of related archives) */
int32_t mz_wasm_native_load_package(const char *package_url, const char *package_name) {
    char package_dir[PATH_MAX];
    char manifest_path[PATH_MAX];
    int32_t result;
    
    if (!package_url || !package_name)
        return MZ_PARAM_ERROR;
    
    mz_wasm_native_init_filesystem();
    
    snprintf(package_dir, sizeof(package_dir), "%s/%s", MZ_WASM_PACKAGES_DIR, package_name);
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", package_dir);
    
    /* Create package directory */
    EM_ASM({
        var packageDir = UTF8ToString($0);
        try {
            if (!Module.FS.analyzePath(packageDir).exists) {
                Module.FS.mkdir(packageDir);
            }
        } catch (e) {
            console.error('Failed to create package directory:', e);
        }
    }, package_dir);
    
    /* Download and extract package */
    result = EM_ASM_INT({
        var packageUrl = UTF8ToString($0);
        var packageDir = UTF8ToString($1);
        var manifestPath = UTF8ToString($2);
        
        return fetch(packageUrl)
            .then(response => response.arrayBuffer())
            .then(buffer => {
                try {
                    var data = new Uint8Array(buffer);
                    var tempZipPath = packageDir + '/package.zip';
                    
                    // Save package ZIP
                    Module.FS.writeFile(tempZipPath, data);
                    
                    // Note: Full ZIP extraction would require calling back to minizip-ng
                    // This is a placeholder for package extraction logic
                    console.log('Package downloaded:', packageDir);
                    
                    // Create simple manifest
                    var manifest = {
                        name: UTF8ToString($3),
                        downloaded: new Date().toISOString(),
                        archives: [] // Would be populated during extraction
                    };
                    
                    Module.FS.writeFile(manifestPath, JSON.stringify(manifest, null, 2));
                    return 0; // MZ_OK
                } catch (e) {
                    console.error('Package processing failed:', e);
                    return -1; // MZ_INTERNAL_ERROR
                }
            })
            .catch(error => {
                console.error('Package download failed:', error);
                return -1; // MZ_INTERNAL_ERROR
            });
    }, package_url, package_dir, manifest_path, package_name);
    
    return (result == 0) ? MZ_OK : MZ_INTERNAL_ERROR;
}

/* Progressive archive loading with callbacks */
typedef struct mz_wasm_progress_s {
    void *user_data;
    mz_wasm_progress_cb progress_cb;
    mz_wasm_complete_cb complete_cb;
    int64_t total_size;
    int64_t loaded_size;
} mz_wasm_progress;

int32_t mz_wasm_native_load_progressive(const char *url, const char *cache_key, 
                                       mz_wasm_progress_cb progress_cb, 
                                       mz_wasm_complete_cb complete_cb,
                                       void *user_data) {
    mz_wasm_progress *progress_ctx;
    
    if (!url || !cache_key)
        return MZ_PARAM_ERROR;
    
    progress_ctx = (mz_wasm_progress *)MZ_ALLOC(sizeof(mz_wasm_progress));
    if (!progress_ctx)
        return MZ_MEM_ERROR;
    
    progress_ctx->user_data = user_data;
    progress_ctx->progress_cb = progress_cb;
    progress_ctx->complete_cb = complete_cb;
    progress_ctx->total_size = 0;
    progress_ctx->loaded_size = 0;
    
    /* Start progressive download */
    EM_ASM({
        var url = UTF8ToString($0);
        var cacheKey = UTF8ToString($1);
        var progressPtr = $2;
        
        fetch(url)
            .then(response => {
                var totalSize = response.headers.get('Content-Length');
                if (totalSize) {
                    setValue(progressPtr + 16, parseInt(totalSize), 'i64'); // total_size offset
                }
                
                var reader = response.body.getReader();
                var chunks = [];
                var loadedSize = 0;
                
                function readChunk() {
                    return reader.read().then(function(result) {
                        if (result.done) {
                            // Complete - combine chunks and save
                            var totalLength = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
                            var combined = new Uint8Array(totalLength);
                            var offset = 0;
                            
                            chunks.forEach(chunk => {
                                combined.set(chunk, offset);
                                offset += chunk.length;
                            });
                            
                            var cachePath = '/archive-cache/' + cacheKey + '.zip';
                            Module.FS.writeFile(cachePath, combined);
                            
                            // Call completion callback
                            if (getValue(progressPtr + 8, '*')) { // complete_cb
                                Module.dynCall('vii', getValue(progressPtr + 8, '*'), [cachePath.length, cachePath]);
                            }
                            
                            return;
                        }
                        
                        chunks.push(result.value);
                        loadedSize += result.value.length;
                        setValue(progressPtr + 24, loadedSize, 'i64'); // loaded_size offset
                        
                        // Call progress callback
                        if (getValue(progressPtr + 0, '*')) { // progress_cb
                            var percent = totalSize ? Math.floor((loadedSize / totalSize) * 100) : 0;
                            Module.dynCall('vii', getValue(progressPtr + 0, '*'), [loadedSize, percent]);
                        }
                        
                        return readChunk();
                    });
                }
                
                return readChunk();
            })
            .catch(error => {
                console.error('Progressive download failed:', error);
            });
    }, url, cache_key, progress_ctx);
    
    return MZ_OK;
}

/* Cache management functions */
int32_t mz_wasm_native_set_cache_limit(int64_t max_size_bytes) {
    g_cache_max_size = max_size_bytes;
    return MZ_OK;
}

int64_t mz_wasm_native_get_cache_size(void) {
    /* Calculate current cache size */
    g_cache_current_size = EM_ASM_INT({
        try {
            var cacheDir = UTF8ToString($0);
            var totalSize = 0;
            
            function calculateSize(path) {
                try {
                    var stat = Module.FS.stat(path);
                    if (Module.FS.isFile(stat.mode)) {
                        return stat.size;
                    } else if (Module.FS.isDir(stat.mode)) {
                        var entries = Module.FS.readdir(path);
                        var size = 0;
                        entries.forEach(function(entry) {
                            if (entry !== '.' && entry !== '..') {
                                size += calculateSize(path + '/' + entry);
                            }
                        });
                        return size;
                    }
                } catch (e) {
                    return 0;
                }
                return 0;
            }
            
            return calculateSize(cacheDir);
        } catch (e) {
            return 0;
        }
    }, MZ_WASM_ARCHIVE_CACHE);
    
    return g_cache_current_size;
}

int32_t mz_wasm_native_clear_cache(void) {
    int32_t result;
    
    result = EM_ASM_INT({
        try {
            var cacheDir = UTF8ToString($0);
            
            function removeRecursive(path) {
                try {
                    var stat = Module.FS.stat(path);
                    if (Module.FS.isDir(stat.mode)) {
                        var entries = Module.FS.readdir(path);
                        entries.forEach(function(entry) {
                            if (entry !== '.' && entry !== '..') {
                                removeRecursive(path + '/' + entry);
                            }
                        });
                        Module.FS.rmdir(path);
                    } else {
                        Module.FS.unlink(path);
                    }
                } catch (e) {
                    console.warn('Failed to remove:', path, e);
                }
            }
            
            // Remove cache contents but keep directory
            var entries = Module.FS.readdir(cacheDir);
            entries.forEach(function(entry) {
                if (entry !== '.' && entry !== '..') {
                    removeRecursive(cacheDir + '/' + entry);
                }
            });
            
            console.log('Archive cache cleared');
            return 0; // MZ_OK
        } catch (e) {
            console.error('Cache clear failed:', e);
            return -1; // MZ_INTERNAL_ERROR
        }
    }, MZ_WASM_ARCHIVE_CACHE);
    
    g_cache_current_size = 0;
    mz_wasm_native_sync_cache();
    
    return (result == 0) ? MZ_OK : MZ_INTERNAL_ERROR;
}

/* Archive download to browser downloads folder */
int32_t mz_wasm_native_save_to_downloads(void *zip_handle, const char *filename) {
    void *mem_stream = NULL;
    void *buffer = NULL;
    int64_t buffer_size = 0;
    int32_t result = MZ_OK;
    
    if (!zip_handle || !filename)
        return MZ_PARAM_ERROR;
    
    /* Create memory stream to capture ZIP data */
    mz_stream_mem_create(&mem_stream);
    mz_stream_mem_open(mem_stream, NULL, MZ_OPEN_MODE_CREATE);
    
    /* This would require integration with ZIP writer to serialize to memory stream */
    /* For now, this is a placeholder showing the pattern */
    
    /* Get buffer from memory stream */
    mz_stream_mem_get_buffer(mem_stream, (const void **)&buffer);
    mz_stream_mem_get_buffer_length(mem_stream, &buffer_size);
    
    if (buffer && buffer_size > 0) {
        /* Trigger browser download */
        EM_ASM({
            var filename = UTF8ToString($0);
            var dataPtr = $1;
            var dataSize = $2;
            
            try {
                // Copy data from WASM memory
                var data = new Uint8Array(Module.HEAPU8.buffer, dataPtr, dataSize);
                var blob = new Blob([data], {type: 'application/zip'});
                
                // Create download link
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a');
                a.href = url;
                a.download = filename;
                document.body.appendChild(a);
                a.click();
                document.body.removeChild(a);
                URL.revokeObjectURL(url);
                
                console.log('Archive downloaded:', filename);
            } catch (e) {
                console.error('Download failed:', e);
            }
        }, filename, buffer, buffer_size);
    } else {
        result = MZ_INTERNAL_ERROR;
    }
    
    mz_stream_mem_close(mem_stream);
    mz_stream_mem_delete(&mem_stream);
    
    return result;
}

#endif /* __EMSCRIPTEN__ */