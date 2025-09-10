#include "batch_compute_manager.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// Metal implementation is in platform/macos/batch_compute_manager_macos.mm

using namespace godot;

void BatchComputeManager::_bind_methods() {
    // Properties
    ClassDB::bind_method(D_METHOD("initialize"), &BatchComputeManager::initialize);
    ClassDB::bind_method(D_METHOD("shutdown"), &BatchComputeManager::shutdown);
    ClassDB::bind_method(D_METHOD("is_available"), &BatchComputeManager::is_available);
    
    // Sensor management
    ClassDB::bind_method(D_METHOD("add_sensor", "sensor_id", "screen_x", "screen_y", "radius"), &BatchComputeManager::add_sensor, DEFVAL(4));
    ClassDB::bind_method(D_METHOD("remove_sensor", "sensor_id"), &BatchComputeManager::remove_sensor);
    ClassDB::bind_method(D_METHOD("clear_all_sensors"), &BatchComputeManager::clear_all_sensors);
    ClassDB::bind_method(D_METHOD("set_sample_radius", "radius"), &BatchComputeManager::set_sample_radius);
    
    // Processing
    ClassDB::bind_method(D_METHOD("process_sensors", "viewport_texture"), &BatchComputeManager::process_sensors);
    ClassDB::bind_method(D_METHOD("get_sensor_result", "sensor_id"), &BatchComputeManager::get_sensor_result);
    ClassDB::bind_method(D_METHOD("get_all_results"), &BatchComputeManager::get_all_results);
    
    // Configuration
    ClassDB::bind_method(D_METHOD("set_max_sensors", "max_count"), &BatchComputeManager::set_max_sensors);
    ClassDB::bind_method(D_METHOD("set_use_optimized_kernel", "use_optimized"), &BatchComputeManager::set_use_optimized_kernel);
    ClassDB::bind_method(D_METHOD("set_sensors_per_thread", "count"), &BatchComputeManager::set_sensors_per_thread);
    
    // Statistics
    ClassDB::bind_method(D_METHOD("get_sensor_count"), &BatchComputeManager::get_sensor_count);
    ClassDB::bind_method(D_METHOD("get_max_sensors"), &BatchComputeManager::get_max_sensors);
    ClassDB::bind_method(D_METHOD("is_processing_active"), &BatchComputeManager::is_processing_active);
}

BatchComputeManager::BatchComputeManager() {
    // Initialize with default values
    sensor_regions.reserve(max_sensors);
    sensor_results.reserve(max_sensors);
}

BatchComputeManager::~BatchComputeManager() {
    shutdown();
}

void BatchComputeManager::_ready() {
    // Auto-initialize when added to scene
    initialize();
}

void BatchComputeManager::_exit_tree() {
    shutdown();
}

bool BatchComputeManager::initialize() {
    // Use double-checked locking pattern for thread safety
    if (is_initialized.load()) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(data_mutex);
    
    // Check again after acquiring the lock
    if (is_initialized.load()) {
        return true;
    }
    
#ifdef __APPLE__
    if (!_init_metal_device()) {
        return false;
    }
    
    if (!_create_compute_pipelines()) {
        return false;
    }
    
    if (!_create_buffers()) {
        return false;
    }
    
    is_initialized.store(true);
    return true;
#else
    return false;
#endif
}

void BatchComputeManager::shutdown() {
    if (!is_initialized.load()) {
        return;
    }
    
    is_processing.store(false);
    
#ifdef __APPLE__
    _cleanup_metal_resources();
#endif
    
    std::lock_guard<std::mutex> lock(data_mutex);
    sensor_regions.clear();
    sensor_results.clear();
    
    is_initialized.store(false);
    UtilityFunctions::print("[BatchComputeManager] Shutdown complete");
}

bool BatchComputeManager::is_available() const {
    return is_initialized.load();
}

void BatchComputeManager::add_sensor(int sensor_id, float screen_x, float screen_y, int radius) {
    std::lock_guard<std::mutex> lock(data_mutex);
    
    // Check if sensor already exists
    int existing_index = _find_sensor_index(sensor_id);
    if (existing_index >= 0) {
        // Update existing sensor
        sensor_regions[existing_index] = SensorRegion(screen_x, screen_y, radius, sensor_id);
        return;
    }
    
    // Add new sensor
    sensor_regions.emplace_back(screen_x, screen_y, radius, sensor_id);
    sensor_results.emplace_back(Color(0, 0, 0, 1)); // Initialize with black
    
    _resize_buffers_if_needed();
}

void BatchComputeManager::remove_sensor(int sensor_id) {
    std::lock_guard<std::mutex> lock(data_mutex);
    
    int index = _find_sensor_index(sensor_id);
    if (index >= 0) {
        sensor_regions.erase(sensor_regions.begin() + index);
        sensor_results.erase(sensor_results.begin() + index);
    }
}

void BatchComputeManager::clear_all_sensors() {
    std::lock_guard<std::mutex> lock(data_mutex);
    sensor_regions.clear();
    sensor_results.clear();
}

void BatchComputeManager::set_sample_radius(int radius) {
    sample_radius = Math::max(1, Math::min(radius, 16)); // Clamp between 1 and 16
    
    std::lock_guard<std::mutex> lock(data_mutex);
    for (auto& region : sensor_regions) {
        region.radius = sample_radius;
    }
}

bool BatchComputeManager::process_sensors(Ref<ViewportTexture> viewport_texture) {
    if (!is_initialized.load() || !viewport_texture.is_valid()) {
        return false;
    }
    
    is_processing.store(true);
    
#ifdef __APPLE__
    if (!_create_viewport_texture(viewport_texture)) {
        is_processing.store(false);
        return false;
    }
    
    if (!_update_sensor_regions_buffer()) {
        is_processing.store(false);
        return false;
    }
    
    if (!_dispatch_compute_kernel()) {
        is_processing.store(false);
        return false;
    }
    
    if (!_read_results()) {
        is_processing.store(false);
        return false;
    }
#endif
    
    is_processing.store(false);
    return true;
}

Color BatchComputeManager::get_sensor_result(int sensor_id) const {
    std::lock_guard<std::mutex> lock(data_mutex);
    
    int index = _find_sensor_index(sensor_id);
    if (index >= 0 && index < static_cast<int>(sensor_results.size())) {
        return sensor_results[index];
    }
    
    return Color(0, 0, 0, 1); // Return black if not found
}

Array BatchComputeManager::get_all_results() const {
    std::lock_guard<std::mutex> lock(data_mutex);
    Array result;
    for (const auto& color : sensor_results) {
        result.append(color);
    }
    return result;
}

void BatchComputeManager::set_max_sensors(int max_count) {
    max_sensors = Math::max(1, max_count);
    sensor_regions.reserve(max_sensors);
    sensor_results.reserve(max_sensors);
}

void BatchComputeManager::set_use_optimized_kernel(bool use_optimized) {
    use_optimized_kernel = use_optimized;
}

void BatchComputeManager::set_sensors_per_thread(int count) {
    sensors_per_thread = Math::max(1, Math::min(count, 16)); // Clamp between 1 and 16
}

int BatchComputeManager::get_sensor_count() const {
    std::lock_guard<std::mutex> lock(data_mutex);
    return static_cast<int>(sensor_regions.size());
}

int BatchComputeManager::get_max_sensors() const {
    return max_sensors;
}

bool BatchComputeManager::is_processing_active() const {
    return is_processing.load();
}

int BatchComputeManager::_find_sensor_index(int sensor_id) const {
    for (size_t i = 0; i < sensor_regions.size(); ++i) {
        if (sensor_regions[i].sensor_id == sensor_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void BatchComputeManager::_resize_buffers_if_needed() {
    if (static_cast<int>(sensor_regions.size()) > max_sensors) {
        UtilityFunctions::print("[BatchComputeManager] Warning: Sensor count exceeds maximum, truncating");
        sensor_regions.resize(max_sensors);
        sensor_results.resize(max_sensors);
    }
}

#ifdef __APPLE__

// _init_metal_device() implementation is in platform/macos/batch_compute_manager_macos.mm

// _create_compute_pipelines() implementation is in platform/macos/batch_compute_manager_macos.mm

// _create_buffers() implementation is in platform/macos/batch_compute_manager_macos.mm

// _cleanup_metal_resources() implementation is in platform/macos/batch_compute_manager_macos.mm

// _create_viewport_texture() implementation is in platform/macos/batch_compute_manager_macos.mm

// _update_sensor_regions_buffer() implementation is in platform/macos/batch_compute_manager_macos.mm

// _dispatch_compute_kernel() implementation is in platform/macos/batch_compute_manager_macos.mm

// _read_results() implementation is in platform/macos/batch_compute_manager_macos.mm

#endif // __APPLE__
