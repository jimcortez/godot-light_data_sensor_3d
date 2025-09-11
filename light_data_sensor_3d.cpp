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
    // Properties matching nanodeath LightSensor3D API
    ClassDB::bind_method(D_METHOD("get_color"), &LightDataSensor3D::get_color);
    ClassDB::bind_method(D_METHOD("get_light_level"), &LightDataSensor3D::get_light_level);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "", "get_color");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "light_level"), "", "get_light_level");

    // Legacy properties (kept for compatibility)
    ClassDB::bind_method(D_METHOD("set_metadata_label", "label"), &LightDataSensor3D::set_metadata_label);
    ClassDB::bind_method(D_METHOD("get_metadata_label"), &LightDataSensor3D::get_metadata_label);

    // Main API method
    ClassDB::bind_method(D_METHOD("refresh"), &LightDataSensor3D::refresh);
    
    // Utility methods
    ClassDB::bind_method(D_METHOD("is_using_gpu"), &LightDataSensor3D::is_using_gpu);
    ClassDB::bind_method(D_METHOD("set_screen_sample_pos", "screen_pos"), &LightDataSensor3D::set_screen_sample_pos);
    ClassDB::bind_method(D_METHOD("get_screen_sample_pos"), &LightDataSensor3D::get_screen_sample_pos);

    // Signals matching nanodeath LightSensor3D API
    ADD_SIGNAL(MethodInfo("color_updated", PropertyInfo(Variant::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("light_level_updated", PropertyInfo(Variant::FLOAT, "luminance")));
}

LightDataSensor3D::LightDataSensor3D() {
    has_new_readings = false;
    current_light_level = 0.0f;
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
    frame_cv.notify_all();
    if (readback_thread.joinable()) {
        readback_thread.join();
    }
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
    // No auto-start - developers should call refresh() as needed
}

void LightDataSensor3D::_exit_tree() {
    // Clean shutdown on tree exit
    frame_cv.notify_all();
    if (readback_thread.joinable()) {
        readback_thread.join();
    }
}


void LightDataSensor3D::set_metadata_label(const String &p_label) {
    metadata_label = p_label;
}

String LightDataSensor3D::get_metadata_label() const {
    return metadata_label;
}

Color LightDataSensor3D::get_color() const {
    return current_color;
}

float LightDataSensor3D::get_light_level() const {
    return current_light_level;
}

void LightDataSensor3D::refresh() {
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
        return;
    }
#elif defined(_WIN32)
    if (d3d_device != nullptr) {
        _capture_center_region_for_gpu();
        // Similar to macOS - simplified approach for now
        return;
    }
#endif
    // Fall back to CPU sampling
    _sample_viewport_color();
    
    // Emit signals for the new readings
    emit_signal("color_updated", current_color);
    emit_signal("light_level_updated", current_light_level);
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



// --- Internal methods ---

void LightDataSensor3D::_sample_viewport_color() {
    // Frame skipping to reduce expensive get_image() calls
    frame_skip_counter++;
    if (frame_skip_counter < frame_skip_interval) {
        // Skip this frame, use cached data if available
        return;
    }
    frame_skip_counter = 0; // Reset counter
    
    Viewport *vp = get_viewport();
    if (!vp) {
        return;
    }
    Ref<ViewportTexture> tex = vp->get_texture();
    if (tex.is_null()) {
        return;
    }
    
    // PERFORMANCE WARNING: get_image() causes expensive CPU-GPU synchronization
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
        current_color = Color(sum_r * inv, sum_g * inv, sum_b * inv, 1.0);
        current_light_level = _calculate_luminance(current_color);
    }
    // No image lock/unlock needed for CPU-side reads in this context.
}

void LightDataSensor3D::_capture_center_region_for_gpu() {
    // PERFORMANCE WARNING: This function is misnamed - it's actually doing CPU sampling with GPU processing
    // For true GPU pipelines, we should work directly with ViewportTexture as a GPU resource
    // However, Godot's current API doesn't provide direct GPU texture access without CPU sync
    
    // Frame skipping to reduce expensive get_image() calls
    frame_skip_counter++;
    if (frame_skip_counter < frame_skip_interval) {
        // Skip this frame, use cached data if available
        if (has_new_readings.exchange(false)) {
            emit_signal("color_updated", current_color);
            emit_signal("light_level_updated", current_light_level);
        }
        return;
    }
    frame_skip_counter = 0; // Reset counter
    
    Viewport *vp = get_viewport();
    if (!vp) {
        return;
    }
    Ref<ViewportTexture> tex = vp->get_texture();
    if (tex.is_null()) {
        return;
    }
    
    // PERFORMANCE WARNING: get_image() causes expensive CPU-GPU synchronization
    // This should only be called when absolutely necessary and at reduced frequency
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

float LightDataSensor3D::_calculate_luminance(const Color &color) const {
    // Calculate luminance using the standard formula: 0.299*R + 0.587*G + 0.114*B
    // This gives a value between 0 (dark) and 1 (bright)
    return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

// Windows D3D12 implementations are in light_data_sensor_3d_windows.cpp
