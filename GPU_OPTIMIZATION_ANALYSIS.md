# GPU Performance Optimization Analysis

## Current Performance Issues

### Problem: Expensive `get_image()` Calls in GPU Mode

The current implementation has a critical performance bottleneck where GPU-accelerated sensors still call `get_image()` on the ViewportTexture, causing expensive CPU-GPU synchronization that takes ~0.2ms per sensor.

### Identified Problem Areas

1. **`_capture_center_region_for_gpu()` in `light_data_sensor_3d.cpp:305`**
   ```cpp
   // PERFORMANCE WARNING: get_image() causes expensive CPU-GPU synchronization
   Ref<Image> img = tex->get_image();
   ```
   - This method is misnamed - it's actually doing CPU sampling with GPU processing
   - Called for every sensor refresh in GPU mode
   - Causes 0.2ms+ delay per sensor due to CPU-GPU sync

2. **`_sample_viewport_color()` in `light_data_sensor_3d.cpp:228`**
   ```cpp
   // PERFORMANCE WARNING: get_image() causes expensive CPU-GPU synchronization
   Ref<Image> img = tex->get_image();
   ```
   - Used in CPU fallback mode
   - Also called in refresh() method for immediate results

3. **Batch Compute Manager Fallback in `batch_compute_manager_macos.mm:353`**
   ```cpp
   // PERFORMANCE WARNING: get_image() causes expensive CPU-GPU synchronization
   Ref<Image> img = viewport_texture->get_image();
   ```
   - Fallback when direct texture access fails
   - Used in batch processing pipeline

### Current Frame Skipping Attempts

The code already attempts to mitigate this with frame skipping:
```cpp
// Frame skipping to reduce expensive get_image() calls
frame_skip_counter++;
if (frame_skip_counter < frame_skip_interval) {
    // Skip this frame, use cached data if available
    return;
}
```

However, this is insufficient for true GPU optimization.

## Optimization Strategy for M6.5

### 1. Eliminate `get_image()` in GPU Mode

**Goal**: Never call `get_image()` when GPU acceleration is available.

**Implementation**:
- Add platform detection to determine if GPU mode is active
- Implement direct GPU texture access for Metal and D3D12
- Use GPU-to-GPU operations without CPU synchronization

### 2. Direct GPU Texture Access

**Metal Implementation**:
- Use `MTLTexture` directly from ViewportTexture
- Implement GPU compute shaders that sample directly from the texture
- Avoid any CPU readback until final result

**D3D12 Implementation**:
- Use `ID3D12Resource` directly from ViewportTexture
- Implement compute shaders for direct texture sampling
- Use GPU-to-GPU operations

### 3. Alternative Sampling Strategies

**Option A: Direct Texture Access**
- Work directly with GPU texture resources
- Implement compute shaders for region sampling
- Use GPU memory operations for averaging

**Option B: Frame Buffer Capture**
- Capture viewport texture as GPU resource
- Process multiple sensors in single compute dispatch
- Batch multiple sensor operations

**Option C: RenderingDevice Integration**
- Use Godot's RenderingDevice for cross-platform GPU access
- Implement custom compute shaders
- Avoid platform-specific GPU code

### 4. Performance Targets

- **<0.2ms per sensor** in GPU mode
- **No `get_image()` calls** in GPU processing pipeline
- **Multiple sensors at 30Hz** without frame budget impact
- **Batch processing** for multiple sensors simultaneously

### 5. Implementation Plan

1. **Phase 1**: Add GPU mode detection and bypass `get_image()` calls
2. **Phase 2**: Implement direct texture access for Metal
3. **Phase 3**: Implement direct texture access for D3D12
4. **Phase 4**: Add batch processing optimization
5. **Phase 5**: Performance monitoring and profiling tools

### 6. Code Changes Required

**Files to modify**:
- `light_data_sensor_3d.cpp` - Add GPU mode detection
- `light_data_sensor_3d.h` - Add GPU optimization flags
- `platform/macos/batch_compute_manager_macos.mm` - Direct texture access
- `platform/windows/` - D3D12 direct texture access
- Add new GPU-only sampling methods

**New methods needed**:
- `_sample_gpu_direct()` - Direct GPU texture sampling
- `_is_gpu_mode_available()` - GPU mode detection
- `_batch_process_sensors_gpu()` - Batch GPU processing

### 7. Testing Strategy

- Performance benchmarks measuring <0.2ms per sensor
- Validation that no `get_image()` calls occur in GPU mode
- Multi-sensor stress tests at 30Hz
- GPU vs CPU path performance comparison

## Expected Results

After M6.5 implementation:
- GPU mode will achieve <0.2ms per sensor sampling time
- No `get_image()` calls in GPU processing pipeline
- Multiple sensors can run at 30Hz without frame budget impact
- Clear performance difference between GPU and CPU paths
- Scalable batch processing for multiple sensors
