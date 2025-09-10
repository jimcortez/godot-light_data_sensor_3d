#ifdef __APPLE__
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "../../batch_compute_manager.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/image.hpp>
#include <mutex>

using namespace godot;

// Global shared Metal resources for batch compute
static bool g_batch_metal_initialized = false;
static id<MTLDevice> g_batch_shared_device = nil;
static id<MTLCommandQueue> g_batch_shared_command_queue = nil;
static id<MTLComputePipelineState> g_batch_shared_compute_pipeline = nil;
static std::mutex g_batch_metal_init_mutex;

// Metal Resource Manager for Batch Compute
namespace BatchMetalResourceManager {
    bool createComputePipeline();
    
    bool initialize() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        
        if (g_batch_metal_initialized) {
            return true;
        }
        
        // Create shared Metal device
        g_batch_shared_device = MTLCreateSystemDefaultDevice();
        if (!g_batch_shared_device) {
            return false;
        }
        
        // Create shared command queue
        g_batch_shared_command_queue = [g_batch_shared_device newCommandQueue];
        if (!g_batch_shared_command_queue) {
            [g_batch_shared_device release];
            g_batch_shared_device = nil;
            return false;
        }
        
        // Create shared compute pipeline
        if (!BatchMetalResourceManager::createComputePipeline()) {
            [g_batch_shared_command_queue release];
            g_batch_shared_command_queue = nil;
            [g_batch_shared_device release];
            g_batch_shared_device = nil;
            return false;
        }
        
        g_batch_metal_initialized = true;
        return true;
    }
    
    bool isAvailable() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        return g_batch_metal_initialized && g_batch_shared_device != nil && g_batch_shared_command_queue != nil && g_batch_shared_compute_pipeline != nil;
    }
    
    id<MTLDevice> getDevice() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        return g_batch_shared_device;
    }
    
    id<MTLCommandQueue> getCommandQueue() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        return g_batch_shared_command_queue;
    }
    
    id<MTLComputePipelineState> getComputePipeline() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        return g_batch_shared_compute_pipeline;
    }
    
    id<MTLBuffer> createOutputBuffer(size_t size) {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        if (!g_batch_shared_device) {
            return nil;
        }
        return [g_batch_shared_device newBufferWithLength:size options:MTLResourceStorageModeShared];
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(g_batch_metal_init_mutex);
        
        if (g_batch_shared_compute_pipeline) {
            [g_batch_shared_compute_pipeline release];
            g_batch_shared_compute_pipeline = nil;
        }
        
        if (g_batch_shared_command_queue) {
            [g_batch_shared_command_queue release];
            g_batch_shared_command_queue = nil;
        }
        
        if (g_batch_shared_device) {
            [g_batch_shared_device release];
            g_batch_shared_device = nil;
        }
        
        g_batch_metal_initialized = false;
    }
    
    bool createComputePipeline() {
        if (!g_batch_shared_device) {
            return false;
        }
        
        // Build the actual viewport sampling compute pipeline
        NSString *src = @"#include <metal_stdlib>\n"
                         @"using namespace metal;\n"
                         @"kernel void simple_test(\n"
                         @"    device float4 *output [[buffer(0)]],\n"
                         @"    device float4 *sensor_regions [[buffer(1)]],\n"
                         @"    device uint *sensor_count [[buffer(2)]],\n"
                         @"    texture2d<float> viewport_texture [[texture(0)]],\n"
                         @"    uint3 gid [[thread_position_in_grid]]\n"
                         @") {\n"
                         @"    uint sensor_id = gid.x;\n"
                         @"    uint total_sensors = sensor_count[0];\n"
                         @"    \n"
                         @"    if (sensor_id >= total_sensors) {\n"
                         @"        return;\n"
                         @"    }\n"
                         @"    \n"
                         @"    // Sample the viewport texture at the sensor position\n"
                         @"    float4 sensor_region = sensor_regions[sensor_id];\n"
                         @"    float2 center = sensor_region.xy;\n"
                         @"    float radius = sensor_region.z;\n"
                         @"    \n"
                         @"    // Debug: Ensure we have valid coordinates\n"
                         @"    if (center.x < 0.0 || center.y < 0.0) {\n"
                         @"        output[sensor_id] = float4(1.0, 0.0, 0.0, 1.0); // Red for invalid coords\n"
                         @"        return;\n"
                         @"    }\n"
                         @"    \n"
                         @"    // Get texture dimensions\n"
                         @"    uint2 texture_size = uint2(viewport_texture.get_width(), viewport_texture.get_height());\n"
                         @"    \n"
                         @"    // Sample a region around the sensor position (like individual sensors do)\n"
                         @"    float3 acc = float3(0.0);\n"
                         @"    uint sample_count = 0;\n"
                         @"    \n"
                         @"    // Sample region around sensor position\n"
                         @"    for (int dy = -int(radius); dy <= int(radius); ++dy) {\n"
                         @"        for (int dx = -int(radius); dx <= int(radius); ++dx) {\n"
                         @"            float2 sample_pos = center + float2(dx, dy);\n"
                         @"            \n"
                         @"            // Convert screen pixel coordinates to texture UV coordinates (0-1 range)\n"
                         @"            // center contains screen pixel coordinates from camera->unproject_position()\n"
                         @"            float2 tex_coord = float2(sample_pos.x / float(texture_size.x), sample_pos.y / float(texture_size.y));\n"
                         @"            tex_coord = clamp(tex_coord, 0.0, 1.0);\n"
                         @"            \n"
                         @"            // Sample the texture with linear filtering\n"
                         @"            constexpr sampler texture_sampler(coord::normalized, address::clamp_to_edge, filter::linear);\n"
                         @"            float4 color = viewport_texture.sample(texture_sampler, tex_coord);\n"
                         @"            \n"
                         @"            // Accumulate color values\n"
                         @"            acc += color.rgb;\n"
                         @"            sample_count++;\n"
                         @"        }\n"
                         @"    }\n"
                         @"    \n"
                         @"    // Calculate average color\n"
                         @"    float3 avg_color = (sample_count > 0) ? (acc / float(sample_count)) : float3(0.0);\n"
                         @"    \n"
                         @"    // Write the result\n"
                         @"    output[sensor_id] = float4(avg_color, 1.0);\n"
                         @"}\n";

        NSError *error = nil;
        id<MTLLibrary> lib = [g_batch_shared_device newLibraryWithSource:src
                                                          options:nil
                                                            error:&error];
        if (!lib) {
            return false;
        }
        
        id<MTLFunction> fn = [lib newFunctionWithName:@"simple_test"];
        if (!fn) {
            return false;
        }
        
        g_batch_shared_compute_pipeline = [g_batch_shared_device newComputePipelineStateWithFunction:fn error:&error];
        if (!g_batch_shared_compute_pipeline) {
            return false;
        }
        
        return true;
    }
}

// Implementation of BatchComputeManager Metal methods
bool BatchComputeManager::_init_metal_device() {
    return BatchMetalResourceManager::initialize();
}

bool BatchComputeManager::_create_compute_pipelines() {
    return BatchMetalResourceManager::isAvailable();
}

bool BatchComputeManager::_create_buffers() {
    if (!BatchMetalResourceManager::isAvailable()) {
        return false;
    }
    
    id<MTLDevice> device = BatchMetalResourceManager::getDevice();
    if (!device) {
        return false;
    }
    
    // Create sensor regions buffer
    size_t regions_size = max_sensors * sizeof(SensorRegion);
    id<MTLBuffer> regions_buf = [device newBufferWithLength:regions_size options:MTLResourceStorageModeShared];
    if (!regions_buf) {
        return false;
    }
    sensor_regions_buffer = (void*)regions_buf;
    
    // Create output buffer
    size_t output_size = max_sensors * sizeof(float) * 4;
    id<MTLBuffer> output_buf = [device newBufferWithLength:output_size options:MTLResourceStorageModeShared];
    if (!output_buf) {
        [(id)sensor_regions_buffer release];
        sensor_regions_buffer = nullptr;
        return false;
    }
    output_buffer = (void*)output_buf;
    
    // Create sensor count buffer
    id<MTLBuffer> count_buf = [device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!count_buf) {
        [(id)sensor_regions_buffer release];
        [(id)output_buffer release];
        sensor_regions_buffer = nullptr;
        output_buffer = nullptr;
        return false;
    }
    sensor_count_buffer = (void*)count_buf;
    
    return true;
}

void BatchComputeManager::_cleanup_metal_resources() {
    if (sensor_count_buffer) {
        [(id)sensor_count_buffer release];
        sensor_count_buffer = nullptr;
    }
    
    if (output_buffer) {
        [(id)output_buffer release];
        output_buffer = nullptr;
    }
    
    if (sensor_regions_buffer) {
        [(id)sensor_regions_buffer release];
        sensor_regions_buffer = nullptr;
    }
    
    if (viewport_texture) {
        [(id)viewport_texture release];
        viewport_texture = nullptr;
    }
    
    if (batch_pipeline) {
        [(id)batch_pipeline release];
        batch_pipeline = nullptr;
    }
    
    if (optimized_pipeline) {
        [(id)optimized_pipeline release];
        optimized_pipeline = nullptr;
    }
    
    if (mtl_command_queue) {
        [(id)mtl_command_queue release];
        mtl_command_queue = nullptr;
    }
    
    if (mtl_device) {
        [(id)mtl_device release];
        mtl_device = nullptr;
    }
}

bool BatchComputeManager::_create_viewport_texture(Ref<ViewportTexture> viewport_texture) {
    if (viewport_texture.is_valid()) {
        id<MTLDevice> device = BatchMetalResourceManager::getDevice();
        if (!device) {
            return false;
        }
        
        // Get the image data from the ViewportTexture
        Ref<Image> img = viewport_texture->get_image();
        if (img.is_null()) {
            return false;
        }
        
        int width = img->get_width();
        int height = img->get_height();
        
        if (width <= 0 || height <= 0) {
            return false;
        }
        
        // Create Metal texture descriptor
        MTLTextureDescriptor* texture_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                 width:width
                                                                                                height:height
                                                                                             mipmapped:NO];
        texture_desc.usage = MTLTextureUsageShaderRead;
        
        id<MTLTexture> metal_texture = [device newTextureWithDescriptor:texture_desc];
        if (!metal_texture) {
            return false;
        }
        
        // Convert Godot Image to Metal texture data
        // Godot uses RGBA8 format, which matches MTLPixelFormatRGBA8Unorm
        uint8_t* pixel_data = (uint8_t*)malloc(width * height * 4);
        if (!pixel_data) {
            return false;
        }
        
        // Copy pixel data from Godot Image to our buffer
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Color pixel = img->get_pixel(x, y);
                int index = (y * width + x) * 4;
                pixel_data[index + 0] = (uint8_t)(pixel.r * 255.0f); // Red
                pixel_data[index + 1] = (uint8_t)(pixel.g * 255.0f); // Green
                pixel_data[index + 2] = (uint8_t)(pixel.b * 255.0f); // Blue
                pixel_data[index + 3] = (uint8_t)(pixel.a * 255.0f); // Alpha
            }
        }
        
        // Copy the image data to the Metal texture
        [metal_texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                          mipmapLevel:0
                            withBytes:pixel_data
                          bytesPerRow:width * 4]; // 4 bytes per pixel (RGBA)
        
        std::free(pixel_data);
        
        // Store the Metal texture
        if (this->viewport_texture) {
            [(id)this->viewport_texture release];
        }
        this->viewport_texture = (void*)metal_texture;
        
        return true;
    } else {
        return false;
    }
}

bool BatchComputeManager::_update_sensor_regions_buffer() {
    if (!sensor_regions_buffer || !sensor_count_buffer) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(data_mutex);
    
    if (sensor_regions.empty()) {
        // Still update the count to 0
        uint32_t count = 0;
        uint32_t* count_data = (uint32_t*)[(id)sensor_count_buffer contents];
        *count_data = count;
        return true;
    }
    
    // Copy sensor regions to Metal buffer
    SensorRegion* buffer_data = (SensorRegion*)[(id)sensor_regions_buffer contents];
    memcpy(buffer_data, sensor_regions.data(), sensor_regions.size() * sizeof(SensorRegion));
    
    // Update sensor count
    uint32_t count = static_cast<uint32_t>(sensor_regions.size());
    uint32_t* count_data = (uint32_t*)[(id)sensor_count_buffer contents];
    *count_data = count;
    
    
    return true;
}

bool BatchComputeManager::_dispatch_compute_kernel() {
    if (!BatchMetalResourceManager::isAvailable()) {
        return false;
    }
    
    id<MTLDevice> device = BatchMetalResourceManager::getDevice();
    id<MTLCommandQueue> queue = BatchMetalResourceManager::getCommandQueue();
    id<MTLComputePipelineState> pipeline = BatchMetalResourceManager::getComputePipeline();
    
    if (!device || !queue || !pipeline) {
        return false;
    }
    
    // Create command buffer
    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    if (!command_buffer) {
        return false;
    }
    
    // Create compute encoder
    id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
    if (!encoder) {
        return false;
    }
    
    // Set compute pipeline state
    [encoder setComputePipelineState:pipeline];
    
    // Check that all buffers exist
    if (!output_buffer || !sensor_regions_buffer || !sensor_count_buffer) {
        [encoder endEncoding];
        return false;
    }
    
    // Set buffers and texture
    [encoder setBuffer:(id)output_buffer offset:0 atIndex:0];
    [encoder setBuffer:(id)sensor_regions_buffer offset:0 atIndex:1];
    [encoder setBuffer:(id)sensor_count_buffer offset:0 atIndex:2];
    
    // Set viewport texture if available
    if (viewport_texture) {
        [encoder setTexture:(id)viewport_texture atIndex:0];
    }
    
    // Calculate threadgroup size
    std::lock_guard<std::mutex> lock(data_mutex);
    uint32_t sensor_count = static_cast<uint32_t>(sensor_regions.size());
    if (sensor_count == 0) {
        [encoder endEncoding];
        [command_buffer commit];
        return true;
    }
    
    // Dispatch compute
    MTLSize threadgroup_size = MTLSizeMake(1, 1, 1);
    MTLSize threadgroup_count = MTLSizeMake(sensor_count, 1, 1);
    [encoder dispatchThreadgroups:threadgroup_count threadsPerThreadgroup:threadgroup_size];
    
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    
    return true;
}

bool BatchComputeManager::_read_results() {
    if (!output_buffer) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(data_mutex);
    
    if (sensor_regions.empty()) {
        return true;
    }
    
    // Read results from Metal buffer
    float* buffer_data = (float*)[(id)output_buffer contents];
    if (!buffer_data) {
        return false;
    }
    
    size_t result_count = sensor_regions.size();
    sensor_results.resize(result_count);
    
    for (size_t i = 0; i < result_count; ++i) {
        float r = buffer_data[i * 4 + 0];
        float g = buffer_data[i * 4 + 1];
        float b = buffer_data[i * 4 + 2];
        float a = buffer_data[i * 4 + 3];
        
        sensor_results[i] = Color(r, g, b, a);
    }
    
    return true;
}

#endif // __APPLE__
