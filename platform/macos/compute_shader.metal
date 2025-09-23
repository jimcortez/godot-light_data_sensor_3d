#include <metal_stdlib>
using namespace metal;

// M6.5: Enhanced compute shader for direct texture sampling
// This shader samples a region from a texture and computes the average color

struct SampleConstants {
    uint center_x;
    uint center_y;
    uint radius;
    uint texture_width;
    uint texture_height;
};

kernel void compute_sensor_color(uint3 gid [[thread_position_in_grid]],
                                 device float4 *outPixel [[buffer(0)]],
                                 texture2d<float> inputTexture [[texture(0)]],
                                 constant SampleConstants &constants [[buffer(2)]]) {
    if (gid.x == 0 && gid.y == 0) {
        constexpr sampler textureSampler(mag_filter::linear, min_filter::linear);
        
        float3 color_sum = float3(0.0);
        uint sample_count = 0;
        
        // Sample a square region around the center point
        for (int dy = -int(constants.radius); dy <= int(constants.radius); ++dy) {
            for (int dx = -int(constants.radius); dx <= int(constants.radius); ++dx) {
                int x = int(constants.center_x) + dx;
                int y = int(constants.center_y) + dy;
                
                // Clamp to texture bounds
                x = max(0, min(x, int(constants.texture_width) - 1));
                y = max(0, min(y, int(constants.texture_height) - 1));
                
                // Sample the texture at the calculated position
                float2 texCoord = float2(float(x) / float(constants.texture_width),
                                        float(y) / float(constants.texture_height));
                float4 sample_color = inputTexture.sample(textureSampler, texCoord);
                
                color_sum += sample_color.rgb;
                sample_count++;
            }
        }
        
        // Compute average color
        if (sample_count > 0) {
            float3 average_color = color_sum / float(sample_count);
            outPixel[0] = float4(average_color, 1.0);
        } else {
            outPixel[0] = float4(0.0, 0.0, 0.0, 1.0);
        }
    }
}

// Legacy kernel for buffer-based processing (used when direct texture access is not available)
kernel void average_region(device float4 *outPixel [[buffer(0)]],
                          device float4 *inPixels [[buffer(1)]],
                          constant uint &count [[buffer(2)]],
                          uint3 gid [[thread_position_in_grid]]) {
    if (gid.x == 0 && gid.y == 0) {
        float3 acc = float3(0.0);
        uint n = count;
        for (uint i = 0; i < n; ++i) {
            float4 c = inPixels[i];
            acc += c.rgb;
        }
        float inv = (n > 0) ? (1.0 / float(n)) : 0.0;
        float3 avg = acc * inv;
        outPixel[0] = float4(avg, 1.0);
    }
}


