#ifndef LIGHT_SENSOR_MANAGER_H
#define LIGHT_SENSOR_MANAGER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

// Forward declaration
namespace godot {
class BatchComputeManager;
}

namespace godot {

// Structure to hold sensor data
struct SensorInfo {
    int sensor_id;
    Vector3 world_position;
    Vector2 screen_position;
    Color last_color;
    double last_update_time;
    bool is_active;
    String metadata_label;
    
    SensorInfo() : sensor_id(0), last_color(Color(0, 0, 0, 1)), last_update_time(0.0), is_active(false) {}
    SensorInfo(int id, const Vector3& pos, const String& label) 
        : sensor_id(id), world_position(pos), last_color(Color(0, 0, 0, 1)), last_update_time(0.0), is_active(true), metadata_label(label) {}
};

class LightSensorManager : public Node {
    GDCLASS(LightSensorManager, Node);

private:
    // Core components
    godot::BatchComputeManager* batch_compute_manager = nullptr;
    
    // Sensor data
    std::vector<SensorInfo> sensors;
    std::unordered_map<int, int> sensor_id_to_index; // Maps sensor_id to vector index
    mutable std::mutex sensor_mutex;
    
    // Timing and polling
    double poll_interval = 1.0 / 30.0; // 30 Hz default
    double time_since_last_update = 0.0;
    
    // Viewport and camera
    Viewport* viewport = nullptr;
    Camera3D* camera = nullptr;
    Ref<ViewportTexture> cached_viewport_texture;
    uint64_t last_frame_id = 0;
    
    // State
    std::atomic<bool> is_running{false};
    std::atomic<bool> is_initialized{false};
    
    // Configuration
    int next_sensor_id = 1;
    int sample_radius = 4;
    bool auto_update_screen_positions = true;
    bool use_gpu_acceleration = true;

protected:
    static void _bind_methods();

public:
    LightSensorManager();
    ~LightSensorManager();

    // Lifecycle
    void _ready() override;
    void _process(double delta) override;
    void _exit_tree() override;
    
    // Initialization
    bool initialize();
    void shutdown();
    bool is_available() const;
    
    // Sensor management
    int add_sensor(const Vector3& world_position, const String& metadata_label = "");
    void remove_sensor(int sensor_id);
    void clear_all_sensors();
    int get_sensor_count() const;
    
    // Sensor data access
    Color get_sensor_color(int sensor_id) const;
    Vector3 get_sensor_position(int sensor_id) const;
    Vector2 get_sensor_screen_position(int sensor_id) const;
    String get_sensor_metadata(int sensor_id) const;
    Dictionary get_sensor_data(int sensor_id) const;
    Array get_all_sensor_data() const;
    
    // Configuration
    void set_poll_hz(double hz);
    double get_poll_hz() const;
    void set_sample_radius(int radius);
    int get_sample_radius() const;
    void set_auto_update_screen_positions(bool enabled);
    bool get_auto_update_screen_positions() const;
    void set_use_gpu_acceleration(bool enabled);
    bool get_use_gpu_acceleration() const;
    
    // Control
    void start_sampling();
    void stop_sampling();
    bool is_sampling_active() const;
    
    // Manual updates
    void force_update_all_sensors();
    void update_sensor_screen_position(int sensor_id, const Vector2& screen_pos);
    
    // Camera and viewport
    void set_camera(Camera3D* cam);
    Camera3D* get_camera() const;
    void set_viewport(Viewport* vp);
    Viewport* get_viewport() const;

private:
    // Internal processing
    void _process_sensors();
    bool _update_viewport_cache();
    void _update_screen_positions();
    void _emit_sensor_signals();
    
    // Utility methods
    int _find_sensor_index(int sensor_id) const;
    void _resize_containers_if_needed();
    Vector2 _world_to_screen(const Vector3& world_pos) const;
    
    // Signal emission
    void _emit_sensor_updated_signal(int sensor_id, const Color& color);
};

} // namespace godot

#endif // LIGHT_SENSOR_MANAGER_H
