#ifndef LIGHT_DATA_SENSOR_3D_H
#define LIGHT_DATA_SENSOR_3D_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
#include <d3d12.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef __APPLE__
// Forward declarations for Metal objects; actual types are Objective-C and will be handled via opaque pointers.
// We keep these as void* to avoid requiring Objective-C++ compilation for this header.
typedef void* MTLDeviceRef;
typedef void* MTLCommandQueueRef;
typedef void* MTLComputePipelineStateRef;
typedef void* MTLBufferRef;
#endif

namespace godot {

class LightDataSensor3D : public Node3D {
    GDCLASS(LightDataSensor3D, Node3D);

private:
    // Metadata label provided by the developer
    String metadata_label;

    // Example: PCIe BAR resource & fence handles (Windows only, unused in M0 CPU path)
#ifdef _WIN32
    ComPtr<ID3D12Device> d3d_device;
    ComPtr<ID3D12Resource> shared_buffer;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event;
    UINT64 fence_value;
    // D3D12 compute members (M2)
    ComPtr<ID3D12CommandQueue> d3d_queue;
    ComPtr<ID3D12CommandAllocator> d3d_allocator;
    ComPtr<ID3D12GraphicsCommandList> d3d_cmdlist;
    ComPtr<ID3D12RootSignature> d3d_root_sig;
    ComPtr<ID3D12PipelineState> d3d_pso;
    ComPtr<ID3D12DescriptorHeap> d3d_desc_heap; // SRV/UAV heap
    UINT d3d_srvuav_desc_size = 0;
    // Buffers
    ComPtr<ID3D12Resource> d3d_input_buffer;       // DEFAULT, SRV
    ComPtr<ID3D12Resource> d3d_input_upload;       // UPLOAD (staging CPU->GPU)
    ComPtr<ID3D12Resource> d3d_constants_upload;   // UPLOAD (count)
    ComPtr<ID3D12Resource> d3d_output_buffer;      // DEFAULT, UAV
    ComPtr<ID3D12Resource> d3d_output_readback;    // READBACK
    UINT current_input_capacity = 0; // number of float4 elements allocated in input buffer
#endif

#ifdef __APPLE__
    // Opaque Metal handles (set up in platform-specific implementation). Unused until compute path is enabled.
    MTLDeviceRef mtl_device = nullptr;
    MTLCommandQueueRef mtl_command_queue = nullptr;
    MTLComputePipelineStateRef mtl_compute_pipeline = nullptr;
    MTLBufferRef mtl_output_buffer = nullptr; // Single RGBA32F pixel buffer
    bool use_metal = false;
#endif

    // Threading
    std::thread readback_thread;
    std::atomic_bool is_running;
	std::mutex frame_mutex;
	std::condition_variable frame_cv;
	bool frame_ready = false;

    // Last-read color
    Color last_color;
    // Whether a new color sample is ready to be signaled on the main thread
    std::atomic_bool has_new_color;

    // CPU sampling cadence (seconds) and accumulator for _process loop (M0)
    double poll_interval_seconds = 1.0 / 30.0; // ~30 Hz
    double time_since_last_sample = 0.0;

    // Frame region data provided by main thread to worker thread
    std::vector<float> frame_rgba32f;
    int frame_width = 0;
    int frame_height = 0;
    Vector2 screen_sample_pos = Vector2(0, 0);

protected:
    static void _bind_methods();

public:
    LightDataSensor3D();
    ~LightDataSensor3D();

    // Metadata property setter/getter
    void set_metadata_label(const String &p_label);
    String get_metadata_label() const;

    // Called when the node enters the scene
    void _ready() override;
    void _process(double p_delta) override;
    void _exit_tree() override;

    // Called to retrieve the color+label
    Dictionary get_light_data() const;

    // Returns true if a GPU compute backend is active for this node (e.g., Metal on macOS)
    bool is_using_gpu() const;

    // Set the screen-space sample position (pixels) where this sensor should sample.
    void set_screen_sample_pos(const Vector2 &p_screen_pos);
    Vector2 get_screen_sample_pos() const;

    // Lifecycle control (M3)
    void start();
    void stop();
    void set_poll_hz(double p_hz);

private:
    // Internal M0 CPU sampling helper
    void _sample_viewport_color();
    // Internal: capture a small center region and stage into frame_rgba32f for GPU/worker
    void _capture_center_region_for_gpu();

#ifdef _WIN32
    // Internal method to initialize PCIe BAR resources (unused in M0)
    void _init_pcie_bar();

    // Internal method that repeatedly does color readback on a separate thread (Windows thread worker)
    void _readback_loop();

    // Helper: read a single pixel from the shared buffer (unused in M0)
    Color _read_pixel_from_bar();
#endif

#ifdef __APPLE__
    // Platform-specific for Metal compute implementation
    void _init_metal_compute();
    void _metal_readback_loop();
    Color _read_pixel_from_mtl_buffer();
    void _cleanup_metal_objects();
#endif
};

} // namespace godot

#endif
