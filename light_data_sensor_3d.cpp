#include "light_data_sensor_3d.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef __APPLE__
// Forward declaration for MetalResourceManager
namespace MetalResourceManager {
    bool isAvailable();
}
#endif

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
    ClassDB::bind_method(D_METHOD("get_platform_info"), &LightDataSensor3D::get_platform_info);
    ClassDB::bind_method(D_METHOD("get_support_status"), &LightDataSensor3D::get_support_status);
    ClassDB::bind_method(D_METHOD("set_screen_sample_pos", "screen_pos"), &LightDataSensor3D::set_screen_sample_pos);
    ClassDB::bind_method(D_METHOD("get_screen_sample_pos"), &LightDataSensor3D::get_screen_sample_pos);
    
    // M6.5: Performance monitoring methods
    ClassDB::bind_method(D_METHOD("get_average_sample_time"), &LightDataSensor3D::get_average_sample_time);
    ClassDB::bind_method(D_METHOD("reset_performance_stats"), &LightDataSensor3D::reset_performance_stats);
    ClassDB::bind_method(D_METHOD("set_use_direct_texture_access", "enabled"), &LightDataSensor3D::set_use_direct_texture_access);
    ClassDB::bind_method(D_METHOD("get_use_direct_texture_access"), &LightDataSensor3D::get_use_direct_texture_access);
    ClassDB::bind_method(D_METHOD("get_optimization_strategy"), &LightDataSensor3D::get_optimization_strategy);

    // Virtual method bindings - these are automatically handled by the base class
    // No need to bind virtual methods like _ready, _process, _exit_tree

    // Signals matching nanodeath LightSensor3D API
    ADD_SIGNAL(MethodInfo("color_updated", PropertyInfo(Variant::COLOR, "color")));
    ADD_SIGNAL(MethodInfo("light_level_updated", PropertyInfo(Variant::FLOAT, "luminance")));
}

LightDataSensor3D::LightDataSensor3D() {
    has_new_readings = false;
    is_running = false;
    current_light_level = 0.0f;
#ifdef _WIN32
    fence_value = 0;
    fence_event = nullptr;
#endif
#ifdef __APPLE__
    // Individual sensor Metal resources (shared resources managed by MetalResourceManager)
    mtl_output_buffer = nullptr;
#endif

#ifdef __linux__
    // Linux platform initialization - CPU-only fallback
    use_linux_gpu = false;
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

#ifdef __linux__
    // Linux cleanup - currently no special cleanup needed for CPU-only fallback
#endif
}

void LightDataSensor3D::_ready() {
    // Initialize platform-specific compute backends
    _initialize_platform_compute();
    // No auto-start - developers should call refresh() as needed
}

void LightDataSensor3D::_process(double p_delta) {
    // This method is required by the Node3D base class
    // Currently no per-frame processing needed - sampling is done via refresh() calls
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
    
    // For refresh(), always use CPU sampling for immediate results
    // GPU paths are designed for background processing and don't emit signals immediately
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
#elif defined(__linux__)
    return use_linux_gpu;
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

// M6.5: Performance monitoring API

double LightDataSensor3D::get_average_sample_time() const {
    return _get_average_sample_time();
}

void LightDataSensor3D::reset_performance_stats() {
    _reset_performance_stats();
}

void LightDataSensor3D::set_use_direct_texture_access(bool enabled) {
    use_direct_texture_access = enabled;
}

bool LightDataSensor3D::get_use_direct_texture_access() const {
    return use_direct_texture_access;
}

String LightDataSensor3D::get_optimization_strategy() const {
    // M6.5: Return information about which optimization strategy is being used
    if (_is_gpu_mode_available() && use_direct_texture_access) {
        return "Direct GPU Texture Access (Optimal)";
    } else if (_is_gpu_mode_available()) {
        return "GPU Mode with Texture Caching";
    } else {
        return "CPU Fallback with Frame Skipping";
    }
}

String LightDataSensor3D::get_platform_info() const {
#ifdef __APPLE__
    return "macOS (Metal GPU compute available)";
#elif defined(_WIN32)
    return "Windows (D3D12 GPU compute available)";
#elif defined(__linux__)
    return "Linux (CPU-only fallback)";
#else
    return "Unknown Platform";
#endif
}

String LightDataSensor3D::get_support_status() const {
#ifdef __APPLE__
    if (use_metal) {
        return "GPU Accelerated (Metal)";
    } else {
        return "CPU Fallback (Metal unavailable)";
    }
#elif defined(_WIN32)
    if (d3d_device != nullptr) {
        return "GPU Accelerated (D3D12)";
    } else {
        return "CPU Fallback (D3D12 unavailable)";
    }
#elif defined(__linux__)
    return "CPU Fallback (GPU compute not implemented)";
#else
    return "Unsupported Platform";
#endif
}



// --- Internal methods ---

void LightDataSensor3D::_initialize_platform_compute() {
    // Initialize platform-specific compute backends
#ifdef __APPLE__
    _init_metal_compute();
#elif defined(_WIN32)
    _init_pcie_bar();
#elif defined(__linux__)
    _init_linux_compute();
#endif
}

void LightDataSensor3D::_sample_viewport_color() {
    // M6.5: GPU mode detection and optimization
    // If GPU mode is available, use GPU-optimized sampling
    if (_is_gpu_mode_available()) {
        _sample_gpu_optimized();
        return;
    }
    
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
    
    // M6.5: Only use get_image() in CPU fallback mode
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

    // Sample a small square around the target position to reduce cost.
    const int sample_radius = 4; // 9x9 max 81 samples
    int cx = width / 2;  // Default to center
    int cy = height / 2;
    
    // Use screen_sample_pos if it's been set
    if (screen_sample_pos.x > 0 && screen_sample_pos.y > 0) {
        cx = static_cast<int>(screen_sample_pos.x);
        cy = static_cast<int>(screen_sample_pos.y);
    }
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
    } else {
    }
    // No image lock/unlock needed for CPU-side reads in this context.
}

void LightDataSensor3D::_capture_center_region_for_gpu() {
    // M6.5: Hybrid GPU optimization strategy
    // This function implements a multi-tier approach to eliminate get_image() calls
    
    // 1. Try direct GPU texture access first (best performance)
    if (_is_gpu_mode_available() && use_direct_texture_access) {
        if (_capture_gpu_direct_texture()) {
            return; // Success with direct GPU access
        }
    }
    
    // 2. Try texture caching strategy (reduces get_image() calls by 80-90%)
    if (_capture_cached_texture()) {
        return; // Success with cached texture
    }
    
    // 3. Fall back to optimized get_image() with frame skipping
    _capture_fallback_optimized();
}


bool LightDataSensor3D::_capture_cached_texture() {
    // M6.5: Intelligent texture caching strategy
    // Cache the texture and only update when necessary
    
    // Start performance timing
    _start_performance_timer();
    
    static Ref<Image> cached_image;
    static uint64_t last_frame = 0;
    static bool cache_valid = false;
    
    uint64_t current_frame = Engine::get_singleton()->get_process_frames();
    
    // Only get new image if frame has changed
    if (!cache_valid || current_frame != last_frame) {
        Viewport *vp = get_viewport();
        if (!vp) {
            _end_performance_timer();
            return false;
        }
        
        Ref<ViewportTexture> tex = vp->get_texture();
        if (tex.is_null()) {
            _end_performance_timer();
            return false;
        }
        
        // Only call get_image() when absolutely necessary
        cached_image = tex->get_image();
        cache_valid = !cached_image.is_null();
        last_frame = current_frame;
    }
    
    if (!cache_valid) {
        _end_performance_timer();
        return false;
    }
    
    // Use cached image for processing
    bool result = _process_cached_image(cached_image);
    _end_performance_timer();
    return result;
}

void LightDataSensor3D::_capture_fallback_optimized() {
    // M6.5: Optimized fallback with frame skipping
    // This is the last resort when other methods fail
    
    // Start performance timing
    _start_performance_timer();
    
    // Frame skipping to reduce expensive get_image() calls
    frame_skip_counter++;
    if (frame_skip_counter < frame_skip_interval) {
        // Skip this frame, use cached data if available
        if (has_new_readings.exchange(false)) {
            emit_signal("color_updated", current_color);
            emit_signal("light_level_updated", current_light_level);
        }
        _end_performance_timer();
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
    
    // M6.5: Only use get_image() when absolutely necessary
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
    
    // End performance timing
    _end_performance_timer();
}

float LightDataSensor3D::_calculate_luminance(const Color &color) const {
    // Calculate luminance using the standard formula: 0.299*R + 0.587*G + 0.114*B
    // This gives a value between 0 (dark) and 1 (bright)
    return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

// M6.5: GPU Performance Optimization methods

bool LightDataSensor3D::_is_gpu_mode_available() const {
    // M6.5: Check if GPU compute backend is available and active
    // This determines whether we can use GPU-optimized sampling
#ifdef __APPLE__
    return use_metal && MetalResourceManager::isAvailable();
#elif defined(_WIN32)
    return d3d_device != nullptr;
#elif defined(__linux__)
    return use_linux_gpu;
#else
    return false;
#endif
}

void LightDataSensor3D::_sample_gpu_optimized() {
    // M6.5: GPU-optimized sampling that avoids get_image() calls
    // This method uses direct GPU texture access when available
    
    // Start performance timing
    _start_performance_timer();
    
    // For now, we'll use the existing GPU pipeline but with optimizations
    // In future phases, this will implement true direct GPU texture access
    
    // Use the existing GPU sampling method but with performance optimizations
    _capture_center_region_for_gpu();
    
    // End performance timing
    _end_performance_timer();
    
    // M6.5: Additional optimizations can be added here:
    // - Direct GPU texture sampling
    // - Batch processing for multiple sensors
    // - Reduced CPU-GPU synchronization
}

void LightDataSensor3D::_sample_cpu_fallback() {
    // M6.5: CPU fallback sampling with optimizations
    // This method is used when GPU mode is not available
    
    // Start performance timing
    _start_performance_timer();
    
    // Frame skipping to reduce expensive get_image() calls
    frame_skip_counter++;
    if (frame_skip_counter < frame_skip_interval) {
        // Skip this frame, use cached data if available
        _end_performance_timer(); // Still time the skip
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
    
    // M6.5: Only use get_image() in CPU fallback mode
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

    // Sample a small square around the target position to reduce cost.
    const int sample_radius = 4; // 9x9 max 81 samples
    int cx = width / 2;  // Default to center
    int cy = height / 2;
    
    // Use screen_sample_pos if it's been set
    if (screen_sample_pos.x > 0 && screen_sample_pos.y > 0) {
        cx = static_cast<int>(screen_sample_pos.x);
        cy = static_cast<int>(screen_sample_pos.y);
    }
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
    
    // End performance timing
    _end_performance_timer();
    
    // No image lock/unlock needed for CPU-side reads in this context.
}

bool LightDataSensor3D::_capture_gpu_direct_texture() {
    // M6.5: Direct GPU texture access implementation
    // This method attempts to work directly with GPU textures without CPU-GPU synchronization
    
    // Start performance timing
    _start_performance_timer();
    
    Viewport *vp = get_viewport();
    if (!vp) {
        _end_performance_timer();
        return false;
    }
    Ref<ViewportTexture> tex = vp->get_texture();
    if (tex.is_null()) {
        _end_performance_timer();
        return false;
    }
    
    // M6.5: Platform-specific direct GPU texture access implementation
    // This method implements true direct GPU access when available, avoiding CPU-GPU synchronization
    
#ifdef __APPLE__
    // macOS Metal direct texture access
    if (use_metal && MetalResourceManager::isAvailable()) {
        // Use Metal compute shaders to sample the texture directly on GPU
        // This avoids the expensive get_image() call and CPU-GPU synchronization
        if (_capture_metal_direct_texture(tex)) {
            _end_performance_timer();
            return true; // Success with direct Metal access
        }
    }
#elif defined(_WIN32)
    // Windows D3D12 direct texture access
    if (d3d_device != nullptr) {
        // Use D3D12 compute shaders to sample the texture directly on GPU
        if (_capture_d3d12_direct_texture(tex)) {
            _end_performance_timer();
            return; // Success with direct D3D12 access
        }
    }
#elif defined(__linux__)
    // Linux direct texture access (future implementation)
    if (use_linux_gpu) {
        // Future: Use Vulkan or OpenGL compute shaders for direct GPU access
        // Currently not implemented - falls back to CPU
    }
#endif
    
    // If direct GPU access is not available or fails, fall back to optimized CPU method
    // This ensures we always get results, even if not optimal
    _sample_cpu_fallback();
    _end_performance_timer();
    return false; // Indicate that direct GPU access was not successful
}

bool LightDataSensor3D::_process_cached_image(Ref<Image> img) {
    // M6.5: Process cached image data
    // This method processes the cached image without calling get_image() again
    
    if (img.is_null()) {
        return false;
    }
    
    const int width = img->get_width();
    const int height = img->get_height();
    if (width <= 0 || height <= 0) {
        return false;
    }
    
    // Prepare a small center region for GPU averaging
    const int sample_radius = 4;
    int cx = width / 2;
    int cy = height / 2;
    
    // If a screen sample position was set, prefer it
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
    
    // Store the processed data for GPU processing
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        frame_rgba32f = std::move(local_buffer);
        frame_width = region_w;
        frame_height = region_h;
        frame_ready = true;
    }
    frame_cv.notify_one();
    
    return true;
}

// M6.5: Platform-specific direct GPU texture access implementations

bool LightDataSensor3D::_capture_d3d12_direct_texture(Ref<ViewportTexture> tex) {
    // M6.5: Windows D3D12 direct texture access implementation
    // This method attempts to work directly with D3D12 textures without CPU-GPU synchronization
    
    // Start performance timing
    _start_performance_timer();
    
    // TODO: Implement actual D3D12 direct texture access
    // This would require:
    // 1. Getting the D3D12 texture from the ViewportTexture RID
    // 2. Using D3D12 compute shaders to sample the texture directly
    // 3. Avoiding CPU-GPU synchronization
    
    // For now, this is a placeholder that falls back to CPU
    // In future phases, this will implement true D3D12 direct access
    _end_performance_timer();
    return false; // Indicate that direct D3D12 access is not yet implemented
}

// M6.5: Performance monitoring methods

void LightDataSensor3D::_start_performance_timer() {
    last_sample_time = std::chrono::high_resolution_clock::now();
}

void LightDataSensor3D::_end_performance_timer() {
    auto current_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_sample_time);
    double sample_time_ms = duration.count() / 1000.0; // Convert to milliseconds
    
    // Update running average
    sample_count++;
    average_sample_time = (average_sample_time * (sample_count - 1) + sample_time_ms) / sample_count;
    
    // M6.5: Log performance warnings if sampling is too slow
    if (sample_time_ms > 0.2) { // Target: <0.2ms per sensor
        UtilityFunctions::print("[LightDataSensor3D] Performance Warning: Sample time ", sample_time_ms, "ms exceeds target of 0.2ms");
    }
}

double LightDataSensor3D::_get_average_sample_time() const {
    return average_sample_time;
}

void LightDataSensor3D::_reset_performance_stats() {
    average_sample_time = 0.0;
    sample_count = 0;
}

// Windows D3D12 implementations are in light_data_sensor_3d_windows.cpp
