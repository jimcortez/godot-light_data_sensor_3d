#ifdef __APPLE__
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "light_data_sensor_3d.h"
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

using namespace godot;

// Global shared Metal resources
static bool g_metal_initialized = false;
static id<MTLDevice> g_shared_device = nil;
static id<MTLCommandQueue> g_shared_command_queue = nil;
static id<MTLComputePipelineState> g_shared_compute_pipeline = nil;

// Metal Resource Manager - Simple functions for shared Metal resources
namespace MetalResourceManager {
    bool createComputePipeline();
    
    bool initialize() {
        // Simple check - Metal initialization is thread-safe
        if (g_metal_initialized) {
            return true;
        }
        
        // Create shared Metal device
        g_shared_device = MTLCreateSystemDefaultDevice();
        if (!g_shared_device) {
            UtilityFunctions::print("[MetalResourceManager] No Metal device available; falling back to CPU.");
            return false;
        }
        
        // Create shared command queue
        g_shared_command_queue = [g_shared_device newCommandQueue];
        if (!g_shared_command_queue) {
            UtilityFunctions::print("[MetalResourceManager] Failed to create shared command queue; falling back to CPU.");
            g_shared_device = nil;
            return false;
        }
        
        // Create shared compute pipeline
        if (!MetalResourceManager::createComputePipeline()) {
            UtilityFunctions::print("[MetalResourceManager] Failed to create shared compute pipeline; falling back to CPU.");
            [g_shared_command_queue release];
            g_shared_command_queue = nil;
            [g_shared_device release];
            g_shared_device = nil;
            return false;
        }
        
        g_metal_initialized = true;
        UtilityFunctions::print("[MetalResourceManager] Successfully initialized shared Metal resources.");
        return true;
    }
    
    bool isAvailable() {
        return g_metal_initialized && g_shared_device != nil && g_shared_command_queue != nil && g_shared_compute_pipeline != nil;
    }
    
    id<MTLDevice> getDevice() {
        return g_shared_device;
    }
    
    id<MTLCommandQueue> getCommandQueue() {
        return g_shared_command_queue;
    }
    
    id<MTLComputePipelineState> getComputePipeline() {
        return g_shared_compute_pipeline;
    }
    
    id<MTLBuffer> createOutputBuffer() {
        if (!g_shared_device) {
            return nil;
        }
        const NSUInteger outSize = sizeof(float) * 4;
        return [g_shared_device newBufferWithLength:outSize options:MTLResourceStorageModeShared];
    }
    
    void shutdown() {
        if (g_shared_compute_pipeline) {
            [g_shared_compute_pipeline release];
            g_shared_compute_pipeline = nil;
        }
        
        if (g_shared_command_queue) {
            [g_shared_command_queue release];
            g_shared_command_queue = nil;
        }
        
        if (g_shared_device) {
            [g_shared_device release];
            g_shared_device = nil;
        }
        
        g_metal_initialized = false;
    }
    
    bool createComputePipeline() {
        if (!g_shared_device) {
            return false;
        }
        
        // Build a simple compute pipeline that averages a small region passed via buffer(1) and writes to buffer(0).
        NSString *src = @"#include <metal_stdlib>\n"
                         @"using namespace metal;\n"
                         @"kernel void average_region(device float4 *outPixel [[buffer(0)]],\n"
                         @"                                   device float4 *inPixels [[buffer(1)]],\n"
                         @"                                   constant uint &count [[buffer(2)]],\n"
                         @"                                   uint3 gid [[thread_position_in_grid]]) {\n"
                         @"    if (gid.x == 0 && gid.y == 0) {\n"
                         @"        float3 acc = float3(0.0);\n"
                         @"        uint n = count;\n"
                         @"        for (uint i = 0; i < n; ++i) {\n"
                         @"            float4 c = inPixels[i];\n"
                         @"            acc += c.rgb;\n"
                         @"        }\n"
                         @"        float inv = (n > 0) ? (1.0 / float(n)) : 0.0;\n"
                         @"        float3 avg = acc * inv;\n"
                         @"        outPixel[0] = float4(avg, 1.0);\n"
                         @"    }\n"
                         @"}\n";

        NSError *error = nil;
        id<MTLLibrary> lib = [g_shared_device newLibraryWithSource:src
                                                          options:nil
                                                            error:&error];
        if (!lib) {
            return false;
        }
        
        id<MTLFunction> fn = [lib newFunctionWithName:@"average_region"];
        if (!fn) {
            return false;
        }
        
        g_shared_compute_pipeline = [g_shared_device newComputePipelineStateWithFunction:fn error:&error];
        if (!g_shared_compute_pipeline) {
            return false;
        }
        
        return true;
    }
}

static id<MTLDevice> _mtlGetSystemDefaultDevice() {
    return MTLCreateSystemDefaultDevice();
}

void LightDataSensor3D::_init_metal_compute() {
    // Initialize shared Metal resources
    if (!MetalResourceManager::initialize()) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to initialize shared Metal resources; falling back to CPU.");
        return;
    }
    
    // Create individual output buffer for this sensor
    mtl_output_buffer = (void *)MetalResourceManager::createOutputBuffer();
    if (!mtl_output_buffer) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to create output buffer; falling back to CPU.");
        return;
    }
    
    use_metal = true;
    UtilityFunctions::print("[LightDataSensor3D][macOS] Successfully initialized with shared Metal resources.");
}

void LightDataSensor3D::_cleanup_metal_objects() {
    // Only clean up individual sensor resources
    if (mtl_output_buffer) {
        [(id)mtl_output_buffer release];
        mtl_output_buffer = nullptr;
    }
    // Shared resources are managed by MetalResourceManager
}

void LightDataSensor3D::_metal_readback_loop() {
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        
        if (!use_metal || !MetalResourceManager::isAvailable()) {
            continue;
        }
        
        // Get shared Metal resources
        id<MTLDevice> device = MetalResourceManager::getDevice();
        id<MTLCommandQueue> queue = MetalResourceManager::getCommandQueue();
        id<MTLComputePipelineState> pipeline = MetalResourceManager::getComputePipeline();
        id<MTLBuffer> outBuf = (id)mtl_output_buffer;
        
        if (!device || !queue || !pipeline || !outBuf) {
            continue;
        }
        
        // Get the captured pixel data from the main thread
        std::vector<float> pixels;
        uint32_t pixel_count = 0;
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (frame_rgba32f.empty()) {
                continue; // No data available yet
            }
            pixels = frame_rgba32f;
            pixel_count = static_cast<uint32_t>(pixels.size() / 4);
        }
        
        if (pixel_count == 0) {
            continue;
        }
        
        // Create command buffer
        id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
        if (!cmdBuf) {
            continue;
        }
        
        // Create compute encoder
        id<MTLComputeCommandEncoder> encoder = [cmdBuf computeCommandEncoder];
        if (!encoder) {
            continue;
        }
        
        // Set compute pipeline state
        [encoder setComputePipelineState:pipeline];
        
        // Set output buffer
        [encoder setBuffer:outBuf offset:0 atIndex:0];
        
        // Create input buffer with actual pixel data
        const NSUInteger inputSize = pixels.size() * sizeof(float);
        id<MTLBuffer> inBuf = [device newBufferWithBytes:pixels.data() length:inputSize options:MTLResourceStorageModeShared];
        if (inBuf) {
            [encoder setBuffer:inBuf offset:0 atIndex:1];
            
            // Set count buffer
            id<MTLBuffer> countBuf = [device newBufferWithBytes:&pixel_count length:sizeof(pixel_count) options:MTLResourceStorageModeShared];
            [encoder setBuffer:countBuf offset:0 atIndex:2];
            
            // Dispatch compute
            [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            
            [inBuf release];
            [countBuf release];
        }
        
        [encoder endEncoding];
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];
        
        // Read result
        float *result = (float *)[outBuf contents];
        if (result) {
            current_color = Color(result[0], result[1], result[2], result[3]);
            current_light_level = 0.299f * result[0] + 0.587f * result[1] + 0.114f * result[2];
            has_new_readings = true;
        }
    }
}

#endif // __APPLE__