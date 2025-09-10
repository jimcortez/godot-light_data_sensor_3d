#ifdef __APPLE__
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "light_data_sensor_3d.h"

using namespace godot;

static id<MTLDevice> _mtlGetSystemDefaultDevice() {
    return MTLCreateSystemDefaultDevice();
}

void LightDataSensor3D::_init_metal_compute() {
    id<MTLDevice> device = _mtlGetSystemDefaultDevice();
    if (!device) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] No Metal device; falling back to CPU.");
        return;
    }
    mtl_device = (void *)[device retain];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to create command queue; fallback to CPU.");
        [(id)mtl_device release];
        mtl_device = nullptr;
        return;
    }
    mtl_command_queue = (void *)[queue retain];

    // Create a tiny output buffer for a single RGBA32F pixel
    const NSUInteger outSize = sizeof(float) * 4;
    id<MTLBuffer> outBuf = [device newBufferWithLength:outSize options:MTLResourceStorageModeShared];
    if (!outBuf) {
        [(id)mtl_command_queue release]; // release
        mtl_command_queue = nullptr;
        [(id)mtl_device release]; // release
        mtl_device = nullptr;
        return;
    }
    mtl_output_buffer = (void *)[outBuf retain];

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
    id<MTLLibrary> lib = [(id)mtl_device newLibraryWithSource:src
                                                            options:nil
                                                              error:&error];
    if (!lib) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to build Metal library; fallback to CPU.");
        return; // fallback will be CPU path
    }
    id<MTLFunction> fn = [lib newFunctionWithName:@"average_region"];
    if (!fn) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to find compute function; fallback to CPU.");
        return;
    }
    id<MTLComputePipelineState> pso = [(id)mtl_device newComputePipelineStateWithFunction:fn error:&error];
    if (!pso) {
        UtilityFunctions::print("[LightDataSensor3D][macOS] Failed to create compute pipeline; fallback to CPU.");
        return;
    }
    mtl_compute_pipeline = (void *)[pso retain];
    use_metal = true;
}

void LightDataSensor3D::_metal_readback_loop() {
    while (is_running) {
        std::vector<float> pixels;
        uint32_t count = 0;
        {
            std::unique_lock<std::mutex> lock(frame_mutex);
            frame_cv.wait(lock, [&]{ return frame_ready || !is_running; });
            if (!is_running) break;
            frame_ready = false;
            pixels = frame_rgba32f; // copy to avoid holding lock during GPU work
            count = (uint32_t)(pixels.size() / 4);
        }
        const size_t pixel_count = (size_t)count;
        if (pixel_count == 0) {
            continue;
        }
        // Copy into a temporary MTLBuffer
        id<MTLDevice> device = (id)mtl_device;
        id<MTLBuffer> outBuf = (id)mtl_output_buffer;
        if (!device || !outBuf || !mtl_compute_pipeline) {
            // fallback: average on CPU quickly
            double sr=0, sg=0, sb=0; size_t n=pixel_count; const float *p = pixels.data();
            for (size_t i=0;i<n;i++) { sr+=p[i*4+0]; sg+=p[i*4+1]; sb+=p[i*4+2]; }
            double inv = n>0 ? 1.0/double(n) : 0.0; last_color = Color(sr*inv, sg*inv, sb*inv, 1.0);
            has_new_color = true;
            continue;
        }
        id<MTLBuffer> inBuf = [device newBufferWithBytes:pixels.data()
                                                   length:pixels.size()*sizeof(float)
                                                  options:MTLResourceStorageModeShared];
        id<MTLBuffer> countBuf = [device newBufferWithBytes:&count length:sizeof(uint32_t) options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> queue = (id)mtl_command_queue;
        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLComputePipelineState> pso = (id)mtl_compute_pipeline;
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:pso];
        [enc setBuffer:outBuf offset:0 atIndex:0];
        [enc setBuffer:inBuf offset:0 atIndex:1];
        [enc setBuffer:countBuf offset:0 atIndex:2];
        MTLSize threadsPerGrid = MTLSizeMake(1,1,1);
        MTLSize threadsPerThreadgroup = MTLSizeMake(1,1,1);
        [enc dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [enc endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];
        float *ptr = (float *)[outBuf contents];
        if (ptr) {
            last_color = Color(ptr[0], ptr[1], ptr[2], ptr[3]);
            has_new_color = true;
        }
    }
}

Color LightDataSensor3D::_read_pixel_from_mtl_buffer() {
    id<MTLBuffer> outBuf = (id)mtl_output_buffer;
    if (!outBuf) {
        return Color(0, 0, 0, 1);
    }
    float *ptr = (float *)[outBuf contents];
    if (!ptr) {
        return Color(0, 0, 0, 1);
    }
    return Color(ptr[0], ptr[1], ptr[2], ptr[3]);
}

void LightDataSensor3D::_cleanup_metal_objects() {
    if (mtl_output_buffer) {
        [(id)mtl_output_buffer release];
        mtl_output_buffer = nullptr;
    }
    if (mtl_compute_pipeline) {
        [(id)mtl_compute_pipeline release];
        mtl_compute_pipeline = nullptr;
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

#endif // __APPLE__


