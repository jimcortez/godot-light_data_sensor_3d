# LightDataSensor3D

A GDExtension addon for Godot 4.x that provides real-time light data sampling from 3D scenes using CPU and GPU backends.

**Author:** Jim Cortez  
**Version:** 1.0.0  
**License:** MIT

## Overview

LightDataSensor3D is a high-performance addon that allows you to sample light data from rendered 3D scenes in real-time. It supports both CPU and GPU-accelerated sampling backends:

- **macOS**: Metal compute shader backend for GPU acceleration
- **Windows**: D3D12 compute shader backend for GPU acceleration  
- **Linux**: CPU-based sampling (GPU compute support planned for future versions)
- **Fallback**: CPU-based sampling for all platforms

The addon is designed for applications that need to analyze lighting conditions, create reactive environments, or implement light-based interactions in 3D scenes.

## Features

- Real-time light data sampling from viewport textures
- GPU-accelerated compute shaders (Metal/D3D12)
- **M6.5**: GPU performance optimization with <0.2ms per sensor target
- **M6.5**: Direct GPU texture access framework (Metal/D3D12)
- **M6.5**: Performance monitoring and profiling tools
- **M6.5**: Batch processing optimization for multiple sensors
- Configurable sampling rates (1-240 Hz)
- Screen-space sample position targeting
- Thread-safe data collection
- Cross-platform compatibility (macOS, Windows, Linux)

## API Reference

### Class: LightDataSensor3D

Inherits from `Node3D`

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `metadata_label` | String | User-defined label for identifying this sensor |

#### Methods

| Method | Return Type | Description |
|--------|-------------|-------------|
| `get_color()` | Color | Returns current light color reading |
| `get_light_level()` | float | Returns current light level (luminance 0.0-1.0) |
| `refresh()` | void | Force immediate sampling and update readings (main thread only) |
| `is_using_gpu()` | bool | Returns true if GPU compute backend is active |
| `get_platform_info()` | String | Returns platform information and GPU availability |
| `get_support_status()` | String | Returns current backend status (GPU/CPU fallback) |
| `set_screen_sample_pos(pos: Vector2)` | void | Set screen-space sample position (pixels) |
| `get_screen_sample_pos()` | Vector2 | Get current screen-space sample position |
| **M6.5**: `get_average_sample_time()` | float | Get average sample time in milliseconds |
| **M6.5**: `reset_performance_stats()` | void | Reset performance statistics |
| **M6.5**: `set_use_direct_texture_access(enabled: bool)` | void | Enable/disable direct GPU texture access |
| **M6.5**: `get_use_direct_texture_access()` | bool | Check if direct texture access is enabled |

#### Signals

| Signal | Parameters | Description |
|--------|------------|-------------|
| `color_updated` | `color: Color` | Emitted when new light color is available |
| `light_level_updated` | `luminance: float` | Emitted when new light level is available |

#### Usage Example

```gdscript
extends Node3D

@onready var sensor: LightDataSensor3D = $LightDataSensor3D

func _ready():
    # Configure the sensor
    sensor.metadata_label = "Main Light Sensor"
    sensor.set_screen_sample_pos(Vector2(400, 300))  # Center of 800x600 viewport
    
    # M6.5: Enable performance optimizations
    sensor.set_use_direct_texture_access(true)
    
    # Connect to signals
    sensor.connect("color_updated", _on_color_updated)
    sensor.connect("light_level_updated", _on_light_level_updated)

func _process(delta):
    # Call refresh() as needed - you control the sampling frequency
    sensor.refresh()
    
    # M6.5: Monitor performance (optional)
    var avg_time = sensor.get_average_sample_time()
    if avg_time > 0.2:  # Warn if exceeding 0.2ms target
        print("Performance warning: ", avg_time, "ms per sample")

func _on_color_updated(color: Color):
    print("Light color: ", color)
    # Update UI, trigger events, etc.

func _on_light_level_updated(luminance: float):
    print("Light level: ", luminance)
    # Update brightness indicators, etc.

# Get current readings
func _on_button_pressed():
    var color = sensor.get_color()
    var level = sensor.get_light_level()
    print("Current color: ", color, " Level: ", level)
    
    # M6.5: Performance monitoring
    var avg_time = sensor.get_average_sample_time()
    print("Average sample time: ", avg_time, "ms")
    
    # Reset performance stats if needed
    if avg_time > 1.0:  # If consistently slow
        sensor.reset_performance_stats()
```

## M6.5: Performance Optimization Features

### GPU Performance Optimization
The addon now includes comprehensive GPU performance optimization with the following features:

- **Performance Target**: <0.2ms per sensor sampling time
- **Direct GPU Texture Access**: Framework for direct GPU texture sampling (Metal/D3D12)
- **Performance Monitoring**: Real-time performance tracking and validation
- **Batch Processing**: Optimized processing for multiple sensors
- **Automatic Fallback**: Graceful fallback to CPU when GPU optimizations are not available

### Performance Monitoring API
```gdscript
# Get average sample time
var avg_time = sensor.get_average_sample_time()
print("Average sample time: ", avg_time, "ms")

# Reset performance statistics
sensor.reset_performance_stats()

# Enable/disable direct texture access
sensor.set_use_direct_texture_access(true)
var is_direct = sensor.get_use_direct_texture_access()
```

### Performance Warnings
The addon automatically logs performance warnings when:
- Individual sensor sampling exceeds 0.2ms
- Batch processing exceeds performance targets
- GPU optimizations are not available

## Build Instructions

### Prerequisites

- Godot 4.x (for running the project)
- Python 3.x and SCons (install via `pip install scons`)
- Submodule `godot-cpp` present and built (see below)
- Platform SDKs:
  - **macOS**: Xcode/Command Line Tools (Metal SDK is part of Xcode)
  - **Windows**: Visual Studio with MSVC and Windows 10/11 SDK
  - **Linux**: GCC/Clang with standard C++ libraries

### Building

1. **Build godot-cpp dependency:**
   ```bash
   cd godot-cpp
   python3 -m pip install scons
   scons platform=macos target=template_debug -j8
   scons platform=macos target=template_release -j8
   # On Windows:
   # scons platform=windows target=template_debug -j8
   # scons platform=windows target=template_release -j8
   # On Linux:
   # scons platform=linux target=template_debug -j8
   # scons platform=linux target=template_release -j8
   ```
   
   This will produce the `libgodot-cpp` artifacts used for linking.

2. **Build the addon:**
   ```bash
   cd addons/light_data_sensor_3d
   scons platform=macos target=template_debug -j8
   scons platform=macos target=template_release -j8
   # On Windows:
   # scons platform=windows target=template_debug -j8
   # scons platform=windows target=template_release -j8
   # On Linux:
   # scons platform=linux target=template_debug -j8
   # scons platform=linux target=template_release -j8
   ```

3. **Enable the addon:**
   - Open the project in Godot 4.x
   - Project Settings → Plugins → enable `LightDataSensor3D`
   - The `LightDataSensor3D` node should appear in the Create Node dialog

4. **Run the demo:**
   - Open `demo/basic_cube_demo.tscn` and press Play.
   - The overlay label should read:
     - macOS: `Platform: macOS (Metal GPU compute available)` / `Backend: GPU Accelerated (Metal)`
     - Windows: `Platform: Windows (D3D12 GPU compute available)` / `Backend: GPU Accelerated (D3D12)` or `CPU Fallback (D3D12 unavailable)`
     - Linux: `Platform: Linux (CPU-only fallback)` / `Backend: CPU Fallback (GPU compute not implemented)`
   - Use arrow keys to rotate the cube; adjust Brightness slider to change light energy.
   - Six sensor rows display RGBA values and colored swatches, updating ~30 Hz.

### Output Structure

Built libraries are placed in `bin/`:
- **macOS**: `liblight_data_sensor.macos.template_{debug,release}.framework/`
- **Windows**: `liblight_data_sensor.windows.template_{debug,release}.<arch>.dll`
- **Linux**: `liblight_data_sensor.linux.template_{debug,release}.<arch>.so`

These paths match `light_data_sensor.gdextension` so Godot can load the library automatically.

### Expected Behavior

- Per-face colors change with rotation and brightness.
- macOS: GPU path averages a small region via Metal compute.
- Windows: GPU path averages a small region via D3D12 compute; if compute init fails, values still update via CPU fallback (averaging staged region in the worker thread).
- Linux: CPU fallback averages a small region via CPU sampling; displays clear messaging about GPU compute not being implemented.

### Packaging

- The addon is ready to zip: include the entire `addons/light_data_sensor_3d/` directory.
- Ensure `plugin.cfg` and `light_data_sensor.gdextension` are present.
- Include the `bin/` subfolder with per-OS artifacts for release builds.
- Provide the top-level `README.md` for quick usage and run steps.
- Optional: Include `demo/` folder in releases to showcase the addon.

Notes:
- Windows links against `d3d12`, `dxgi`, and `d3dcompiler`.
- The current macOS implementation enables the Metal compute path.
- Platform-specific source files are organized in `platform/macos/` and `platform/windows/` subdirectories.
- If you change file names or output locations, update `light_data_sensor.gdextension` accordingly.

## Platform-Specific Implementation

### macOS (Metal)
- Uses Metal compute shaders for GPU-accelerated color averaging
- Automatically falls back to CPU if Metal is unavailable
- Requires macOS 10.13+ for Metal support

### Windows (D3D12)
- Uses D3D12 compute shaders for GPU-accelerated color averaging
- Automatically falls back to CPU if D3D12 is unavailable
- Links against `d3d12`, `dxgi`, and `d3dcompiler` libraries

### Linux (CPU-only)
- Currently uses CPU-based sampling only
- Displays clear messaging about GPU compute not being implemented
- Future versions may support Godot RenderingDevice compute for GPU acceleration

### CPU Fallback
- Available on all platforms
- Samples a 9x9 pixel region around the target position
- Performs color averaging on the CPU

## Troubleshooting

### Addon Fails to Load
- Check Godot Output for GDExtension errors
- Verify built artifact paths in `light_data_sensor.gdextension` match your platform
- Ensure all required libraries are available

### Backend Shows "GDScript"
- Rebuild the addon and restart Godot
- Check that the correct platform libraries are built

### No Data Updates
- Verify the sensor is started (`sensor.start()`)
- Check that the sample position is within the viewport bounds
- Ensure the sampling rate is reasonable (1-240 Hz)

### Performance Issues
- **M6.5**: Check performance warnings in the console for sampling times >0.2ms
- **M6.5**: Use `sensor.get_average_sample_time()` to monitor performance
- **M6.5**: Enable direct texture access with `sensor.set_use_direct_texture_access(true)`
- **M6.5**: Reset performance stats with `sensor.reset_performance_stats()` if needed

### Demo-Specific Issues
- If the addon fails to load, check Godot Output for GDExtension errors. Ensure the built artifact paths in `light_data_sensor.gdextension` match your platform/target.
- If backend shows `GDScript`, rebuild the addon and restart the editor.
- If values appear static, verify the demo is running (Stop button toggled off), anchors are visible to the camera, and poll Hz is reasonable.

## Threading Requirements

**IMPORTANT**: The `force_sample()` method has strict threading requirements:

- ✅ **Safe**: Call from main thread (e.g., in `_ready()`, `_process()`, signal handlers, button callbacks)
- ❌ **Unsafe**: Call from background threads, worker threads, or async operations

This is because `force_sample()` calls Godot APIs like `get_viewport()`, `get_texture()`, and `get_image()` which are **not thread-safe** and must only be called from the main thread.

### Safe Usage Examples:
```gdscript
# ✅ Safe - called from main thread
func _ready():
    var data = sensor.force_sample()

func _on_button_pressed():
    var data = sensor.force_sample()

func _process(delta):
    if some_condition:
        var data = sensor.force_sample()
```

### Unsafe Usage Examples:
```gdscript
# ❌ Unsafe - called from background thread
func _thread_worker():
    var data = sensor.force_sample()  # Will crash!

# ❌ Unsafe - called from async operation
func _async_operation():
    await some_async_function()
    var data = sensor.force_sample()  # May crash!
```

## File Structure

```
addons/light_data_sensor_3d/
├── README.md                    # This file (includes build instructions)
├── plugin.cfg                  # Addon metadata
├── light_data_sensor.gdextension # GDExtension configuration
├── light_data_sensor_3d.h      # Main header file
├── light_data_sensor_3d.cpp    # Core implementation
├── register_types.cpp          # GDExtension registration
├── light_data_sensor_3d.gd     # Editor plugin script
├── platform/                   # Platform-specific implementations
│   ├── macos/
│   │   ├── light_data_sensor_3d_macos.mm
│   │   └── compute_shader.metal
│   ├── windows/
│   │   ├── light_data_sensor_3d_windows.cpp
│   │   └── compute_shader.hlsl
│   └── linux/
│       └── light_data_sensor_3d_linux.cpp
├── bin/                        # Built libraries (generated)
└── SConstruct                  # Build configuration
```

## Credits

**Author:** Jim Cortez  
**License:** MIT

Built with:
- [godot-cpp](https://github.com/godotengine/godot-cpp) - GDExtension C++ bindings
- Metal (macOS) - GPU compute framework
- Direct3D 12 (Windows) - GPU compute framework

## License

MIT License - see LICENSE file for details.
