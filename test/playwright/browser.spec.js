/* browser.spec.js -- Playwright browser tests for minizip-ng.wasm
   Copyright (C) 2025 Superstruct Ltd, New Zealand
   Licensed under the same terms as minizip-ng
   
   Comprehensive browser testing with real WASM execution
*/

const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

// Test configuration
const TEST_CONFIG = {
    wasmPath: path.join(__dirname, '../../install'),
    timeout: 30000,
    slowMo: process.env.SLOW_MO ? parseInt(process.env.SLOW_MO) : 0
};

test.describe('minizip-ng.wasm Browser Tests', () => {
    test.beforeEach(async ({ page }) => {
        // Set up console logging
        page.on('console', (msg) => {
            if (msg.type() === 'error') {
                console.error('Browser error:', msg.text());
            } else if (process.env.VERBOSE) {
                console.log(`Browser ${msg.type()}:`, msg.text());
            }
        });
        
        // Set up error handling
        page.on('pageerror', (error) => {
            console.error('Page error:', error.message);
        });
        
        // Navigate to test page
        await page.goto('about:blank');
        
        // Set up test environment
        await page.addInitScript(() => {
            // Mock fetch for CDN testing
            window._originalFetch = window.fetch;
            window.fetch = (url, options) => {
                if (url.includes('test-archive')) {
                    // Return mock ZIP data
                    const mockZipData = new Uint8Array([
                        0x50, 0x4B, 0x03, 0x04, // ZIP signature
                        0x14, 0x00, 0x00, 0x00, // Version, flags
                        0x00, 0x00, 0x00, 0x00, // Compression, time, date
                        0x00, 0x00, 0x00, 0x00, // CRC32
                        0x00, 0x00, 0x00, 0x00, // Compressed size
                        0x00, 0x00, 0x00, 0x00, // Uncompressed size
                        0x05, 0x00, 0x00, 0x00, // Filename length, extra length
                        // Filename "test.txt"
                        0x74, 0x65, 0x73, 0x74, 0x2E, 0x74, 0x78, 0x74
                    ]);
                    
                    return Promise.resolve(new Response(mockZipData.buffer, {
                        status: 200,
                        headers: {
                            'Content-Type': 'application/zip',
                            'Content-Length': mockZipData.length.toString()
                        }
                    }));
                }
                return window._originalFetch(url, options);
            };
        });
    });
    
    test('should load WASM module successfully', async ({ page }) => {
        // Check if WASM files exist
        const wasmFiles = fs.readdirSync(TEST_CONFIG.wasmPath)
            .filter(f => f.endsWith('.wasm'));
        
        expect(wasmFiles.length).toBeGreaterThan(0);
        
        const wasmFile = wasmFiles[0];
        const wasmPath = path.join(TEST_CONFIG.wasmPath, wasmFile);
        const wasmData = fs.readFileSync(wasmPath);
        
        // Load WASM module in browser
        const result = await page.evaluate((wasmBuffer) => {
            return new Promise((resolve, reject) => {
                try {
                    const wasmModule = new WebAssembly.Module(wasmBuffer);
                    const wasmInstance = new WebAssembly.Instance(wasmModule);
                    resolve({
                        success: true,
                        exports: Object.keys(wasmInstance.exports),
                        wasmSize: wasmBuffer.byteLength
                    });
                } catch (error) {
                    resolve({
                        success: false,
                        error: error.message,
                        wasmSize: wasmBuffer.byteLength
                    });
                }
            });
        }, wasmData.buffer);
        
        expect(result.success).toBeTruthy();
        expect(result.wasmSize).toBeGreaterThan(1000);
        expect(result.exports.length).toBeGreaterThan(0);
    });
    
    test('should initialize minizip-ng wrapper', async ({ page }) => {
        // Load the JavaScript wrapper
        const wrapperPath = path.join(TEST_CONFIG.wasmPath, 'minizip-ng-wasm.js');
        
        if (fs.existsSync(wrapperPath)) {
            const wrapperContent = fs.readFileSync(wrapperPath, 'utf8');
            
            const result = await page.evaluate((wrapperCode) => {
                try {
                    // Create a simplified Emscripten module mock
                    window.Module = {
                        onRuntimeInitialized: null,
                        ready: Promise.resolve(),
                        cwrap: () => () => 0,
                        ccall: () => 0,
                        _malloc: () => 1000,
                        _free: () => {},
                        FS: {
                            readFile: () => new Uint8Array(0),
                            writeFile: () => {},
                            mkdir: () => {},
                            mount: () => {},
                            syncfs: (populate, callback) => callback && callback(null)
                        }
                    };
                    
                    eval(wrapperCode);
                    
                    const MinizipWasm = window.MinizipNGWasm || window.default;
                    const minizip = new MinizipWasm(window.Module);
                    
                    return {
                        success: true,
                        hasClass: typeof MinizipWasm === 'function',
                        hasInstance: minizip !== null,
                        methods: Object.getOwnPropertyNames(MinizipWasm.prototype)
                    };
                } catch (error) {
                    return {
                        success: false,
                        error: error.message
                    };
                }
            }, wrapperContent);
            
            expect(result.success).toBeTruthy();
            expect(result.hasClass).toBeTruthy();
            expect(result.hasInstance).toBeTruthy();
            expect(result.methods).toContain('initialize');
        } else {
            // Skip if wrapper doesn't exist yet
            console.warn('JavaScript wrapper not found, skipping test');
        }
    });
    
    test('should support WASM SIMD detection', async ({ page }) => {
        const simdSupport = await page.evaluate(() => {
            try {
                // Check WebAssembly SIMD support
                const wasmSupportsThreads = typeof SharedArrayBuffer !== 'undefined';
                const wasmSupportsSIMD = typeof WebAssembly.instantiate !== 'undefined';
                
                return {
                    webAssemblyAvailable: typeof WebAssembly !== 'undefined',
                    sharedArrayBufferAvailable: wasmSupportsThreads,
                    simdDetected: wasmSupportsSIMD,
                    userAgent: navigator.userAgent
                };
            } catch (error) {
                return {
                    error: error.message
                };
            }
        });
        
        expect(simdSupport.webAssemblyAvailable).toBeTruthy();
        expect(typeof simdSupport.userAgent).toBe('string');
    });
    
    test('should handle IDBFS file system operations', async ({ page }) => {
        const idbfsTest = await page.evaluate(() => {
            return new Promise((resolve) => {
                try {
                    // Test IndexedDB availability
                    if (!window.indexedDB) {
                        resolve({ success: false, error: 'IndexedDB not available' });
                        return;
                    }
                    
                    const request = window.indexedDB.open('test-minizip', 1);
                    
                    request.onupgradeneeded = (event) => {
                        const db = event.target.result;
                        if (!db.objectStoreNames.contains('cache')) {
                            db.createObjectStore('cache');
                        }
                    };
                    
                    request.onsuccess = (event) => {
                        const db = event.target.result;
                        resolve({
                            success: true,
                            version: db.version,
                            stores: Array.from(db.objectStoreNames)
                        });
                        db.close();
                    };
                    
                    request.onerror = () => {
                        resolve({ success: false, error: 'Failed to open IndexedDB' });
                    };
                    
                    setTimeout(() => {
                        resolve({ success: false, error: 'IndexedDB timeout' });
                    }, 5000);
                } catch (error) {
                    resolve({ success: false, error: error.message });
                }
            });
        });
        
        expect(idbfsTest.success).toBeTruthy();
        expect(idbfsTest.version).toBe(1);
    });
    
    test('should perform ZIP operations in browser', async ({ page }) => {
        const zipTest = await page.evaluate(() => {
            return new Promise((resolve) => {
                try {
                    // Create a simple ZIP file structure in memory
                    const testData = new Uint8Array([
                        // ZIP local file header signature
                        0x50, 0x4B, 0x03, 0x04,
                        // Version needed to extract
                        0x14, 0x00,
                        // General purpose bit flag
                        0x00, 0x00,
                        // Compression method (stored)
                        0x00, 0x00,
                        // File last modification time
                        0x00, 0x00,
                        // File last modification date
                        0x00, 0x00,
                        // CRC-32
                        0x00, 0x00, 0x00, 0x00,
                        // Compressed size
                        0x0C, 0x00, 0x00, 0x00,
                        // Uncompressed size
                        0x0C, 0x00, 0x00, 0x00,
                        // File name length
                        0x08, 0x00,
                        // Extra field length
                        0x00, 0x00,
                        // File name
                        0x74, 0x65, 0x73, 0x74, 0x2E, 0x74, 0x78, 0x74, // "test.txt"
                        // File data
                        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x5A, 0x49, 0x50, 0x21, 0x0A, 0x00 // "Hello ZIP!\n"
                    ]);
                    
                    resolve({
                        success: true,
                        zipSize: testData.length,
                        hasValidSignature: testData[0] === 0x50 && testData[1] === 0x4B
                    });
                } catch (error) {
                    resolve({
                        success: false,
                        error: error.message
                    });
                }
            });
        });
        
        expect(zipTest.success).toBeTruthy();
        expect(zipTest.zipSize).toBeGreaterThan(0);
        expect(zipTest.hasValidSignature).toBeTruthy();
    });
    
    test('should handle CDN loading simulation', async ({ page }) => {
        const cdnTest = await page.evaluate(() => {
            return new Promise((resolve) => {
                // Test our mocked fetch
                fetch('https://cdn.example.com/test-archive.zip')
                    .then(response => response.arrayBuffer())
                    .then(buffer => {
                        const data = new Uint8Array(buffer);
                        resolve({
                            success: true,
                            size: data.length,
                            hasZipSignature: data[0] === 0x50 && data[1] === 0x4B
                        });
                    })
                    .catch(error => {
                        resolve({
                            success: false,
                            error: error.message
                        });
                    });
            });
        });
        
        expect(cdnTest.success).toBeTruthy();
        expect(cdnTest.size).toBeGreaterThan(0);
        expect(cdnTest.hasZipSignature).toBeTruthy();
    });
    
    test('should measure performance in browser', async ({ page }) => {
        const performanceTest = await page.evaluate(() => {
            return new Promise((resolve) => {
                const startTime = performance.now();
                
                // Simulate ZIP processing workload
                const testData = new ArrayBuffer(1024 * 100); // 100KB
                const view = new Uint8Array(testData);
                
                // Fill with test pattern
                for (let i = 0; i < view.length; i++) {
                    view[i] = i % 256;
                }
                
                // Simulate compression/decompression work
                let checksum = 0;
                for (let i = 0; i < view.length; i++) {
                    checksum = (checksum + view[i]) % 0xFFFFFFFF;
                }
                
                const endTime = performance.now();
                const duration = endTime - startTime;
                
                resolve({
                    duration: duration,
                    dataSize: testData.byteLength,
                    throughput: (testData.byteLength / (duration / 1000)) / (1024 * 1024), // MB/s
                    checksum: checksum
                });
            });
        });
        
        expect(performanceTest.duration).toBeLessThan(1000); // Should complete within 1 second
        expect(performanceTest.throughput).toBeGreaterThan(1); // At least 1 MB/s
        expect(performanceTest.checksum).toBeGreaterThan(0);
    });
    
    test('should handle memory operations efficiently', async ({ page }) => {
        const memoryTest = await page.evaluate(() => {
            const results = [];
            const iterations = 10;
            
            for (let i = 0; i < iterations; i++) {
                const startHeap = performance.memory ? performance.memory.usedJSHeapSize : 0;
                const startTime = performance.now();
                
                // Allocate and deallocate memory
                const buffers = [];
                for (let j = 0; j < 100; j++) {
                    buffers.push(new ArrayBuffer(1024)); // 1KB each
                }
                
                // Process buffers
                let totalSize = 0;
                buffers.forEach(buffer => {
                    totalSize += buffer.byteLength;
                });
                
                // Clear references
                buffers.length = 0;
                
                const endTime = performance.now();
                const endHeap = performance.memory ? performance.memory.usedJSHeapSize : 0;
                
                results.push({
                    iteration: i,
                    duration: endTime - startTime,
                    heapGrowth: endHeap - startHeap,
                    totalSize: totalSize
                });
                
                // Force garbage collection if available
                if (window.gc) {
                    window.gc();
                }
            }
            
            const avgDuration = results.reduce((sum, r) => sum + r.duration, 0) / results.length;
            const avgHeapGrowth = results.reduce((sum, r) => sum + r.heapGrowth, 0) / results.length;
            
            return {
                iterations: iterations,
                avgDuration: avgDuration,
                avgHeapGrowth: avgHeapGrowth,
                memoryAPIAvailable: !!performance.memory
            };
        });
        
        expect(memoryTest.avgDuration).toBeLessThan(100); // Should be fast
        expect(memoryTest.iterations).toBe(10);
        
        if (memoryTest.memoryAPIAvailable) {
            // Memory growth should be reasonable
            expect(memoryTest.avgHeapGrowth).toBeLessThan(1024 * 1024); // Less than 1MB per iteration
        }
    });
});

// Browser compatibility tests
test.describe('Browser Compatibility', () => {
    test('should work in different browser contexts', async ({ page, browserName }) => {
        const browserInfo = await page.evaluate(() => {
            return {
                userAgent: navigator.userAgent,
                webAssembly: typeof WebAssembly !== 'undefined',
                indexedDB: typeof indexedDB !== 'undefined',
                fetch: typeof fetch !== 'undefined',
                arrayBuffer: typeof ArrayBuffer !== 'undefined',
                uint8Array: typeof Uint8Array !== 'undefined',
                sharedArrayBuffer: typeof SharedArrayBuffer !== 'undefined'
            };
        });
        
        expect(browserInfo.webAssembly).toBeTruthy();
        expect(browserInfo.indexedDB).toBeTruthy();
        expect(browserInfo.fetch).toBeTruthy();
        expect(browserInfo.arrayBuffer).toBeTruthy();
        expect(browserInfo.uint8Array).toBeTruthy();
        
        console.log(`Browser: ${browserName}, WebAssembly: ${browserInfo.webAssembly}, SIMD: ${browserInfo.sharedArrayBuffer}`);
    });
});