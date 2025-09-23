#ifndef LIGHT_DATA_SENSOR_3D_H
#define LIGHT_DATA_SENSOR_3D_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>

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

#ifdef __linux__
// Linux platform declarations - currently CPU-only fallback
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
    // Individual sensor Metal resources (now using shared resources)
    MTLBufferRef mtl_output_buffer = nullptr; // Single RGBA32F pixel buffer per sensor
    bool use_metal = false;
#endif

#ifdef __linux__
    // Linux platform members - currently CPU-only fallback
    bool use_linux_gpu = false; // Reserved for future RenderingDevice implementation
#endif

    // Threading
    std::thread readback_thread;
	std::mutex frame_mutex;
	std::condition_variable frame_cv;
	bool frame_ready = false;

    // Current sensor readings
    Color current_color;
    float current_light_level;
    // Whether new readings are ready to be signaled on the main thread
    std::atomic_bool has_new_readings;
    // Whether the readback thread should continue running
    std::atomic_bool is_running{false};

    // Frame skipping to reduce expensive get_image() calls
    int frame_skip_counter = 0;
    int frame_skip_interval = 3; // Only call get_image() every 3rd frame
    
    // M6.5: Performance monitoring and optimization
    bool use_direct_texture_access = false; // Enable direct GPU texture access
    std::chrono::high_resolution_clock::time_point last_sample_time;
    double average_sample_time = 0.0; // Average time per sample in milliseconds
    int sample_count = 0; // Number of samples taken for averaging

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

    // Properties matching nanodeath LightSensor3D API
    Color get_color() const;
    float get_light_level() const;
    
    // Main API method - updates sensor readings
    // WARNING: This method MUST be called from the main thread only!
    // Calling from background threads will cause crashes due to Godot API restrictions.
    void refresh();

    // Returns true if a GPU compute backend is active for this node (e.g., Metal on macOS)
    bool is_using_gpu() const;

    // Get platform information and support status
    String get_platform_info() const;
    String get_support_status() const;

    // Set the screen-space sample position (pixels) where this sensor should sample.
    void set_screen_sample_pos(const Vector2 &p_screen_pos);
    Vector2 get_screen_sample_pos() const;
    
    // M6.5: Performance monitoring API
    double get_average_sample_time() const;
    void reset_performance_stats();
    void set_use_direct_texture_access(bool enabled);
    bool get_use_direct_texture_access() const;
    String get_optimization_strategy() const;


private:
    // Platform-specific initialization
    void _initialize_platform_compute();
    
    // Internal M0 CPU sampling helper
    void _sample_viewport_color();
    // Internal: capture a small center region and stage into frame_rgba32f for GPU/worker
    void _capture_center_region_for_gpu();
    // Internal: calculate luminance from color (0=dark, 1=bright)
    float _calculate_luminance(const Color &color) const;
    
    // M6.5: GPU Performance Optimization methods
    bool _is_gpu_mode_available() const;
    void _sample_gpu_optimized();
    void _sample_cpu_fallback();
    bool _capture_gpu_direct_texture();
    
    // M6.5: Platform-specific direct GPU texture access methods
    bool _capture_metal_direct_texture(Ref<ViewportTexture> tex);
    bool _capture_d3d12_direct_texture(Ref<ViewportTexture> tex);
    
    // M6.5: Hybrid optimization strategy methods
    bool _capture_cached_texture();
    void _capture_fallback_optimized();
    bool _process_cached_image(Ref<Image> img);
    
    // M6.5: Performance monitoring methods
    void _start_performance_timer();
    void _end_performance_timer();
    double _get_average_sample_time() const;
    void _reset_performance_stats();

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
    bool _process_metal_texture_direct(void* device, void* queue, void* pipeline, void* outBuf, void* metal_texture);
#endif

#ifdef __linux__
    // Platform-specific for Linux implementation (currently CPU-only fallback)
    void _init_linux_compute();
    void _linux_readback_loop();
    Color _read_pixel_from_linux();
#endif
};

} // namespace godot

#endif
