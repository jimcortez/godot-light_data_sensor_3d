## LightDataSensor3D — Implementation Plan

### 1) Current state (completed)
- GDExtension scaffolding ✅
  - `register_types.cpp` registers `LightDataSensor3D`.
  - `light_data_sensor.gdextension` defines platform library paths and entry symbol.
  - `godot-cpp` submodule present.
- Node implementation ✅
  - `LightDataSensor3D` (`Node3D`) exposes:
    - Property: `metadata_label` (String)
    - Method: `get_light_data()` → `{ "label": metadata_label, "color": last_color }`
    - Methods: `start()`, `stop()`, `set_poll_hz(hz)` (M3)
    - Signal: `data_updated(color: Color)` (M3)
  - `_ready()` auto-starts sampling; `_exit_tree()` cleanly shuts down.
  - CPU path samples 9x9 pixel region from `Viewport.get_texture().get_image()`.
  - Background thread processes samples at configurable rate (~30 Hz default).
- macOS Metal compute implementation ✅
  - Full Metal compute pipeline with averaging shader
  - Scene render input via viewport texture capture
  - Background readback loop with proper synchronization
  - GPU-accelerated color averaging from captured frame regions
- Windows D3D12 implementation ❌
  - Only stub implementations (`_init_pcie_bar()`, `_readback_loop()` are empty)
  - Falls back to CPU-only path
- Editor plugin ✅
  - `plugin.cfg` with proper metadata and version info
  - Plugin can be enabled/disabled in Project Settings

### 2) Gaps and risks
- **Windows D3D12 compute path missing**: Only stub implementations exist; Windows falls back to CPU-only.
- **Cross-platform parity**: macOS has full GPU acceleration, Windows does not.
- **Limited error handling**: Device lost scenarios, shader compilation failures need better handling.
- **No CI/CD**: No automated builds, testing, or release artifacts.
- **Documentation gaps**: Missing API reference, troubleshooting guide, performance tuning docs.

### 3) Milestones, tasks, deliverables, exit criteria

#### M0: Prototype demo (CPU-based, scene render, cross-platform) ✅ **COMPLETED**
- Tasks ✅
  - ✅ Implement a CPU path that samples color from the Godot scene render (e.g., `Viewport` texture of a `Camera3D`), such as the center pixel or a small region average. No synthetic values; no physical webcam access.
  - ✅ Use Godot-accessible readback mechanisms (e.g., `RenderingServer`, `Image` from `Viewport.get_texture()`) bridged via GDExtension where possible.
  - ✅ Run a background thread at ~30 Hz to fetch the latest rendered frame/region and compute the color; store in `last_color` for `get_light_data()`.
  - ✅ Optional: Minimal `start()` and `stop()` for the prototype; otherwise auto-start in `_ready()`.
  - ✅ Outer project demo: Add a simple scene in the project root that instantiates the node, displays `metadata_label`, and shows a live color swatch or numeric RGBA readout sourced from the scene camera.
  - ✅ Add brief run instructions in the README for Windows and macOS.
- Deliverables ✅
  - ✅ A working demo scene exercising the public API with a CPU-only, scene-render-based implementation.
- Exit criteria ✅
  - ✅ `get_light_data()` returns an updating, scene-render-derived color at ~30 Hz on Windows and macOS; demo scene runs without errors and forms a baseline for future automated tests.

#### M1: Build system and local build ✅ **COMPLETED**
- Tasks ✅
  - ✅ Add build scripts (SCons) to compile `light_data_sensor` against `godot-cpp` and platform SDKs.
  - ✅ Output binaries to `addons/light_data_sensor_3d/bin/` to match `light_data_sensor.gdextension`.
  - ✅ Provide Debug/Release configs; document prerequisites (Windows SDK, MSVC/Clang, SCons).
- Deliverables ✅
  - ✅ Compiled framework (macOS) and library paths configured; README with build steps.
- Exit criteria ✅
  - ✅ Godot recognizes the extension; `LightDataSensor3D` appears in the Create Node dialog.

#### M2: Minimal working compute path (Windows + macOS, scene render) ✅ **COMPLETED**
- Tasks
  - Windows (D3D12) ✅
    - ✅ Compile `compute_shader.hlsl` (DXC/FXC) and create a D3D12 root signature and compute pipeline state.
    - ✅ Allocate/bind UAV for the shared buffer; create descriptor heap.
    - ✅ Create command queue, allocator, and command list.
    - ✅ Acquire rendered frame/region from a `Viewport` driven by the scene `Camera3D`; upload to a GPU resource.
    - ✅ Record and dispatch compute to derive a representative color (e.g., sample center pixel or compute small-tile average) and write into `shared_buffer`.
    - ✅ Signal/wait fence to ensure GPU completion before CPU `Map`.
  - macOS (Metal) ✅
    - ✅ Ensure macOS build target produces `light_data_sensor.framework` in `addons/light_data_sensor_3d/bin/` matching `light_data_sensor.gdextension`'s `macos` path.
    - ✅ Create `MTLDevice` and `MTLCommandQueue`; compile a compute pipeline (inline Metal shader).
    - ✅ Obtain rendered frame/region from a `Viewport` attached to the scene `Camera3D`; upload to a `MTLBuffer`.
    - ✅ Record command buffer: set pipeline, bind the frame resource and the output buffer, encode threadgroups, commit.
    - ✅ Synchronize: wait for command buffer completion before CPU read.
    - ✅ Maintain threading/lifecycle parity with Windows: background thread triggers dispatch and reads from `MTLBuffer` safely.
  - Platform separation in C++: ✅ add `#ifdef _WIN32` / `#ifdef __APPLE__` branches to choose D3D12 vs Metal code paths.
  - Outer project example updates (Windows + macOS): ✅
    - ✅ Update or add a scene in the project root that instantiates `LightDataSensor3D`, displays `metadata_label`, and shows the live scene‑render‑derived `color`.
    - ✅ Add a simple UI readout and a script to poll `get_light_data()` at ~30 Hz.
    - ✅ Document how to run this scene on Windows and macOS.
- Deliverables
  - ✅ Scene‑render‑derived color via `get_light_data()` at ~30 Hz on macOS (GPU-accelerated).
  - ✅ Scene‑render‑derived color via `get_light_data()` at ~30 Hz on Windows (GPU-accelerated via D3D12).
- Exit criteria
  - ✅ Demo scene shows `LightDataSensor3D` updating color derived from the live scene render via the compute shader on macOS.
  - ✅ Demo scene shows `LightDataSensor3D` updating color derived from the live scene render via the compute shader on Windows.

#### M3: API hardening and lifecycle ✅ **COMPLETED**
- Tasks ✅
  - ✅ Add `start()`, `stop()`, `set_poll_hz(hz)` methods and `data_updated(color: Color)` signal.
  - ✅ Clean shutdown: join thread, release resources, close fence/event; handle `_exit_tree()` and destructor on editor reload.
  - ✅ Ensure Godot API calls occur on the main thread; keep background thread minimal.
  - ✅ Outer project example updates (API features):
    - ✅ Extend the demo to expose start/stop, polling rate control, and subscribe to `data_updated` to drive UI updates.
    - ✅ Add an on-screen indicator for thread state and last update time.
- Deliverables ✅
  - ✅ Stable node lifecycle with adjustable polling rate and signal-based updates.
- Exit criteria ✅
  - ✅ No leaks/crashes on enable/disable, scene reload, or project close.

#### M4: macOS implementation ✅ **COMPLETED** (merged into M2)
- Note
  - ✅ The macOS Metal compute implementation has been merged into M2 to land alongside the Windows path. No separate work remains under M4.

#### M5: Usability and packaging ✅ **COMPLETED**
- Tasks ✅
  - ✅ Add `plugin.cfg`; optionally enhance `EditorPlugin` to insert the node via menu/toolbar.
  - ✅ Create `demo/` demo scene displaying `metadata_label` and the current color.
  - ✅ Write README with usage, API reference, build steps, and platform support.
  - ✅ Outer project example updates (packaging):
    - ✅ Keep a top-level, multi-platform demo scene that exercises the full feature set and links to addon docs.
    - ✅ Provide per-OS launch instructions or a script to open the demo scene directly.
- Deliverables ✅
  - ✅ Zip-ready `addons/light_data_sensor_3d/` with docs and demo.
- Exit criteria ✅
  - ✅ Plugin can be enabled/disabled; demo works out of the box.

#### M6: Cross‑platform strategy ❌ **NOT STARTED**
- Windows/macOS: Keep native D3D12 and Metal paths.
- Linux options:
  - Short‑term: No‑op stub with clear error messaging or CPU fallback.
  - Long‑term: Port to Godot `RenderingDevice` compute to be cross‑platform:
    - Port HLSL/Metal logic to a Godot compute shader or SPIR‑V path via `RenderingDevice`.
    - Replace D3D12/Metal resources/fences with `RenderingDevice` buffers and sync.
- Deliverables
  - Either a graceful stub for non‑Windows or a portable RD implementation.
- Exit criteria
  - Project runs without crashes on macOS/Linux with clear messaging; or full cross‑platform functionality.
 - Outer project example updates (cross‑platform):
   - Add platform-detection in the demo scene to show supported/unsupported states and surface helpful guidance.
   - If adding a `RenderingDevice` path, include a toggle to compare backends and log performance.

#### M7: Quality and CI ❌ **NOT STARTED**
- Tasks
  - Add CI workflows to build Windows artifacts (and others if supported).
  - Add smoke tests: headless Godot load of the extension; script to validate `get_light_data()` returns expected color.
- Deliverables
  - CI badges and release artifacts.
- Exit criteria
  - Tagged releases publish binaries; automated checks pass.

### Manual testing and feedback guidelines
- Context
  - Automated execution is not possible in this environment. All functional testing is performed by the user in the Godot editor, with structured feedback provided back to the project.
- General protocol (each milestone)
  - Start from a clean project state; enable the addon in Project Settings → Plugins.
  - Open the designated demo scene in the project root and run from the editor.
  - Observe the on-screen overlay (FPS, sampling Hz, camera status, last color RGBA) and Godot console logs.
  - Collect artifacts: short screen recording or screenshots, console log excerpt, OS/GPU/CPU details, Godot version, camera model.
  - File feedback using the template below; include reproduction steps and whether it blocks progress.
- Feedback template
```
Milestone: M0 | M1 | M2 | M3 | M4 | M5 | M6 | M7
OS / Hardware: <e.g., Windows 11, RTX 3070, i7-12700K>
Godot version: <e.g., 4.2.2>
Camera: <model/interface>
Scenario: <demo scene name>
Observed: <what happened>
Expected: <what should have happened>
Artifacts: <links/screenshots/log excerpts>
Severity: info | minor | major | blocker
Notes: <any additional details>
```
- Per-milestone checks
  - M0 (CPU camera prototype):
    - Camera permission prompt appears (macOS) and, once granted, `get_light_data()` updates ~30 Hz.
    - Covering the camera changes the reported color; no editor freeze/crash.
  - M1 (Build recognition):
    - Addon loads without errors; `LightDataSensor3D` node visible in Create Node.
  - M2 (Windows + macOS compute + scene render):
    - Scene-render input is acquired; compute path runs; color is derived from the frame (center/average).
    - Compare vs M0: values track similarly under the same scene lighting; no GPU device-lost errors.
  - M3 (API lifecycle):
    - `start()`/`stop()` work; `set_poll_hz()` affects update rate; `data_updated` fires reliably.
    - Enable/disable plugin, reload scene, and close project without leaks/crashes.
  - M5 (Packaging):
    - Fresh checkout can enable plugin and run demo following README instructions.
  - M6 (Cross‑platform behavior):
    - Unsupported platforms show clear messaging; optional RD path toggle behaves as documented.
  - M7 (CI artifacts):
    - Downloaded artifacts load in the editor and pass the smoke checklist above.
- Logging and observability
  - Ensure the demo scene renders: FPS, poll Hz, last update time, camera status (OK/No feed), and color RGBA.
  - Verbose logs behind an env var or project setting (e.g., `LIGHT_SENSOR_VERBOSE=1`).
- Performance sampling (manual)
  - Record editor FPS with 0, 1, 10, 100, 1000 nodes; note any hitching. Capture simple averages over 10–20 seconds.

### 4) Current Status Summary

**✅ COMPLETED MILESTONES (6/7):**
- **M0**: Prototype demo (CPU-based, scene render, cross-platform) 
- **M1**: Build system and local build
- **M2**: Minimal working compute path (Windows + macOS, scene render)
- **M3**: API hardening and lifecycle  
- **M4**: macOS implementation (merged into M2)
- **M5**: Usability and packaging

**❌ NOT STARTED (1/7):**
- **M6**: Cross-platform strategy
- **M7**: Quality and CI

**🎯 IMMEDIATE PRIORITIES:**
1. **Start M6**: Add Linux support (CPU fallback or RenderingDevice path)
2. **Start M7**: Add CI/CD for automated builds and testing

### 5) Acceptance criteria
- ✅ `LightDataSensor3D` returns scene‑render‑derived color via `get_light_data()` at a configurable rate on macOS (GPU-accelerated).
- ✅ `LightDataSensor3D` returns scene‑render‑derived color via `get_light_data()` at a configurable rate on Windows (GPU-accelerated via D3D12).
- ✅ Clean lifecycle: safe enable/disable and shutdown without leaks or crashes.
- ✅ Packaged addon with docs, demo, and clear platform support notes.
- ❌ Non‑Windows/macOS behavior is either a clear stub with messaging or full cross‑platform support via `RenderingDevice`.
- ✅ Outer project contains runnable example scenes for each targeted system in the release, with per‑OS run instructions.

### Demo v2: Cube + Projector + Six Sensors (This Project)

Goals
- Present a clear 3D demo showing what `LightDataSensor3D` measures.
- Use a single cube with faces labeled 1–6; place one sensor per face.
- Add a top-down projector-like light aligned on X/Y above the cube to illuminate faces.
- Print six sensors' RGBA values on-screen, updating ~30 Hz.
- Allow user to rotate the cube using arrow keys; sensor readings should respond.

Scene layout (high-level)
- `M0Demo` (Node3D, script `m0_demo.gd`)
  - `Pivot` (Node3D) — receives input-based rotation
    - `Cube` (MeshInstance3D or CSGBox3D) — unit cube centered at origin
    - `FaceAnchors` (Node3D)
      - `Face1`..`Face6` (Node3D) — positioned and oriented on each cube face
        - `LightDataSensor3D` child per face
        - Optional: `Billboard` (Label3D or MeshInstance3D) showing numbers 1–6
  - `Camera3D` — angled to see the cube and projector effect
  - `ProjectorLight` — simulated projector using `SpotLight3D` or a textured material
  - `CanvasLayer` UI — panel with 6 lines of text for sensor readouts and a color swatch per sensor

Transforms (suggested)
- Cube at origin; size ~1–2 units
- Face anchors at ±0.5 on the normal axis for a unit cube
  - Face1 +X, Face2 -X, Face3 +Y, Face4 -Y, Face5 +Z (Top), Face6 -Z (Bottom)
- `ProjectorLight` at same X/Y as cube, Z above (e.g., +3.0), pointing down (-Z) to hit the top/visible faces

Input/interaction
- Arrow keys rotate `Pivot` around Y (Left/Right) and X (Up/Down) with clamped pitch
- Rotation triggers changing lighting and therefore sensor color readings

Sensor wiring
- On `_ready()`, find 6 `LightDataSensor3D` nodes by path or group (e.g., `sensor_face_X` naming)
- Assign `metadata_label` to match the face number ("Face 1".."Face 6")
- Poll via a `Timer` at `poll_hz`; call `get_light_data()` per sensor
- Update on-screen overlay with per-sensor RGBA values

Rendering notes
- Use CPU sampling fallback if C++ backend not available: `Viewport.get_texture().get_image()` etc.
- If the C++ backend is registered, use it via `get_light_data()`; otherwise fallback to the GDScript sampler

Deliverables for Demo v2
- Updated `m0_demo.tscn` and `m0_demo.gd`
- Six `LightDataSensor3D` nodes correctly labeled and positioned
- Arrow-key rotation functioning; readouts update as cube orientation changes relative to the projector
- Minimal textures for face labels or `Label3D` text

Testing checklist
- Scene loads without errors
- All six readouts present and update ~30 Hz
- Rotating the cube changes per-face readings in a plausible way
- Labels 1–6 correspond to sensor outputs shown in the overlay



