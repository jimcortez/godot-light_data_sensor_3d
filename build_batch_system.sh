#!/bin/bash

# Build script for the new batch GPU compute system
# This script compiles the new LightSensorManager and BatchComputeManager

set -e

echo "Building Batch GPU Compute System..."

# Set up paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
GODOT_CPP_DIR="$PROJECT_ROOT/../godot-cpp"

# Check if godot-cpp exists
if [ ! -d "$GODOT_CPP_DIR" ]; then
    echo "Error: godot-cpp directory not found at $GODOT_CPP_DIR"
    echo "Please ensure godot-cpp is properly set up"
    exit 1
fi

# Change to the addon directory
cd "$SCRIPT_DIR"

# Create build directory
mkdir -p build
cd build

# Set up environment variables
export GODOT_CPP_DIR="$GODOT_CPP_DIR"
export CXXFLAGS="-std=c++17 -O3"

# Compile the new classes
echo "Compiling BatchComputeManager..."
g++ -c ../batch_compute_manager.cpp \
    -I"$GODOT_CPP_DIR/include" \
    -I"$GODOT_CPP_DIR/gen/include" \
    -I"$GODOT_CPP_DIR/gdextension" \
    -std=c++17 \
    -O3 \
    -o batch_compute_manager.o

echo "Compiling LightSensorManager..."
g++ -c ../light_sensor_manager.cpp \
    -I"$GODOT_CPP_DIR/include" \
    -I"$GODOT_CPP_DIR/gen/include" \
    -I"$GODOT_CPP_DIR/gdextension" \
    -std=c++17 \
    -O3 \
    -o light_sensor_manager.o

echo "Compiling register_types..."
g++ -c ../register_types.cpp \
    -I"$GODOT_CPP_DIR/include" \
    -I"$GODOT_CPP_DIR/gen/include" \
    -I"$GODOT_CPP_DIR/gdextension" \
    -std=c++17 \
    -O3 \
    -o register_types.o

# Compile existing light_data_sensor_3d.cpp
echo "Compiling existing LightDataSensor3D..."
g++ -c ../light_data_sensor_3d.cpp \
    -I"$GODOT_CPP_DIR/include" \
    -I"$GODOT_CPP_DIR/gen/include" \
    -I"$GODOT_CPP_DIR/gdextension" \
    -std=c++17 \
    -O3 \
    -o light_data_sensor_3d.o

# Compile platform-specific files
echo "Compiling macOS Metal implementation..."
if [ -f "../platform/macos/light_data_sensor_3d_macos.mm" ]; then
    clang++ -c ../platform/macos/light_data_sensor_3d_macos.mm \
        -I"$GODOT_CPP_DIR/include" \
        -I"$GODOT_CPP_DIR/gen/include" \
        -I"$GODOT_CPP_DIR/gdextension" \
        -I".." \
        -std=c++17 \
        -O3 \
        -framework Metal \
        -framework Foundation \
        -o light_data_sensor_3d_macos.o
fi

# Link everything together
echo "Linking library..."
g++ -shared \
    batch_compute_manager.o \
    light_sensor_manager.o \
    register_types.o \
    light_data_sensor_3d.o \
    light_data_sensor_3d_macos.o \
    -L"$GODOT_CPP_DIR/bin" \
    -lgodot-cpp.macos.template_debug \
    -framework Metal \
    -framework Foundation \
    -o liblight_data_sensor_batch.macos.template_debug.dylib

echo "Build complete!"
echo "Library created: build/liblight_data_sensor_batch.macos.template_debug.dylib"

# Copy to the addon directory
cp liblight_data_sensor_batch.macos.template_debug.dylib ../
echo "Library copied to addon directory"

echo ""
echo "To use the new batch system:"
echo "1. Update your .gdextension file to point to the new library"
echo "2. Use LightSensorManager instead of individual LightDataSensor3D nodes"
echo "3. See demo/optimized_stress_tester.gd for usage example"
echo ""
echo "Expected performance improvements:"
echo "- 2500 sensors: <1 FPS -> 30-60 FPS (30-60x improvement)"
echo "- 1000 sensors: 2-3 FPS -> 60+ FPS (20-30x improvement)"
echo "- 500 sensors: 5-10 FPS -> 60+ FPS (6-12x improvement)"
