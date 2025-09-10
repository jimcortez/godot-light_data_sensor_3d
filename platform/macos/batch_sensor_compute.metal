#include <metal_stdlib>
using namespace metal;

// Structure to define sensor sampling regions
struct SensorRegion {
    float center_x;
    float center_y;
    int radius;
    int sensor_id;
};

// Sampler for texture sampling
constexpr sampler texture_sampler(coord::pixel, address::clamp_to_edge, filter::linear);

// Main compute kernel for batched sensor processing
kernel void batch_sensor_average(
    device float4 *output [[buffer(0)]],                    // Output buffer for all sensor results
    texture2d<float> viewport_texture [[texture(0)]],       // Input viewport texture
    constant SensorRegion *regions [[buffer(1)]],           // Array of sensor regions
    constant uint &sensor_count [[buffer(2)]],              // Number of sensors to process
    uint3 gid [[thread_position_in_grid]]                   // Thread ID
) {
    uint sensor_id = gid.x;
    
    // Bounds check
    if (sensor_id >= sensor_count) {
        return;
    }
    
    SensorRegion region = regions[sensor_id];
    
    // Initialize accumulator
    float3 acc = float3(0.0);
    uint sample_count = 0;
    
    // Sample region around sensor position
    for (int dy = -region.radius; dy <= region.radius; ++dy) {
        for (int dx = -region.radius; dx <= region.radius; ++dx) {
            float2 sample_pos = float2(region.center_x + dx, region.center_y + dy);
            
            // Sample texture at position
            float4 color = viewport_texture.sample(texture_sampler, sample_pos);
            
            // Accumulate color values
            acc += color.rgb;
            sample_count++;
        }
    }
    
    // Calculate average color
    float3 avg_color = (sample_count > 0) ? (acc / float(sample_count)) : float3(0.0);
    
    // Store result
    output[sensor_id] = float4(avg_color, 1.0);
}

// Alternative kernel for processing multiple sensors per thread (for very high sensor counts)
kernel void batch_sensor_average_optimized(
    device float4 *output [[buffer(0)]],
    texture2d<float> viewport_texture [[texture(0)]],
    constant SensorRegion *regions [[buffer(1)]],
    constant uint &sensor_count [[buffer(2)]],
    constant uint &sensors_per_thread [[buffer(3)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint base_sensor_id = gid.x * sensors_per_thread;
    
    for (uint i = 0; i < sensors_per_thread && (base_sensor_id + i) < sensor_count; ++i) {
        uint sensor_id = base_sensor_id + i;
        SensorRegion region = regions[sensor_id];
        
        float3 acc = float3(0.0);
        uint sample_count = 0;
        
        // Sample region
        for (int dy = -region.radius; dy <= region.radius; ++dy) {
            for (int dx = -region.radius; dx <= region.radius; ++dx) {
                float2 sample_pos = float2(region.center_x + dx, region.center_y + dy);
                float4 color = viewport_texture.sample(texture_sampler, sample_pos);
                acc += color.rgb;
                sample_count++;
            }
        }
        
        float3 avg_color = (sample_count > 0) ? (acc / float(sample_count)) : float3(0.0);
        output[sensor_id] = float4(avg_color, 1.0);
    }
}
