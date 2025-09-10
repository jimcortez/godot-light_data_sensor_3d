#ifndef BATCH_COMPUTE_MANAGER_H
#define BATCH_COMPUTE_MANAGER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

#ifdef __APPLE__
// Forward declarations for Metal objects
typedef void* MTLDeviceRef;
typedef void* MTLCommandQueueRef;
typedef void* MTLComputePipelineStateRef;
typedef void* MTLBufferRef;
typedef void* MTLTextureRef;
typedef void* MTLCommandBufferRef;
typedef void* MTLComputeCommandEncoderRef;
#endif

namespace godot {

// Structure to define sensor sampling regions (matches Metal shader)
struct SensorRegion {
    float center_x;
    float center_y;
    int radius;
    int sensor_id;
    
    SensorRegion() : center_x(0.0f), center_y(0.0f), radius(4), sensor_id(0) {}
    SensorRegion(float x, float y, int r, int id) : center_x(x), center_y(y), radius(r), sensor_id(id) {}
};

class BatchComputeManager : public Node {
    GDCLASS(BatchComputeManager, Node);

private:
    // Metal resources
#ifdef __APPLE__
    MTLDeviceRef mtl_device = nullptr;
    MTLCommandQueueRef mtl_command_queue = nullptr;
    MTLComputePipelineStateRef batch_pipeline = nullptr;
    MTLComputePipelineStateRef optimized_pipeline = nullptr;
    
    // Buffers
    MTLBufferRef sensor_regions_buffer = nullptr;
    MTLBufferRef output_buffer = nullptr;
    MTLBufferRef sensor_count_buffer = nullptr;
    MTLBufferRef sensors_per_thread_buffer = nullptr;
    
    // Texture
    MTLTextureRef viewport_texture = nullptr;
#endif

    // Sensor data
    std::vector<SensorRegion> sensor_regions;
    std::vector<Color> sensor_results;
    mutable std::mutex data_mutex;
    
    // Configuration
    int max_sensors = 10000;
    int sample_radius = 4;
    bool use_optimized_kernel = false;
    int sensors_per_thread = 4;
    
    // State
    std::atomic<bool> is_initialized{false};
    std::atomic<bool> is_processing{false};

protected:
    static void _bind_methods();

public:
    BatchComputeManager();
    ~BatchComputeManager();

    // Lifecycle
    void _ready() override;
    void _exit_tree() override;
    
    // Public API
    bool initialize();
    void shutdown();
    bool is_available() const;
    
    // Sensor management
    void add_sensor(int sensor_id, float screen_x, float screen_y, int radius = 4);
    void remove_sensor(int sensor_id);
    void clear_all_sensors();
    void set_sample_radius(int radius);
    
    // Processing
    bool process_sensors(Ref<ViewportTexture> viewport_texture);
    Color get_sensor_result(int sensor_id) const;
    Array get_all_results() const;
    
    // Configuration
    void set_max_sensors(int max_count);
    void set_use_optimized_kernel(bool use_optimized);
    void set_sensors_per_thread(int count);
    
    // Statistics
    int get_sensor_count() const;
    int get_max_sensors() const;
    bool is_processing_active() const;

private:
#ifdef __APPLE__
    // Metal initialization
    bool _init_metal_device();
    bool _create_compute_pipelines();
    bool _create_buffers();
    void _cleanup_metal_resources();
    
    // Metal processing
    bool _create_viewport_texture(Ref<ViewportTexture> viewport_texture);
    bool _update_sensor_regions_buffer();
    bool _dispatch_compute_kernel();
    bool _read_results();
    
    // Helper methods
    MTLBufferRef _create_buffer(size_t size, bool shared = true);
    void _release_buffer(MTLBufferRef buffer);
#endif
    
    // Utility methods
    int _find_sensor_index(int sensor_id) const;
    void _resize_buffers_if_needed();
};

} // namespace godot

#endif // BATCH_COMPUTE_MANAGER_H
