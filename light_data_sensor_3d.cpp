#include "light_data_sensor_3d.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

// For demonstration, minimal error checking
#include <chrono>
#include <thread>

using namespace godot;

void LightDataSensor3D::_bind_methods() {
    // Properties
    ClassDB::bind_method(D_METHOD("set_metadata_label", "label"), &LightDataSensor3D::set_metadata_label);
    ClassDB::bind_method(D_METHOD("get_metadata_label"), &LightDataSensor3D::get_metadata_label);
    // ADD_PROPERTY(PropertyInfo(Variant::STRING, "metadata_label"), "set_metadata_label", "get_metadata_label");

    // Methods
    ClassDB::bind_method(D_METHOD("get_light_data"), &LightDataSensor3D::get_light_data);
    ClassDB::bind_method(D_METHOD("force_sample"), &LightDataSensor3D::force_sample);
    ClassDB::bind_method(D_METHOD("is_using_gpu"), &LightDataSensor3D::is_using_gpu);
    ClassDB::bind_method(D_METHOD("set_screen_sample_pos", "screen_pos"), &LightDataSensor3D::set_screen_sample_pos);
    ClassDB::bind_method(D_METHOD("get_screen_sample_pos"), &LightDataSensor3D::get_screen_sample_pos);

    // Lifecycle methods (M3)
    ClassDB::bind_method(D_METHOD("start"), &LightDataSensor3D::start);
    ClassDB::bind_method(D_METHOD("stop"), &LightDataSensor3D::stop);
    ClassDB::bind_method(D_METHOD("set_poll_hz", "hz"), &LightDataSensor3D::set_poll_hz);

    // Signal when new data is available (M3)
    ADD_SIGNAL(MethodInfo("data_updated", PropertyInfo(Variant::COLOR, "color")));
}

LightDataSensor3D::LightDataSensor3D() {
    is_running = false;
    has_new_color = false;
#ifdef _WIN32
    fence_value = 0;
    fence_event = nullptr;
#endif
#ifdef __APPLE__
    // Individual sensor Metal resources (shared resources managed by MetalResourceManager)
    mtl_output_buffer = nullptr;
#endif
}

LightDataSensor3D::~LightDataSensor3D() {
    // Ensure clean shutdown
    stop();
#ifdef _WIN32
    if (fence_event) {
        CloseHandle(fence_event);
        fence_event = nullptr;
    }
#endif
#ifdef __APPLE__
    // Clean up Metal objects
    _cleanup_metal_objects();
#endif
}

void LightDataSensor3D::_ready() {
    // Auto-start by default (M3)
    start();
}

void LightDataSensor3D::_exit_tree() {
    // Clean shutdown on tree exit (M3)
    stop();
}

void LightDataSensor3D::_process(double p_delta) {
    if (!is_running) {
        return;
    }
    time_since_last_sample += p_delta;
    if (time_since_last_sample < poll_interval_seconds) {
        // If worker thread updated a new color, emit signal without sampling
        if (has_new_color.exchange(false)) {
            emit_signal("data_updated", last_color);
        }
        return;
    }
    time_since_last_sample = 0.0;
#if defined(__APPLE__)
    if (use_metal) {
        _capture_center_region_for_gpu();
        if (has_new_color.exchange(false)) {
            emit_signal("data_updated", last_color);
        }
        return;
    }
#elif defined(_WIN32)
    // On Windows, use GPU path if D3D12 is available, otherwise fallback to CPU
    if (d3d_device != nullptr) {
        _capture_center_region_for_gpu();
        if (has_new_color.exchange(false)) {
            emit_signal("data_updated", last_color);
        }
        return;
    }
#endif
    _sample_viewport_color();
    has_new_color = true;
    emit_signal("data_updated", last_color);
}

void LightDataSensor3D::set_metadata_label(const String &p_label) {
    metadata_label = p_label;
}

String LightDataSensor3D::get_metadata_label() const {
    return metadata_label;
}

Dictionary LightDataSensor3D::get_light_data() const {
    Dictionary data;
    data["label"] = metadata_label;
    data["color"] = last_color;
    return data;
}

Dictionary LightDataSensor3D::force_sample() {
    // Force immediate sampling regardless of is_running state
    // This allows getting fresh data even when the sampling process is stopped
    
    // IMPORTANT: This method MUST be called from the main thread only!
    // Godot API calls (get_viewport(), get_texture(), etc.) are not thread-safe
    // and must only be called from the main thread.
    
    // Use the same sampling logic as _process() but without the timing constraints
#if defined(__APPLE__)
    if (use_metal) {
        _capture_center_region_for_gpu();
        // For GPU path, we need to wait for the readback to complete
        // This is a simplified approach - in a production system you might want
        // to implement a synchronous version of the GPU sampling
        return get_light_data();
    }
#elif defined(_WIN32)
    if (d3d_device != nullptr) {
        _capture_center_region_for_gpu();
        // Similar to macOS - simplified approach for now
        return get_light_data();
    }
#endif
    // Fall back to CPU sampling
    _sample_viewport_color();
    
    Dictionary data;
    data["label"] = metadata_label;
    data["color"] = last_color;
    return data;
}

bool LightDataSensor3D::is_using_gpu() const {
#ifdef __APPLE__
    return use_metal;
#elif defined(_WIN32)
    return d3d_device != nullptr;
#else
    return false;
#endif
}

void LightDataSensor3D::set_screen_sample_pos(const Vector2 &p_screen_pos) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    screen_sample_pos = p_screen_pos;
}

Vector2 LightDataSensor3D::get_screen_sample_pos() const {
    return screen_sample_pos;
}

// Lifecycle control (M3)
void LightDataSensor3D::start() {
    if (is_running) {
        return;
    }
    is_running = true;
    has_new_color = false;
    set_process(true);
#ifdef __APPLE__
    _init_metal_compute();
    if (use_metal) {
        if (!readback_thread.joinable()) {
            readback_thread = std::thread(&LightDataSensor3D::_metal_readback_loop, this);
        }
        return;
    }
#endif
#ifdef _WIN32
    _init_pcie_bar();
    if (!readback_thread.joinable()) {
        readback_thread = std::thread(&LightDataSensor3D::_readback_loop, this);
    }
#endif
}

void LightDataSensor3D::stop() {
    if (!is_running) {
        return;
    }
    is_running = false;
    frame_cv.notify_all();
    if (readback_thread.joinable()) {
        readback_thread.join();
    }
    set_process(false);
}

void LightDataSensor3D::set_poll_hz(double p_hz) {
    if (p_hz < 1.0) {
        p_hz = 1.0;
    }
    if (p_hz > 240.0) {
        p_hz = 240.0;
    }
    poll_interval_seconds = 1.0 / p_hz;
}

// --- Internal methods ---

void LightDataSensor3D::_sample_viewport_color() {
    Viewport *vp = get_viewport();
    if (!vp) {
        return;
    }
    Ref<ViewportTexture> tex = vp->get_texture();
    if (tex.is_null()) {
        return;
    }
    Ref<Image> img = tex->get_image();
    if (img.is_null()) {
        return;
    }

    const int width = img->get_width();
    const int height = img->get_height();
    if (width <= 0 || height <= 0) {
        return;
    }

    // Sample a small square around the center to reduce cost.
    const int sample_radius = 4; // 9x9 max 81 samples
    const int cx = width / 2;
    const int cy = height / 2;
    double sum_r = 0.0;
    double sum_g = 0.0;
    double sum_b = 0.0;
    int sample_count = 0;

    for (int dy = -sample_radius; dy <= sample_radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height) continue;
        for (int dx = -sample_radius; dx <= sample_radius; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= width) continue;
            const Color c = img->get_pixel(x, y);
            sum_r += c.r;
            sum_g += c.g;
            sum_b += c.b;
            ++sample_count;
        }
    }

    if (sample_count > 0) {
        const double inv = 1.0 / static_cast<double>(sample_count);
        last_color = Color(sum_r * inv, sum_g * inv, sum_b * inv, 1.0);
    }
    // No image lock/unlock needed for CPU-side reads in this context.
}

void LightDataSensor3D::_capture_center_region_for_gpu() {
    Viewport *vp = get_viewport();
    if (!vp) {
        return;
    }
    Ref<ViewportTexture> tex = vp->get_texture();
    if (tex.is_null()) {
        return;
    }
    Ref<Image> img = tex->get_image();
    if (img.is_null()) {
        return;
    }
    const int width = img->get_width();
    const int height = img->get_height();
    if (width <= 0 || height <= 0) {
        return;
    }
    // Prepare a small center region for GPU averaging.
    const int sample_radius = 4;
    int cx = width / 2;
    int cy = height / 2;
    // If a screen sample position was set, prefer it.
    if (screen_sample_pos.x > 0 && screen_sample_pos.y > 0) {
        cx = static_cast<int>(screen_sample_pos.x);
        cy = static_cast<int>(screen_sample_pos.y);
    }
    const int region_w = sample_radius * 2 + 1;
    const int region_h = sample_radius * 2 + 1;
    std::vector<float> local_buffer;
    local_buffer.reserve(region_w * region_h * 4);
    for (int dy = -sample_radius; dy <= sample_radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= height) continue;
        for (int dx = -sample_radius; dx <= sample_radius; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= width) continue;
            const Color c = img->get_pixel(x, y);
            local_buffer.push_back(c.r);
            local_buffer.push_back(c.g);
            local_buffer.push_back(c.b);
            local_buffer.push_back(1.0f);
        }
    }
    // No image lock/unlock needed for CPU-side reads in this context.
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        frame_rgba32f = std::move(local_buffer);
        frame_width = region_w;
        frame_height = region_h;
        frame_ready = true;
    }
    frame_cv.notify_one();
}

// Windows D3D12 implementations are in light_data_sensor_3d_windows.cpp
