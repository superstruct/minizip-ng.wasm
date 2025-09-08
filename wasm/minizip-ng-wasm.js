/* minizip-ng-wasm.js -- High-level JavaScript wrapper for minizip-ng WASM
   Copyright (C) 2025 Superstruct Ltd, New Zealand
   Licensed under the same terms as minizip-ng
   
   This provides a modern, Promise-based JavaScript API for ZIP operations
   with WASM-native features like persistent caching and CDN loading
*/

class MinizipNGWasm {
    constructor(wasmModule) {
        this.Module = wasmModule;
        this.initialized = false;
        this.simdSupported = false;
        this.persistentCacheEnabled = false;
        
        // Bind C functions
        this._bindCFunctions();
    }
    
    _bindCFunctions() {
        const Module = this.Module;
        
        // Core ZIP functions
        this._mz_zip_reader_create = Module.cwrap('mz_zip_reader_create', 'number', []);
        this._mz_zip_reader_delete = Module.cwrap('mz_zip_reader_delete', null, ['number']);
        this._mz_zip_reader_open_buffer = Module.cwrap('mz_zip_reader_open_buffer', 'number', ['number', 'number', 'number']);
        this._mz_zip_reader_close = Module.cwrap('mz_zip_reader_close', 'number', ['number']);
        this._mz_zip_reader_get_num_entries = Module.cwrap('mz_zip_reader_get_num_entries', 'number', ['number']);
        this._mz_zip_reader_goto_first_entry = Module.cwrap('mz_zip_reader_goto_first_entry', 'number', ['number']);
        this._mz_zip_reader_goto_next_entry = Module.cwrap('mz_zip_reader_goto_next_entry', 'number', ['number']);
        this._mz_zip_reader_get_entry_info = Module.cwrap('mz_zip_reader_get_entry_info', 'number', ['number', 'number']);
        this._mz_zip_reader_extract_entry_to_mem = Module.cwrap('mz_zip_reader_extract_entry_to_mem', 'number', ['number', 'number', 'number', 'number']);
        
        // ZIP writer functions
        this._mz_zip_writer_create = Module.cwrap('mz_zip_writer_create', 'number', []);
        this._mz_zip_writer_delete = Module.cwrap('mz_zip_writer_delete', null, ['number']);
        this._mz_zip_writer_open_buffer = Module.cwrap('mz_zip_writer_open_buffer', 'number', ['number', 'number', 'number']);
        this._mz_zip_writer_close = Module.cwrap('mz_zip_writer_close', 'number', ['number']);
        this._mz_zip_writer_add_buffer = Module.cwrap('mz_zip_writer_add_buffer', 'number', ['number', 'string', 'number', 'number', 'number']);
        
        // WASM-native functions (if available)
        try {
            this._mz_wasm_native_init_filesystem = Module.cwrap('mz_wasm_native_init_filesystem', 'number', []);
            this._mz_wasm_native_mount_persistent_cache = Module.cwrap('mz_wasm_native_mount_persistent_cache', 'number', []);
            this._mz_wasm_native_load_from_cdn = Module.cwrap('mz_wasm_native_load_from_cdn', 'number', ['string', 'string', 'number', 'number']);
            this._mz_wasm_simd_supported = Module.cwrap('mz_wasm_simd_supported', 'number', []);
        } catch (e) {
            console.warn('WASM-native functions not available:', e);
        }
    }
    
    async initialize(options = {}) {
        if (this.initialized) {
            return;
        }
        
        const {
            persistentCache = true,
            simdOptimizations = true,
            maxCacheSize = 100 * 1024 * 1024 // 100MB
        } = options;
        
        try {
            // Initialize WASM SIMD
            if (simdOptimizations && this._mz_wasm_simd_supported) {
                this.simdSupported = this._mz_wasm_simd_supported() === 1;
                console.log(`WASM SIMD support: ${this.simdSupported ? 'enabled' : 'not available'}`);
            }
            
            // Initialize WASM-native filesystem
            if (this._mz_wasm_native_init_filesystem) {
                const result = this._mz_wasm_native_init_filesystem();
                if (result === 0) {
                    console.log('WASM-native filesystem initialized');
                    
                    // Mount persistent cache if requested
                    if (persistentCache && this._mz_wasm_native_mount_persistent_cache) {
                        const cacheResult = this._mz_wasm_native_mount_persistent_cache();
                        if (cacheResult === 0) {
                            this.persistentCacheEnabled = true;
                            console.log('Persistent cache enabled');
                        }
                    }
                }
            }
            
            this.initialized = true;
            console.log('minizip-ng WASM initialized successfully');
        } catch (error) {
            console.error('Failed to initialize minizip-ng WASM:', error);
            throw error;
        }
    }
    
    async loadFromBuffer(buffer) {
        if (!this.initialized) {
            await this.initialize();
        }
        
        // Allocate memory for buffer
        const bufferSize = buffer.byteLength;
        const bufferPtr = this.Module._malloc(bufferSize);
        
        try {
            // Copy buffer to WASM memory
            const heapBytes = new Uint8Array(this.Module.HEAPU8.buffer, bufferPtr, bufferSize);
            heapBytes.set(new Uint8Array(buffer));
            
            // Create ZIP reader
            const readerPtr = this._mz_zip_reader_create();
            if (!readerPtr) {
                throw new Error('Failed to create ZIP reader');
            }
            
            // Open buffer
            const result = this._mz_zip_reader_open_buffer(readerPtr, bufferPtr, bufferSize);
            if (result !== 0) {
                this._mz_zip_reader_delete(readerPtr);
                throw new Error(`Failed to open ZIP buffer (error: ${result})`);
            }
            
            return new ZipReader(this, readerPtr, bufferPtr, bufferSize);
        } catch (error) {
            this.Module._free(bufferPtr);
            throw error;
        }
    }
    
    async loadFromUrl(url, options = {}) {
        if (!this.initialized) {
            await this.initialize();
        }
        
        const {
            useCache = true,
            cacheKey = null,
            progressCallback = null
        } = options;
        
        try {
            let buffer;
            
            if (useCache && this.persistentCacheEnabled && this._mz_wasm_native_load_from_cdn) {
                // Use WASM-native CDN loading with caching
                const key = cacheKey || this._generateCacheKey(url);
                const pathPtr = this.Module._malloc(512);
                
                try {
                    const result = this._mz_wasm_native_load_from_cdn(url, key, pathPtr, 512);
                    if (result === 0) {
                        const localPath = this.Module.UTF8ToString(pathPtr);
                        const data = this.Module.FS.readFile(localPath);
                        buffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
                        console.log(`Loaded from cache: ${localPath}`);
                    }
                } finally {
                    this.Module._free(pathPtr);
                }
            }
            
            if (!buffer) {
                // Fallback to direct fetch
                console.log(`Downloading from URL: ${url}`);
                const response = await fetch(url);
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                }
                
                if (progressCallback && response.body) {
                    buffer = await this._fetchWithProgress(response, progressCallback);
                } else {
                    buffer = await response.arrayBuffer();
                }
            }
            
            return await this.loadFromBuffer(buffer);
        } catch (error) {
            console.error('Failed to load ZIP from URL:', error);
            throw error;
        }
    }
    
    async _fetchWithProgress(response, progressCallback) {
        const reader = response.body.getReader();
        const contentLength = parseInt(response.headers.get('Content-Length'), 10);
        
        let receivedLength = 0;
        let chunks = [];
        
        while (true) {
            const { done, value } = await reader.read();
            
            if (done) break;
            
            chunks.push(value);
            receivedLength += value.length;
            
            if (contentLength) {
                const progress = Math.round((receivedLength / contentLength) * 100);
                progressCallback(receivedLength, progress);
            }
        }
        
        // Combine chunks
        let position = 0;
        let buffer = new ArrayBuffer(receivedLength);
        let uint8Array = new Uint8Array(buffer);
        
        for (let chunk of chunks) {
            uint8Array.set(chunk, position);
            position += chunk.length;
        }
        
        return buffer;
    }
    
    createWriter() {
        if (!this.initialized) {
            throw new Error('minizip-ng not initialized');
        }
        
        const writerPtr = this._mz_zip_writer_create();
        if (!writerPtr) {
            throw new Error('Failed to create ZIP writer');
        }
        
        return new ZipWriter(this, writerPtr);
    }
    
    _generateCacheKey(url) {
        // Simple cache key generation based on URL
        return btoa(url).replace(/[^a-zA-Z0-9]/g, '').substring(0, 32);
    }
    
    getStats() {
        return {
            initialized: this.initialized,
            simdSupported: this.simdSupported,
            persistentCacheEnabled: this.persistentCacheEnabled,
            wasmMemoryUsed: this.Module.HEAP8.length
        };
    }
}

class ZipReader {
    constructor(wasm, readerPtr, bufferPtr, bufferSize) {
        this.wasm = wasm;
        this.readerPtr = readerPtr;
        this.bufferPtr = bufferPtr;
        this.bufferSize = bufferSize;
        this.closed = false;
        
        // Cache entry count
        this.entryCount = this.wasm._mz_zip_reader_get_num_entries(readerPtr);
    }
    
    getEntryCount() {
        return this.entryCount;
    }
    
    async *entries() {
        if (this.closed) {
            throw new Error('ZIP reader is closed');
        }
        
        const result = this.wasm._mz_zip_reader_goto_first_entry(this.readerPtr);
        if (result !== 0) {
            return;
        }
        
        do {
            const entryInfo = this._getCurrentEntryInfo();
            if (entryInfo) {
                yield entryInfo;
            }
        } while (this.wasm._mz_zip_reader_goto_next_entry(this.readerPtr) === 0);
    }
    
    _getCurrentEntryInfo() {
        // Allocate memory for entry info structure
        const infoSize = 256; // Approximate size of mz_zip_file structure
        const infoPtr = this.wasm.Module._malloc(infoSize);
        
        try {
            const result = this.wasm._mz_zip_reader_get_entry_info(this.readerPtr, infoPtr);
            if (result !== 0) {
                return null;
            }
            
            // Read entry information from memory
            // This is a simplified version - full implementation would parse the full structure
            const namePtr = this.wasm.Module.getValue(infoPtr, 'i32');
            const filename = this.wasm.Module.UTF8ToString(namePtr);
            
            return {
                filename: filename,
                // Additional properties would be extracted from the full structure
                size: 0,
                compressedSize: 0,
                isDirectory: filename.endsWith('/'),
                lastModified: new Date()
            };
        } finally {
            this.wasm.Module._free(infoPtr);
        }
    }
    
    async extractEntry(entryName) {
        if (this.closed) {
            throw new Error('ZIP reader is closed');
        }
        
        // Find entry by name
        let found = false;
        const result = this.wasm._mz_zip_reader_goto_first_entry(this.readerPtr);
        if (result !== 0) {
            throw new Error('No entries in ZIP');
        }
        
        do {
            const info = this._getCurrentEntryInfo();
            if (info && info.filename === entryName) {
                found = true;
                break;
            }
        } while (this.wasm._mz_zip_reader_goto_next_entry(this.readerPtr) === 0);
        
        if (!found) {
            throw new Error(`Entry not found: ${entryName}`);
        }
        
        // Extract entry to memory
        const maxSize = 10 * 1024 * 1024; // 10MB max per entry
        const outputPtr = this.wasm.Module._malloc(maxSize);
        const sizePtr = this.wasm.Module._malloc(4);
        
        try {
            this.wasm.Module.setValue(sizePtr, maxSize, 'i32');
            
            const extractResult = this.wasm._mz_zip_reader_extract_entry_to_mem(
                this.readerPtr, outputPtr, maxSize, sizePtr
            );
            
            if (extractResult !== 0) {
                throw new Error(`Failed to extract entry (error: ${extractResult})`);
            }
            
            const actualSize = this.wasm.Module.getValue(sizePtr, 'i32');
            const data = new Uint8Array(this.wasm.Module.HEAPU8.buffer, outputPtr, actualSize);
            
            // Copy data to new buffer
            const result = new ArrayBuffer(actualSize);
            new Uint8Array(result).set(data);
            
            return result;
        } finally {
            this.wasm.Module._free(outputPtr);
            this.wasm.Module._free(sizePtr);
        }
    }
    
    close() {
        if (!this.closed) {
            this.wasm._mz_zip_reader_close(this.readerPtr);
            this.wasm._mz_zip_reader_delete(this.readerPtr);
            this.wasm.Module._free(this.bufferPtr);
            this.closed = true;
        }
    }
}

class ZipWriter {
    constructor(wasm, writerPtr) {
        this.wasm = wasm;
        this.writerPtr = writerPtr;
        this.closed = false;
        this.bufferPtr = null;
        this.bufferSize = 1024 * 1024; // 1MB initial size
        
        // Initialize output buffer
        this._initBuffer();
    }
    
    _initBuffer() {
        this.bufferPtr = this.wasm.Module._malloc(this.bufferSize);
        const result = this.wasm._mz_zip_writer_open_buffer(
            this.writerPtr, this.bufferPtr, this.bufferSize
        );
        
        if (result !== 0) {
            this.wasm.Module._free(this.bufferPtr);
            throw new Error(`Failed to initialize ZIP writer buffer (error: ${result})`);
        }
    }
    
    addBuffer(filename, buffer, options = {}) {
        if (this.closed) {
            throw new Error('ZIP writer is closed');
        }
        
        const {
            compressionLevel = 6,
            lastModified = new Date()
        } = options;
        
        // Allocate memory for buffer
        const bufferSize = buffer.byteLength;
        const dataPtr = this.wasm.Module._malloc(bufferSize);
        
        try {
            // Copy buffer to WASM memory
            const heapBytes = new Uint8Array(this.wasm.Module.HEAPU8.buffer, dataPtr, bufferSize);
            heapBytes.set(new Uint8Array(buffer));
            
            // Add to ZIP
            const result = this.wasm._mz_zip_writer_add_buffer(
                this.writerPtr, filename, dataPtr, bufferSize, compressionLevel
            );
            
            if (result !== 0) {
                throw new Error(`Failed to add file to ZIP (error: ${result})`);
            }
        } finally {
            this.wasm.Module._free(dataPtr);
        }
    }
    
    async finalize() {
        if (this.closed) {
            throw new Error('ZIP writer is closed');
        }
        
        const result = this.wasm._mz_zip_writer_close(this.writerPtr);
        if (result !== 0) {
            throw new Error(`Failed to finalize ZIP (error: ${result})`);
        }
        
        // Get the actual written size and copy buffer
        // This would require additional C functions to get the final buffer size
        const finalBuffer = new ArrayBuffer(this.bufferSize);
        const sourceBytes = new Uint8Array(this.wasm.Module.HEAPU8.buffer, this.bufferPtr, this.bufferSize);
        new Uint8Array(finalBuffer).set(sourceBytes);
        
        this.closed = true;
        return finalBuffer;
    }
    
    async saveToDownloads(filename) {
        const buffer = await this.finalize();
        
        // Use browser download API
        const blob = new Blob([buffer], { type: 'application/zip' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }
    
    close() {
        if (!this.closed) {
            this.wasm._mz_zip_writer_delete(this.writerPtr);
            if (this.bufferPtr) {
                this.wasm.Module._free(this.bufferPtr);
            }
            this.closed = true;
        }
    }
}

// Export for use as ES6 module or global
if (typeof module !== 'undefined' && module.exports) {
    module.exports = MinizipNGWasm;
} else if (typeof window !== 'undefined') {
    window.MinizipNGWasm = MinizipNGWasm;
}

export default MinizipNGWasm;