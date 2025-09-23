extends Node

# Phase 1 Direct Texture Access Test
# This script tests the new direct texture access functionality in BatchComputeManager

var batch_compute_manager: BatchComputeManager
var test_viewport: SubViewport
var test_camera: Camera3D
var test_mesh: MeshInstance3D

func _ready():
	print("[Phase1Test] Starting Phase 1 Direct Texture Access Tests")
	
	# Create test components
	_setup_test_environment()
	
	# Run tests
	_test_direct_texture_access_configuration()
	_test_fallback_mechanism()
	_test_texture_creation_methods()
	
	print("[Phase1Test] All Phase 1 tests completed")

func _setup_test_environment():
	# Create BatchComputeManager
	batch_compute_manager = BatchComputeManager.new()
	add_child(batch_compute_manager)
	
	# Initialize the manager
	if not batch_compute_manager.initialize():
		print("[Phase1Test] ERROR: Failed to initialize BatchComputeManager")
		return
	
	# Create test viewport
	test_viewport = SubViewport.new()
	test_viewport.size = Vector2i(1920, 1080)
	add_child(test_viewport)
	
	# Create test camera
	test_camera = Camera3D.new()
	test_viewport.add_child(test_camera)
	
	# Create test mesh with material
	test_mesh = MeshInstance3D.new()
	var box_mesh = BoxMesh.new()
	test_mesh.mesh = box_mesh
	
	# Create a simple material
	var material = StandardMaterial3D.new()
	material.albedo_color = Color.RED
	test_mesh.material_override = material
	
	test_viewport.add_child(test_mesh)
	
	# Position camera to see the mesh
	test_camera.position = Vector3(0, 0, 5)
	test_camera.look_at(Vector3.ZERO, Vector3.UP)

func _test_direct_texture_access_configuration():
	print("[Phase1Test] Testing direct texture access configuration...")
	
	# Test default configuration
	var default_direct_access = batch_compute_manager.use_direct_texture_access
	print("[Phase1Test] Default use_direct_texture_access: ", default_direct_access)
	
	# Test setting direct texture access
	batch_compute_manager.set_use_direct_texture_access(false)
	var disabled_direct_access = batch_compute_manager.use_direct_texture_access
	print("[Phase1Test] Disabled use_direct_texture_access: ", disabled_direct_access)
	
	# Test re-enabling direct texture access
	batch_compute_manager.set_use_direct_texture_access(true)
	var enabled_direct_access = batch_compute_manager.use_direct_texture_access
	print("[Phase1Test] Re-enabled use_direct_texture_access: ", enabled_direct_access)
	
	# Verify configuration changes
	assert(disabled_direct_access == false, "Failed to disable direct texture access")
	assert(enabled_direct_access == true, "Failed to enable direct texture access")
	
	print("[Phase1Test] ✓ Direct texture access configuration test passed")

func _test_fallback_mechanism():
	print("[Phase1Test] Testing fallback mechanism...")
	
	# Add a test sensor
	batch_compute_manager.add_sensor(1, 960, 540, 4)  # Center of 1920x1080 viewport
	
	# Get viewport texture
	var viewport_texture = test_viewport.get_texture()
	if viewport_texture == null:
		print("[Phase1Test] ERROR: Failed to get viewport texture")
		return
	
	# Test with direct texture access enabled (should fallback to get_image())
	batch_compute_manager.set_use_direct_texture_access(true)
	var result_direct = batch_compute_manager.process_sensors(viewport_texture)
	print("[Phase1Test] Direct texture access result: ", result_direct)
	
	# Test with direct texture access disabled (should use get_image())
	batch_compute_manager.set_use_direct_texture_access(false)
	var result_fallback = batch_compute_manager.process_sensors(viewport_texture)
	print("[Phase1Test] Fallback method result: ", result_fallback)
	
	# Both should succeed (fallback should work)
	assert(result_direct == true, "Direct texture access path failed")
	assert(result_fallback == true, "Fallback method failed")
	
	# Get sensor results
	var sensor_result = batch_compute_manager.get_sensor_result(1)
	print("[Phase1Test] Sensor result: ", sensor_result)
	
	# Verify we got a valid color (not black)
	assert(sensor_result != Color.BLACK, "Sensor returned invalid color")
	
	print("[Phase1Test] ✓ Fallback mechanism test passed")

func _test_texture_creation_methods():
	print("[Phase1Test] Testing texture creation methods...")
	
	# Test with multiple sensors
	var sensor_positions = [
		Vector2(100, 100),
		Vector2(500, 300),
		Vector2(800, 600),
		Vector2(1200, 400),
		Vector2(1600, 800)
	]
	
	# Add multiple sensors
	for i in range(sensor_positions.size()):
		batch_compute_manager.add_sensor(i + 10, sensor_positions[i].x, sensor_positions[i].y, 4)
	
	print("[Phase1Test] Added ", sensor_positions.size(), " test sensors")
	
	# Get viewport texture
	var viewport_texture = test_viewport.get_texture()
	if viewport_texture == null:
		print("[Phase1Test] ERROR: Failed to get viewport texture")
		return
	
	# Test processing with both methods
	batch_compute_manager.set_use_direct_texture_access(true)
	var start_time_direct = Time.get_ticks_msec()
	var result_direct = batch_compute_manager.process_sensors(viewport_texture)
	var end_time_direct = Time.get_ticks_msec()
	
	batch_compute_manager.set_use_direct_texture_access(false)
	var start_time_fallback = Time.get_ticks_msec()
	var result_fallback = batch_compute_manager.process_sensors(viewport_texture)
	var end_time_fallback = Time.get_ticks_msec()
	
	print("[Phase1Test] Direct access processing time: ", end_time_direct - start_time_direct, "ms")
	print("[Phase1Test] Fallback processing time: ", end_time_fallback - start_time_fallback, "ms")
	
	# Both should succeed
	assert(result_direct == true, "Direct texture access processing failed")
	assert(result_fallback == true, "Fallback processing failed")
	
	# Verify all sensors have results
	for i in range(sensor_positions.size()):
		var sensor_id = i + 10
		var result = batch_compute_manager.get_sensor_result(sensor_id)
		assert(result != Color.BLACK, "Sensor " + str(sensor_id) + " returned invalid color")
	
	print("[Phase1Test] ✓ Texture creation methods test passed")

func _exit_tree():
	# Cleanup
	if batch_compute_manager:
		batch_compute_manager.shutdown()
		batch_compute_manager.queue_free()
