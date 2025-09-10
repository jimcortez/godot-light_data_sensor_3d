// Average a buffer of float4 pixels and write a single float4 to output
RWStructuredBuffer<float4> outputColor : register(u0);
StructuredBuffer<float4> inputColor : register(t0);
cbuffer CSConstants : register(b0) {
    uint Count;
};

[numthreads(1,1,1)]
void mainCS(uint3 tid : SV_DispatchThreadID) {
    float3 acc = float3(0.0, 0.0, 0.0);
    uint n = Count;
    [loop]
    for (uint i = 0; i < n; ++i) {
        float4 c = inputColor[i];
        acc += c.rgb;
    }
    float inv = (n > 0) ? (1.0 / (float)n) : 0.0;
    float3 avg = acc * inv;
    outputColor[0] = float4(avg, 1.0);
}
