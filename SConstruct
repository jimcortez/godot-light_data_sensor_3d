#!/usr/bin/env python

import os

# Reuse godot-cpp build environment
env = SConscript("../../godot-cpp/SConstruct")

# Add this addon's include paths
env.Append(CPPPATH=["."])

sources = [
    "light_data_sensor_3d.cpp",
    "batch_compute_manager.cpp",
    "light_sensor_manager.cpp",
    "register_types.cpp",
]
if env["platform"] == "macos":
    # Objective-C++ source and Metal shader are used on macOS
    sources.append("platform/macos/light_data_sensor_3d_macos.mm")
    sources.append("platform/macos/batch_compute_manager_macos.mm")
    sources.append("platform/macos/metal_texture_access.mm")
if env["platform"] == "windows":
    sources.append("platform/windows/light_data_sensor_3d_windows.cpp")
if env["platform"] == "linux":
    sources.append("platform/linux/light_data_sensor_3d_linux.cpp")

# Output base directory
out_dir = "bin"

target_name = "liblight_data_sensor"

if env["platform"] == "macos":
    # Match the framework layout used by godot-cpp example for convenience
    framework_path = f"{out_dir}/{target_name}.{env['platform']}.{env['target']}.framework/{target_name}.{env['platform']}.{env['target']}"
    library = env.SharedLibrary(framework_path, source=sources)
    # Link Metal and Foundation frameworks
    env.Append(LINKFLAGS=["-framework", "Metal", "-framework", "Foundation"])
elif env["platform"] == "ios":
    library = env.StaticLibrary(
        f"{out_dir}/{target_name}.{env['platform']}.{env['target']}.a",
        source=sources,
    )
else:
    # Windows / Linux / others default to shared library artifact naming via suffix
    library = env.SharedLibrary(
        f"{out_dir}/{target_name}{env['suffix']}{env['SHLIBSUFFIX']}",
        source=sources,
    )

# Platform-specific linking (Windows: D3D12/DXGI when present)
if env["platform"] == "windows":
    env.Append(LIBS=["d3d12", "dxgi", "d3dcompiler"])

env.NoCache(library)
Default(library)


