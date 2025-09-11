#ifdef _WIN32
#include "light_data_sensor_3d.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstring>
#include <wrl.h>
#include <chrono>
#include <thread>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace godot;

// D3D12 compute path to average a staged float4 buffer and write a single float4 result.

static ComPtr<ID3D12Device> _create_device() {
    ComPtr<ID3D12Device> device;
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    return device;
}

static const char *kAverageHLSL = R"(
RWStructuredBuffer<float4> outputColor : register(u0);
StructuredBuffer<float4> inputColor : register(t0);
cbuffer CSConstants : register(b0) { uint Count; };
[numthreads(1,1,1)]
void mainCS(uint3 tid : SV_DispatchThreadID) {
    float3 acc = float3(0.0, 0.0, 0.0);
    uint n = Count;
    for (uint i = 0; i < n; ++i) {
        float4 c = inputColor[i];
        acc += c.rgb;
    }
    float inv = (n > 0) ? (1.0 / (float)n) : 0.0;
    float3 avg = acc * inv;
    outputColor[0] = float4(avg, 1.0);
}
)";

static void _wait_fence(ComPtr<ID3D12Fence> &fence, HANDLE event_handle, UINT64 &value, ID3D12CommandQueue *queue) {
    const UINT64 signal = ++value;
    queue->Signal(fence.Get(), signal);
    if (fence->GetCompletedValue() < signal) {
        fence->SetEventOnCompletion(signal, event_handle);
        WaitForSingleObject(event_handle, INFINITE);
    }
}

void LightDataSensor3D::_init_pcie_bar() {
    d3d_device = _create_device();
    if (!d3d_device) return;
    UtilityFunctions::print("[LightDataSensor3D][Windows] D3D12 device created.");

    // Queue/allocator/cmdlist
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    d3d_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&d3d_queue));
    d3d_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&d3d_allocator));
    d3d_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, d3d_allocator.Get(), nullptr, IID_PPV_ARGS(&d3d_cmdlist));
    d3d_cmdlist->Close();

    // Descriptor heap for SRV/UAV
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.NumDescriptors = 2;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    d3d_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&d3d_desc_heap));
    d3d_srvuav_desc_size = d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Root signature: SRV table (t0), UAV table (u0), CBV root (b0)
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1; ranges[0].BaseShaderRegister = 0; ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1; ranges[1].BaseShaderRegister = 0; ranges[1].OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
    rs_desc.NumParameters = 3;
    rs_desc.pParameters = params;
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rs_blob, rs_err;
    D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err);
    if (FAILED(d3d_device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&d3d_root_sig)))) {
        UtilityFunctions::print("[LightDataSensor3D][Windows] Failed to create root signature; fallback to CPU.");
        return;
    }

    // Compile HLSL and create PSO
    ComPtr<ID3DBlob> cs_blob, cs_err;
    D3DCompile(kAverageHLSL, strlen(kAverageHLSL), nullptr, nullptr, nullptr, "mainCS", "cs_5_1", 0, 0, &cs_blob, &cs_err);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.pRootSignature = d3d_root_sig.Get();
    pso_desc.CS = { cs_blob->GetBufferPointer(), cs_blob->GetBufferSize() };
    if (FAILED(d3d_device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&d3d_pso)))) {
        UtilityFunctions::print("[LightDataSensor3D][Windows] Failed to create PSO; fallback to CPU.");
        return;
    }

    // Fence
    if (FAILED(d3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        UtilityFunctions::print("[LightDataSensor3D][Windows] Failed to create fence; fallback to CPU.");
        return;
    }
    fence_value = 0;
    fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void LightDataSensor3D::_readback_loop() {
    // GPU averaging via D3D12 compute. Falls back to sleep if device is missing.
    while (is_running) {
        if (!d3d_device || !d3d_queue) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            continue;
        }
        std::vector<float> pixels;
        UINT count = 0;
        {
            std::unique_lock<std::mutex> lock(frame_mutex);
            if (!frame_ready) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            frame_ready = false;
            pixels = frame_rgba32f;
            count = static_cast<UINT>(pixels.size() / 4);
        }
        if (count == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Ensure GPU buffers sized for current count
        UINT needed_capacity = count;
        UINT input_bytes = needed_capacity * sizeof(float) * 4;
        if (needed_capacity != current_input_capacity || !d3d_input_buffer) {
            current_input_capacity = needed_capacity;
            // Release old
            d3d_input_buffer.Reset(); d3d_input_upload.Reset(); d3d_output_buffer.Reset(); d3d_output_readback.Reset(); d3d_constants_upload.Reset();

            // Input DEFAULT buffer (SRV)
            D3D12_RESOURCE_DESC buf_desc = {};
            buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buf_desc.Alignment = 0;
            buf_desc.Width = input_bytes;
            buf_desc.Height = 1;
            buf_desc.DepthOrArraySize = 1;
            buf_desc.MipLevels = 1;
            buf_desc.Format = DXGI_FORMAT_UNKNOWN;
            buf_desc.SampleDesc.Count = 1;
            buf_desc.SampleDesc.Quality = 0;
            buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            buf_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES hp_default = {};
            hp_default.Type = D3D12_HEAP_TYPE_DEFAULT;
            d3d_device->CreateCommittedResource(&hp_default, D3D12_HEAP_FLAG_NONE, &buf_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&d3d_input_buffer));
            // Input UPLOAD staging
            D3D12_HEAP_PROPERTIES hp_upload = {}; hp_upload.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC upload_desc = buf_desc; upload_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            upload_desc.Width = input_bytes;
            d3d_device->CreateCommittedResource(&hp_upload, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&d3d_input_upload));
            // Constants UPLOAD (uint Count)
            D3D12_RESOURCE_DESC const_desc = upload_desc; const_desc.Width = 256;
            d3d_device->CreateCommittedResource(&hp_upload, D3D12_HEAP_FLAG_NONE, &const_desc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&d3d_constants_upload));
            // Output DEFAULT (UAV) 16 bytes
            D3D12_RESOURCE_DESC out_desc = buf_desc; out_desc.Width = 16; out_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            d3d_device->CreateCommittedResource(&hp_default, D3D12_HEAP_FLAG_NONE, &out_desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&d3d_output_buffer));
            // Readback
            D3D12_HEAP_PROPERTIES hp_readback = {}; hp_readback.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rb_desc = buf_desc; rb_desc.Width = 16; rb_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            d3d_device->CreateCommittedResource(&hp_readback, D3D12_HEAP_FLAG_NONE, &rb_desc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&d3d_output_readback));

            // Create SRV for input
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.FirstElement = 0;
            srv.Buffer.NumElements = needed_capacity;
            srv.Format = DXGI_FORMAT_UNKNOWN;
            srv.Buffer.StructureByteStride = sizeof(float) * 4;
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            auto cpu_start = d3d_desc_heap->GetCPUDescriptorHandleForHeapStart();
            d3d_device->CreateShaderResourceView(d3d_input_buffer.Get(), &srv, cpu_start);

            // Create UAV for output
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement = 0;
            uav.Buffer.NumElements = 1;
            uav.Format = DXGI_FORMAT_UNKNOWN;
            uav.Buffer.StructureByteStride = sizeof(float) * 4;
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_uav = cpu_start;
            cpu_uav.ptr += d3d_srvuav_desc_size * 1;
            d3d_device->CreateUnorderedAccessView(d3d_output_buffer.Get(), nullptr, &uav, cpu_uav);
        }

        // Upload input pixels
        {
            void *mapped = nullptr; D3D12_RANGE r = {0, 0};
            d3d_input_upload->Map(0, &r, &mapped);
            memcpy(mapped, pixels.data(), input_bytes);
            d3d_input_upload->Unmap(0, nullptr);
        }
        // Upload constants
        {
            void *mapped = nullptr; D3D12_RANGE r = {0, 0};
            d3d_constants_upload->Map(0, &r, &mapped);
            UINT *ptr = reinterpret_cast<UINT *>(mapped);
            ptr[0] = count;
            d3d_constants_upload->Unmap(0, nullptr);
        }

        // Record commands
        d3d_allocator->Reset();
        d3d_cmdlist->Reset(d3d_allocator.Get(), d3d_pso.Get());
        // Transition input to COPY_DEST
        D3D12_RESOURCE_BARRIER to_copy = {};
        to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_copy.Transition.pResource = d3d_input_buffer.Get();
        to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        d3d_cmdlist->ResourceBarrier(1, &to_copy);
        // Copy upload -> input
        d3d_cmdlist->CopyBufferRegion(d3d_input_buffer.Get(), 0, d3d_input_upload.Get(), 0, input_bytes);
        // Transition input to SRV readable
        D3D12_RESOURCE_BARRIER to_srv = to_copy;
        to_srv.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        to_srv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        d3d_cmdlist->ResourceBarrier(1, &to_srv);
        // Set descriptor heap
        ID3D12DescriptorHeap *heaps[] = { d3d_desc_heap.Get() };
        d3d_cmdlist->SetDescriptorHeaps(1, heaps);
        d3d_cmdlist->SetComputeRootSignature(d3d_root_sig.Get());
        // Set tables
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = d3d_desc_heap->GetGPUDescriptorHandleForHeapStart();
        d3d_cmdlist->SetComputeRootDescriptorTable(0, gpu_start);
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_uav = gpu_start; gpu_uav.ptr += d3d_srvuav_desc_size * 1;
        d3d_cmdlist->SetComputeRootDescriptorTable(1, gpu_uav);
        d3d_cmdlist->SetComputeRootConstantBufferView(2, d3d_constants_upload->GetGPUVirtualAddress());
        d3d_cmdlist->Dispatch(1, 1, 1);
        // UAV barrier then transition output to COPY_SOURCE
        D3D12_RESOURCE_BARRIER uav_barrier = {};
        uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav_barrier.UAV.pResource = d3d_output_buffer.Get();
        d3d_cmdlist->ResourceBarrier(1, &uav_barrier);
        D3D12_RESOURCE_BARRIER out_to_copy = {};
        out_to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        out_to_copy.Transition.pResource = d3d_output_buffer.Get();
        out_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        out_to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        out_to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        d3d_cmdlist->ResourceBarrier(1, &out_to_copy);
        d3d_cmdlist->CopyResource(d3d_output_readback.Get(), d3d_output_buffer.Get());
        // Transition output back to UAV for next iteration
        D3D12_RESOURCE_BARRIER out_to_uav = out_to_copy;
        out_to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        out_to_uav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        d3d_cmdlist->ResourceBarrier(1, &out_to_uav);
        d3d_cmdlist->Close();
        ID3D12CommandList *lists[] = { d3d_cmdlist.Get() };
        d3d_queue->ExecuteCommandLists(1, lists);
        _wait_fence(fence, fence_event, fence_value, d3d_queue.Get());

        // Read back
        void *mapped = nullptr; D3D12_RANGE read = {0, 16};
        if (SUCCEEDED(d3d_output_readback->Map(0, &read, &mapped)) && mapped) {
            float *p = reinterpret_cast<float *>(mapped);
            current_color = Color(p[0], p[1], p[2], p[3]);
            current_light_level = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            has_new_readings = true;
            d3d_output_readback->Unmap(0, nullptr);
        }
    }
}

Color LightDataSensor3D::_read_pixel_from_bar() {
    return current_color;
}

#endif // _WIN32


