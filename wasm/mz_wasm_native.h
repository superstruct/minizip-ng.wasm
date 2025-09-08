/* mz_wasm_native.h -- WASM-native features header
   Copyright (C) 2025 Superstruct Ltd, New Zealand  
   Licensed under the same terms as minizip-ng
*/

#ifndef MZ_WASM_NATIVE_H
#define MZ_WASM_NATIVE_H

#ifdef __EMSCRIPTEN__

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

/* WASM-native callback types */
typedef void (*mz_wasm_progress_cb)(int64_t loaded_bytes, int32_t percent_complete);
typedef void (*mz_wasm_complete_cb)(const char *local_path);

/***************************************************************************/

/* Filesystem initialization */
int32_t mz_wasm_native_init_filesystem(void);
int32_t mz_wasm_native_mount_persistent_cache(void);
int32_t mz_wasm_native_sync_cache(void);

/* CDN and async loading */
int32_t mz_wasm_native_load_from_cdn(const char *url, const char *cache_key, 
                                    char *local_path, int32_t max_path);
int32_t mz_wasm_native_load_package(const char *package_url, const char *package_name);
int32_t mz_wasm_native_load_progressive(const char *url, const char *cache_key,
                                       mz_wasm_progress_cb progress_cb,
                                       mz_wasm_complete_cb complete_cb,
                                       void *user_data);

/* Cache management */
int32_t mz_wasm_native_set_cache_limit(int64_t max_size_bytes);
int64_t mz_wasm_native_get_cache_size(void);
int32_t mz_wasm_native_clear_cache(void);

/* Browser integration */
int32_t mz_wasm_native_save_to_downloads(void *zip_handle, const char *filename);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* __EMSCRIPTEN__ */

#endif /* MZ_WASM_NATIVE_H */