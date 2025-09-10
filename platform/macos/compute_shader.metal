#include <metal_stdlib>
using namespace metal;

// Minimal compute kernel stub for M2 scaffolding. Writes a constant color.
// TODO: In a future step, sample from a rendered frame texture and compute an average.

struct OutputPixelBuffer {
    device float4 *pixel [[id(0)]];
};

kernel void compute_sensor_color(uint3 gid [[thread_position_in_grid]],
                                 device float4 *outPixel [[buffer(0)]]) {
    if (gid.x == 0 && gid.y == 0) {
        outPixel[0] = float4(0.2, 0.6, 0.9, 1.0);
    }
}


