#include "light_sensor_manager.h"
#include "batch_compute_manager.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/timer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void LightSensorManager::_bind_methods() {
    // Signals
    ADD_SIGNAL(MethodInfo("sensor_updated", PropertyInfo(Variant::INT, "sensor_id"), PropertyInfo(Variant::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("all_sensors_updated"));
    
    // Properties
    ClassDB::bind_method(D_METHOD("initialize"), &LightSensorManager::initialize);
    ClassDB::bind_method(D_METHOD("shutdown"), &LightSensorManager::shutdown);
    ClassDB::bind_method(D_METHOD("is_available"), &LightSensorManager::is_available);
    
    // Sensor management
    ClassDB::bind_method(D_METHOD("add_sensor", "world_position", "metadata_label"), &LightSensorManager::add_sensor, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("remove_sensor", "sensor_id"), &LightSensorManager::remove_sensor);
    ClassDB::bind_method(D_METHOD("clear_all_sensors"), &LightSensorManager::clear_all_sensors);
    ClassDB::bind_method(D_METHOD("get_sensor_count"), &LightSensorManager::get_sensor_count);
    
    // Sensor data access
    ClassDB::bind_method(D_METHOD("get_sensor_color", "sensor_id"), &LightSensorManager::get_sensor_color);
    ClassDB::bind_method(D_METHOD("get_sensor_position", "sensor_id"), &LightSensorManager::get_sensor_position);
    ClassDB::bind_method(D_METHOD("get_sensor_screen_position", "sensor_id"), &LightSensorManager::get_sensor_screen_position);
    ClassDB::bind_method(D_METHOD("get_sensor_metadata", "sensor_id"), &LightSensorManager::get_sensor_metadata);
    ClassDB::bind_method(D_METHOD("get_sensor_data", "sensor_id"), &LightSensorManager::get_sensor_data);
    ClassDB::bind_method(D_METHOD("get_all_sensor_data"), &LightSensorManager::get_all_sensor_data);
    
    // Configuration
    ClassDB::bind_method(D_METHOD("set_poll_hz", "hz"), &LightSensorManager::set_poll_hz);
    ClassDB::bind_method(D_METHOD("get_poll_hz"), &LightSensorManager::get_poll_hz);
    ClassDB::bind_method(D_METHOD("set_sample_radius", "radius"), &LightSensorManager::set_sample_radius);
    ClassDB::bind_method(D_METHOD("get_sample_radius"), &LightSensorManager::get_sample_radius);
    ClassDB::bind_method(D_METHOD("set_auto_update_screen_positions", "enabled"), &LightSensorManager::set_auto_update_screen_positions);
    ClassDB::bind_method(D_METHOD("get_auto_update_screen_positions"), &LightSensorManager::get_auto_update_screen_positions);
    ClassDB::bind_method(D_METHOD("set_use_gpu_acceleration", "enabled"), &LightSensorManager::set_use_gpu_acceleration);
    ClassDB::bind_method(D_METHOD("get_use_gpu_acceleration"), &LightSensorManager::get_use_gpu_acceleration);
    
    // Control
    ClassDB::bind_method(D_METHOD("start_sampling"), &LightSensorManager::start_sampling);
    ClassDB::bind_method(D_METHOD("stop_sampling"), &LightSensorManager::stop_sampling);
    ClassDB::bind_method(D_METHOD("is_sampling_active"), &LightSensorManager::is_sampling_active);
    
    // Manual updates
    ClassDB::bind_method(D_METHOD("force_update_all_sensors"), &LightSensorManager::force_update_all_sensors);
    ClassDB::bind_method(D_METHOD("update_sensor_screen_position", "sensor_id", "screen_pos"), &LightSensorManager::update_sensor_screen_position);
    
    // Camera and viewport
    ClassDB::bind_method(D_METHOD("set_camera", "camera"), &LightSensorManager::set_camera);
    ClassDB::bind_method(D_METHOD("get_camera"), &LightSensorManager::get_camera);
    ClassDB::bind_method(D_METHOD("set_viewport", "viewport"), &LightSensorManager::set_viewport);
    ClassDB::bind_method(D_METHOD("get_viewport"), &LightSensorManager::get_viewport);
}

LightSensorManager::LightSensorManager() {
    // Batch compute manager will be created in _ready()
    batch_compute_manager = nullptr;
}

LightSensorManager::~LightSensorManager() {
    shutdown();
}

void LightSensorManager::_ready() {
    // Call parent's _ready() to ensure ready signal is emitted
    Node::_ready();
    
    // Create batch compute manager as a child node
    batch_compute_manager = memnew(BatchComputeManager);
    add_child(batch_compute_manager);
    
    // Defer initialization to next frame to ensure viewport is available
    call_deferred("initialize");
}

void LightSensorManager::_process(double delta) {
    if (!is_running.load() || !is_initialized.load()) {
        return;
    }
    
    time_since_last_update += delta;
    
    // Update screen positions if enabled
    if (auto_update_screen_positions) {
        _update_screen_positions();
    }
    
    // Process sensors if enough time has passed
    if (time_since_last_update >= poll_interval) {
        _process_sensors();
        time_since_last_update = 0.0;
    }
}

void LightSensorManager::_exit_tree() {
    shutdown();
}

bool LightSensorManager::initialize() {
    if (is_initialized.load()) {
        return true;
    }
    
    // Check if batch compute manager exists
    if (!batch_compute_manager) {
        return false;
    }
    
    // Initialize batch compute manager
    if (!batch_compute_manager->initialize()) {
        return false;
    }
    
    // Get viewport and camera references
    if (!viewport) {
        viewport = get_viewport();
        if (!viewport) {
            // Don't fail initialization, just use CPU fallback
        }
    }
    
    // Try to find camera automatically
    if (!camera && viewport) {
        camera = viewport->get_camera_3d();
    }
    
    is_initialized.store(true);
    return true;
}

void LightSensorManager::shutdown() {
    if (!is_initialized.load()) {
        return;
    }
    
    stop_sampling();
    
    if (batch_compute_manager) {
        batch_compute_manager->shutdown();
        // The child node will be automatically freed when the parent is freed
        batch_compute_manager = nullptr;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    sensors.clear();
    sensor_id_to_index.clear();
    
    is_initialized.store(false);
}

bool LightSensorManager::is_available() const {
    return is_initialized.load() && batch_compute_manager && batch_compute_manager->is_available();
}

int LightSensorManager::add_sensor(const Vector3& world_position, const String& metadata_label) {
    
    if (!is_initialized.load()) {
        return -1;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    int sensor_id = next_sensor_id++;
    Vector2 screen_pos = _world_to_screen(world_position);
    
    // Add to internal storage
    sensors.emplace_back(sensor_id, world_position, metadata_label);
    sensors.back().screen_position = screen_pos;
    sensor_id_to_index[sensor_id] = static_cast<int>(sensors.size() - 1);
    
    // Add to batch compute manager
    batch_compute_manager->add_sensor(sensor_id, screen_pos.x, screen_pos.y, sample_radius);
    
    _resize_containers_if_needed();
    
    return sensor_id;
}

void LightSensorManager::remove_sensor(int sensor_id) {
    if (!is_initialized.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it == sensor_id_to_index.end()) {
        return;
    }
    
    int index = it->second;
    
    // Remove from batch compute manager
    batch_compute_manager->remove_sensor(sensor_id);
    
    // Remove from internal storage
    sensors.erase(sensors.begin() + index);
    sensor_id_to_index.erase(it);
    
    // Update indices for remaining sensors
    for (auto& pair : sensor_id_to_index) {
        if (pair.second > index) {
            pair.second--;
        }
    }
    
}

void LightSensorManager::clear_all_sensors() {
    if (!is_initialized.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    batch_compute_manager->clear_all_sensors();
    sensors.clear();
    sensor_id_to_index.clear();
    
}

int LightSensorManager::get_sensor_count() const {
    std::lock_guard<std::mutex> lock(sensor_mutex);
    return static_cast<int>(sensors.size());
}

Color LightSensorManager::get_sensor_color(int sensor_id) const {
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        return sensors[it->second].last_color;
    }
    
    return Color(0, 0, 0, 1);
}

Vector3 LightSensorManager::get_sensor_position(int sensor_id) const {
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        return sensors[it->second].world_position;
    }
    
    return Vector3();
}

Vector2 LightSensorManager::get_sensor_screen_position(int sensor_id) const {
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        return sensors[it->second].screen_position;
    }
    
    return Vector2();
}

String LightSensorManager::get_sensor_metadata(int sensor_id) const {
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        return sensors[it->second].metadata_label;
    }
    
    return "";
}

Dictionary LightSensorManager::get_sensor_data(int sensor_id) const {
    Dictionary data;
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        const SensorInfo& sensor = sensors[it->second];
        data["sensor_id"] = sensor.sensor_id;
        data["world_position"] = sensor.world_position;
        data["screen_position"] = sensor.screen_position;
        data["color"] = sensor.last_color;
        data["metadata_label"] = sensor.metadata_label;
        data["is_active"] = sensor.is_active;
    }
    
    return data;
}

Array LightSensorManager::get_all_sensor_data() const {
    Array result;
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    for (const auto& sensor : sensors) {
        Dictionary data;
        data["sensor_id"] = sensor.sensor_id;
        data["world_position"] = sensor.world_position;
        data["screen_position"] = sensor.screen_position;
        data["color"] = sensor.last_color;
        data["metadata_label"] = sensor.metadata_label;
        data["is_active"] = sensor.is_active;
        result.append(data);
    }
    
    return result;
}

void LightSensorManager::set_poll_hz(double hz) {
    poll_interval = Math::max(0.01, 1.0 / Math::max(1.0, hz));
}

double LightSensorManager::get_poll_hz() const {
    return 1.0 / poll_interval;
}

void LightSensorManager::set_sample_radius(int radius) {
    sample_radius = Math::max(1, Math::min(radius, 16));
    
    if (batch_compute_manager) {
        batch_compute_manager->set_sample_radius(sample_radius);
    }
}

int LightSensorManager::get_sample_radius() const {
    return sample_radius;
}

void LightSensorManager::set_auto_update_screen_positions(bool enabled) {
    auto_update_screen_positions = enabled;
}

bool LightSensorManager::get_auto_update_screen_positions() const {
    return auto_update_screen_positions;
}

void LightSensorManager::set_use_gpu_acceleration(bool enabled) {
    use_gpu_acceleration = enabled;
}

bool LightSensorManager::get_use_gpu_acceleration() const {
    return use_gpu_acceleration;
}

void LightSensorManager::start_sampling() {
    if (!is_initialized.load()) {
        return;
    }
    
    is_running.store(true);
    
}

void LightSensorManager::stop_sampling() {
    is_running.store(false);
    
}

bool LightSensorManager::is_sampling_active() const {
    return is_running.load();
}

void LightSensorManager::force_update_all_sensors() {
    if (!is_initialized.load()) {
        return;
    }
    
    _process_sensors();
}

void LightSensorManager::update_sensor_screen_position(int sensor_id, const Vector2& screen_pos) {
    if (!is_initialized.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    auto it = sensor_id_to_index.find(sensor_id);
    if (it != sensor_id_to_index.end() && it->second < static_cast<int>(sensors.size())) {
        sensors[it->second].screen_position = screen_pos;
        batch_compute_manager->add_sensor(sensor_id, screen_pos.x, screen_pos.y, sample_radius);
    }
}

void LightSensorManager::set_camera(Camera3D* cam) {
    camera = cam;
}

Camera3D* LightSensorManager::get_camera() const {
    return camera;
}

void LightSensorManager::set_viewport(Viewport* vp) {
    viewport = vp;
    
    // If we have a batch compute manager and it's initialized, update the viewport cache
    if (batch_compute_manager && is_initialized.load()) {
        if (vp) {
            // Force update the viewport cache
            _update_viewport_cache();
        }
    }
}

Viewport* LightSensorManager::get_viewport() const {
    return viewport;
}

void LightSensorManager::_process_sensors() {
    if (!is_initialized.load() || !batch_compute_manager) {
        return;
    }
    
    // Update viewport cache
    if (!_update_viewport_cache()) {
        // If no viewport is available, we can't process sensors yet
        return;
    }
    
    // Process sensors using batch compute manager
    if (use_gpu_acceleration && batch_compute_manager->is_available()) {
        if (batch_compute_manager->process_sensors(cached_viewport_texture)) {
            _emit_sensor_signals();
        } else {
        }
    } else {
    }
}

bool LightSensorManager::_update_viewport_cache() {
    if (!viewport) {
        return false;
    }
    
    // Get new viewport texture
    cached_viewport_texture = viewport->get_texture();
    if (cached_viewport_texture.is_null()) {
        return false;
    }
    
    return true;
}

void LightSensorManager::_update_screen_positions() {
    if (!camera || !is_initialized.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    for (auto& sensor : sensors) {
        Vector2 new_screen_pos = _world_to_screen(sensor.world_position);
        if (new_screen_pos != sensor.screen_position) {
            sensor.screen_position = new_screen_pos;
            batch_compute_manager->add_sensor(sensor.sensor_id, new_screen_pos.x, new_screen_pos.y, sample_radius);
        }
    }
}

void LightSensorManager::_emit_sensor_signals() {
    if (!batch_compute_manager) {
        return;
    }
    
    Array results_array = batch_compute_manager->get_all_results();
    std::vector<Color> results;
    for (int i = 0; i < results_array.size(); ++i) {
        results.push_back(results_array[i]);
    }
    
    std::lock_guard<std::mutex> lock(sensor_mutex);
    
    for (size_t i = 0; i < sensors.size() && i < results.size(); ++i) {
        if (sensors[i].last_color != results[i]) {
            sensors[i].last_color = results[i];
            _emit_sensor_updated_signal(sensors[i].sensor_id, results[i]);
        }
    }
    
    emit_signal("all_sensors_updated");
}

int LightSensorManager::_find_sensor_index(int sensor_id) const {
    auto it = sensor_id_to_index.find(sensor_id);
    return (it != sensor_id_to_index.end()) ? it->second : -1;
}

void LightSensorManager::_resize_containers_if_needed() {
    // This could be used to implement dynamic resizing if needed
    // For now, we'll let the containers grow as needed
}

Vector2 LightSensorManager::_world_to_screen(const Vector3& world_pos) const {
    if (!camera) {
        return Vector2();
    }
    
    Vector2 screen_pos = camera->unproject_position(world_pos);
    return screen_pos;
}

void LightSensorManager::_emit_sensor_updated_signal(int sensor_id, const Color& color) {
    emit_signal("sensor_updated", sensor_id, color);
}
