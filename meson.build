project('minizip-ng.wasm', 'c', version: '4.0.0', meson_version: '>=1.3.0', default_options: ['buildtype=release'])

if get_option('simd')
  add_project_arguments('-msimd128', language: 'c')
endif

incs = [ include_directories('.') ]

core_sources = [
  'mz_os.c',
  'mz_strm.c',
  'mz_strm_mem.c',
  'mz_strm_buf.c',
  'mz_strm_pkcrypt.c',
  'mz_strm_wzaes.c',
  'mz_strm_split.c',
  'mz_zip.c',
  'mz_zip_rw.c',
  'mz_crypt.c',
  'mz_os_posix.c',
]

# Exclude optional backends requiring external libs or OS specifics
# (zlib, zstd, bzip2, lzma, openssl/win)

core = static_library('minizip_core', core_sources, include_directories: incs)

if get_option('main_module')
  executable('minizip-main', 'src/minizip_wasm_module.c',
    link_with: core,
    include_directories: incs,
    install: true,
    link_args: [
      '--no-entry',
      '-sMODULARIZE=1',
      '-sEXPORT_ES6=1',
      '-sALLOW_MEMORY_GROWTH=1',
      '-sENVIRONMENT=web,webview,worker',
      '-sNO_FILESYSTEM=1',
      '-sEXPORTED_RUNTIME_METHODS=["cwrap","ccall","UTF8ToString"]',
      '-sEXPORTED_FUNCTIONS=@' + meson.project_source_root() / get_option('exports'),
    ])
endif

if get_option('side_module')
  shared_module('minizip-side', 'src/minizip_wasm_side.c',
    link_whole: core,
    include_directories: incs,
    name_prefix: '',
    install: true,
    link_args: ['-sSIDE_MODULE=2', '-fPIC'])
endif

