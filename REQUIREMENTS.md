# LightDataSensor3D - Requirements Specification

Purpose: Machine-readable requirements for the `LightDataSensor3D` addon. For roadmap, status, and sequencing, see `addons/light_data_sensor_3d/IMPLEMENTATION_PLAN.md`.

## Scope
- Addon: `LightDataSensor3D`
- Engine: Godot 4.x (GDExtension, `godot-cpp`)
- Function: Expose scene-render-derived color data (from a Godot `Camera3D`/`Viewport`) via a `Node3D` API

## References
- Implementation Plan: `addons/light_data_sensor_3d/IMPLEMENTATION_PLAN.md`

## Legend
- ID prefixes: FR (Functional), PR (Platform), BR (Build/Packaging), NFR (Non-functional), XR (Examples/Demos), TR (Testing/CI)
- Status values: Current, Planned, Future
- Plan: Milestone reference from the Implementation Plan (e.g., M2)

## Terminology
- "Camera" refers to a Godot scene camera (`Camera3D`) or rendered `Viewport`, not the user's physical webcam or imaging device.

## Functional Requirements
- FR-001 (Status: ✅ **Current**, Plan: M1/M2)
  - ✅ Provide `Node3D` class `LightDataSensor3D` with property `metadata_label: String` and method `get_light_data() -> Dictionary` returning keys: `label: String`, `color: Color`.
- FR-002 (Status: ✅ **Current**, Plan: M2/M4)
  - ✅ Produce color derived from the Godot scene render output (not a physical device camera). GPU path computes on a rendered frame/texture and writes to a single-pixel RGBA32F buffer for readback; CPU prototype samples from a `Viewport`/`Camera3D` readback.
  - ✅ **macOS**: Full Metal GPU compute implementation
  - ✅ **Windows**: Full D3D12 GPU compute implementation
- FR-003 (Status: ✅ **Current**, Plan: M3)
  - ✅ Expose control methods: `start()`, `stop()`, and `set_poll_hz(hz: float)`.
- FR-004 (Status: ✅ **Current**, Plan: M3)
  - ✅ Emit `data_updated(color: Color)` signal when a new sample is available.
- FR-005 (Status: ✅ **Current**, Plan: M5)
  - ✅ Optional `EditorPlugin` affordance to insert `LightDataSensor3D` into a scene.

## Platform Requirements
- PR-001 Windows (Status: ✅ **Current**, Plan: M1/M2)
  - ✅ Direct3D 12 compute pipeline: root signature, descriptor heap, UAV buffer, command queue/list, fence sync.
- PR-002 macOS (Status: ✅ **Current**, Plan: M4)
  - ✅ Metal compute pipeline: `MTLDevice`, `MTLCommandQueue`, compute pipeline state, `MTLBuffer` (shared), command buffer sync.
- PR-003 Non-Windows/macOS behavior (Status: ❌ **Not Implemented**, Plan: M6)
  - ❌ Graceful no-op stub or documented limitation; must not crash. If cross-platform via `RenderingDevice` is implemented, supersedes stub.
- PR-004 Binary placement (Status: ✅ **Current**, Plan: M1/M4)
  - ✅ Artifacts placed at `addons/light_data_sensor_3d/bin/` and referenced by `light_data_sensor.gdextension` `library_path` per OS.

## Build and Packaging Requirements
- BR-001 Build scripts (Status: ✅ **Current**, Plan: M1/M4)
  - ✅ Scripts/project files to build Windows `.dll` and macOS `.framework` against `godot-cpp` and platform SDKs.
- BR-002 Configurations (Status: ✅ **Current**, Plan: M1)
  - ✅ Support Debug and Release; document prerequisites and environment setup.
- BR-003 Addon packaging (Status: ✅ **Current**, Plan: M5)
  - ✅ Include `plugin.cfg`, icons, and zip-ready `addons/light_data_sensor_3d/` structure.
- BR-004 Documentation (Status: ✅ **Current**, Plan: M5)
  - ✅ README covering usage, API, build steps, platform support matrix, troubleshooting.

## Non-Functional Requirements
- NFR-001 Sampling rate (Status: ✅ **Current**, Plan: M3)
  - ✅ Default ~30 Hz; configurable via `set_poll_hz` with stable timing (±10%).
- NFR-002 Lifecycle safety (Status: ✅ **Current**, Plan: M3)
  - ✅ Clean start/stop; join background thread; release GPU resources; safe editor reload; no leaks.
- NFR-003 Thread safety (Status: ✅ **Current**, Plan: M3)
  - ✅ Avoid Godot API calls off the main thread; use signals/atomic data for hand-off.
- NFR-004 Efficiency (Status: ✅ **Current**, Plan: M2/M3)
  - ✅ Background loop sleeps between samples; minimal CPU overhead at default rate.
- NFR-005 Error handling (Status: 🔄 **Partially Current**, Plan: M2/M3)
  - ✅ Clear logging for failures (device creation, shader compile, dispatch, mapping); safe defaults.
  - ❌ Device lost scenarios and advanced error recovery not fully implemented.
 - NFR-006 Scalability (Status: ✅ **Current**, Plan: M3/M5)
  - ✅ Support thousands of `LightDataSensor3D` nodes in a single project with negligible impact on overall frame time (target: ≤5% increase versus baseline scene at default 30 Hz sampling on a mid‑range GPU/CPU). Allow batching/shared GPU resources where applicable.

## Examples and Demos (Outer Project)
- XR-001 Windows demo (Status: ✅ **Current**, Plan: M2)
  - ✅ Runnable scene in project root instantiating `LightDataSensor3D`, showing `metadata_label`, and live `color`; Windows run steps documented. GPU-accelerated via D3D12 compute.
- XR-002 API demo features (Status: ✅ **Current**, Plan: M3)
  - ✅ UI controls for `start/stop`, polling rate; subscribes to `data_updated`; shows thread state and last update timestamp.
- XR-003 macOS demo (Status: ✅ **Current**, Plan: M4)
  - ✅ Same scene runs on macOS; Windows-only controls gated; macOS run steps (Apple Silicon/Intel) documented.
- XR-004 Packaging demo (Status: ✅ **Current**, Plan: M5)
  - ✅ Top-level multi-platform demo scene with links to addon docs; per-OS launch instructions or helper scripts.
- XR-005 Cross-platform behavior (Status: ❌ **Not Implemented**, Plan: M6)
  - ❌ Platform detection in demo; communicates unsupported states; if `RenderingDevice` path exists, toggle to compare backends and log performance.

## Testing and CI
- TR-001 Smoke load (Status: ❌ **Not Implemented**, Plan: M7)
  - ❌ Headless Godot launch that loads the addon without errors on each target OS.
- TR-002 Functional check (Status: ❌ **Not Implemented**, Plan: M7)
  - ❌ Automated script validates `get_light_data()` returns expected color from a controlled scene (e.g., solid-color quad) within a timeout.
- TR-003 Release artifacts (Status: ❌ **Not Implemented**, Plan: M7)
  - ❌ CI builds and publishes per-OS binaries on tagged releases; include checksums.

## Dependencies
- Godot 4.x matching `godot-cpp` version.
- Windows: Windows 10/11, D3D12-capable GPU, Windows SDK, MSVC/Clang.
- macOS: macOS 12+ (Monterey) or later, Xcode with Metal SDK, Apple Silicon or Intel with Metal support.

## Constraints
- Single-pixel RGBA32F buffer for MVP; extensible later.
- Windows uses D3D12; macOS uses Metal; shared C++ interface with platform branches (`_WIN32`, `__APPLE__`).

## Risks
- Device/driver variability; shader compiler differences across platforms.
- Threading and shutdown ordering issues within editor lifecycle.
- Cross-platform parity if/when abstracted to `RenderingDevice`.

## Acceptance Criteria (summary)
- AC-001: ✅ **macOS builds load in Godot; node appears and functions.** ✅ **Windows builds load and GPU acceleration works via D3D12.**
- AC-002: ✅ **`get_light_data()` yields scene-render-derived color at a configurable rate; per-OS demos show live updates.** (macOS GPU-accelerated via Metal, Windows GPU-accelerated via D3D12)
- AC-003: ✅ **Clean lifecycle (no leaks/crashes) across enable/disable, scene reload, project shutdown.**
- AC-004: ✅ **Packaged addon with docs and runnable per-OS example scenes.**
- AC-005: ❌ **Non-targeted platforms fail gracefully or use documented cross-platform path.**
- AC-006: ✅ **Scalability: a test scene with thousands of nodes maintains frame time within the defined threshold compared to a baseline scene on target hardware.**

## Traceability to Implementation Plan
- **M1 ✅** -> BR-001, BR-002, PR-004, partial FR-001
- **M2 ✅** -> FR-002, NFR-004, NFR-005, XR-001 (macOS ✅, Windows ✅)
- **M3 ✅** -> FR-003, FR-004, NFR-001, NFR-002, NFR-003, XR-002
- **M4 ✅** -> PR-002, XR-003 (merged into M2)
- **M5 ✅** -> BR-003, BR-004, XR-004, FR-005
- **M6 ❌** -> PR-003, XR-005
- **M7 ❌** -> TR-001, TR-002, TR-003
