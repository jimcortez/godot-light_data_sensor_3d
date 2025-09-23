#ifdef __linux__
#include "light_data_sensor_3d.h"
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void LightDataSensor3D::_init_linux_compute() {
    // Linux platform stub - no GPU compute implementation yet
    // This provides graceful fallback to CPU-only path with clear messaging
    UtilityFunctions::print("[LightDataSensor3D][Linux] GPU compute not yet implemented for Linux.");
    UtilityFunctions::print("[LightDataSensor3D][Linux] Falling back to CPU-only color sampling.");
    UtilityFunctions::print("[LightDataSensor3D][Linux] For GPU acceleration, consider using Windows (D3D12) or macOS (Metal).");
    UtilityFunctions::print("[LightDataSensor3D][Linux] Future versions may support Godot RenderingDevice compute.");
}

void LightDataSensor3D::_linux_readback_loop() {
    // Linux readback loop - CPU-only implementation
    // This is essentially a no-op since CPU sampling happens in the main thread
    // We just sleep to avoid busy-waiting
    
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 Hz
        
        // On Linux, we rely on the CPU sampling path in the main thread
        // The GPU readback loop is essentially a placeholder that doesn't interfere
        // with the CPU fallback functionality
    }
}

Color LightDataSensor3D::_read_pixel_from_linux() {
    // Linux pixel reading stub - returns current color from CPU sampling
    return current_color;
}

#endif // __linux__
