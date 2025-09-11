# LightDataSensor3D

A GDExtension addon for Godot 4.x that provides real-time light data sampling from 3D scenes using CPU and GPU backends.

**Author:** Jim Cortez  
**Version:** 1.0.0  
**License:** MIT

## Overview

LightDataSensor3D is a high-performance addon that allows you to sample light data from rendered 3D scenes in real-time. It supports both CPU and GPU-accelerated sampling backends:

- **macOS**: Metal compute shader backend for GPU acceleration
- **Windows**: D3D12 compute shader backend for GPU acceleration  
- **Fallback**: CPU-based sampling for all platforms

The addon is designed for applications that need to analyze lighting conditions, create reactive environments, or implement light-based interactions in 3D scenes.

## Features

- Real-time light data sampling from viewport textures
- GPU-accelerated compute shaders (Metal/D3D12)
- Configurable sampling rates (1-240 Hz)
- Screen-space sample position targeting
- Thread-safe data collection
- Cross-platform compatibility (macOS, Windows)

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
| `set_screen_sample_pos(pos: Vector2)` | void | Set screen-space sample position (pixels) |
| `get_screen_sample_pos()` | Vector2 | Get current screen-space sample position |

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
    
    # Connect to signals
    sensor.connect("color_updated", _on_color_updated)
    sensor.connect("light_level_updated", _on_light_level_updated)

func _process(delta):
    # Call refresh() as needed - you control the sampling frequency
    sensor.refresh()

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
```

## Build Instructions

### Prerequisites

- Godot 4.x (for running the project)
- Python 3.x and SCons (install via `pip install scons`)
- Submodule `godot-cpp` present and built (see below)
- Platform SDKs:
  - **macOS**: Xcode/Command Line Tools (Metal SDK is part of Xcode)
  - **Windows**: Visual Studio with MSVC and Windows 10/11 SDK

### Building

1. **Build godot-cpp dependency:**
   ```bash
   cd godot-cpp
   python3 -m pip install scons
   scons platform=macos target=template_debug -j8
   scons platform=macos target=template_release -j8
   # On Windows instead:
   # scons platform=windows target=template_debug -j8
   # scons platform=windows target=template_release -j8
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
   ```

3. **Enable the addon:**
   - Open the project in Godot 4.x
   - Project Settings → Plugins → enable `LightDataSensor3D`
   - The `LightDataSensor3D` node should appear in the Create Node dialog

4. **Run the demo:**
   - Open `demo/basic_cube_demo.tscn` and press Play.
   - The overlay label should read:
     - macOS: `Backend: C++ (Metal)`
     - Windows: `Backend: C++`
   - Use arrow keys to rotate the cube; adjust Brightness slider to change light energy.
   - Six sensor rows display RGBA values and colored swatches, updating ~30 Hz.

### Output Structure

Built libraries are placed in `bin/`:
- **macOS**: `liblight_data_sensor.macos.template_{debug,release}.framework/`
- **Windows**: `liblight_data_sensor.windows.template_{debug,release}.<arch>.dll`

These paths match `light_data_sensor.gdextension` so Godot can load the library automatically.

### Expected Behavior

- Per-face colors change with rotation and brightness.
- macOS: GPU path averages a small region via Metal compute.
- Windows: GPU path averages a small region via D3D12 compute; if compute init fails, values still update via CPU fallback (averaging staged region in the worker thread).

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
│   └── windows/
│       ├── light_data_sensor_3d_windows.cpp
│       └── compute_shader.hlsl
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
