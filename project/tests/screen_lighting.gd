extends "res://tests/transport.gd"

func sample_screen(scene: Node, image: Image) -> Array[Color]:
	var camera: Camera3D = root.get_camera_3d()
	var screen: MeshInstance3D = scene.get_node("EmissiveScreen")
	var size: Vector2 = screen.mesh.size
	var samples: Array[Color] = []
	for y in range(9):
		for x in range(15):
			var point := screen.to_global(Vector3((x / 14.0 - 0.5) * size.x * 0.96, (y / 8.0 - 0.5) * size.y * 0.96, 0))
			var pixel := Vector2i(camera.unproject_position(point))
			if Rect2i(Vector2i.ZERO, image.get_size()).has_point(pixel):
				samples.append(image.get_pixelv(pixel))
	return samples

func capture() -> Image:
	for frame in range(5):
		await process_frame
	await RenderingServer.frame_post_draw
	return root.get_texture().get_image()

func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	root.add_child(scene)
	player = scene.get_node("MPVPlayer")
	player.playback_error.connect(func(message: String): errors.append(message))
	check(await wait_for(func(): return player.get_playback_position() > 3.0), "initial playback")
	player.pause()
	check(await wait_for(func(): return player.is_paused()), "pause acknowledged")
	await create_timer(0.5).timeout
	var environment: Environment = scene.get_node("WorldEnvironment").environment
	# Isolate direct lighting from bloom leaking in from illuminated neighbors.
	var glow := environment.glow_enabled
	environment.glow_enabled = false
	var area: AreaLight3D = scene.get_node("EmissiveScreen/AreaLight3D")
	var directional: DirectionalLight3D = scene.get_node("DirectionalLight3D")
	var area_energy := area.light_energy
	var directional_energy := directional.light_energy
	area.light_energy = 0
	directional.light_energy = 0
	var before := await capture()
	var screen_before := sample_screen(scene, before)
	area.light_energy = 10
	directional.light_energy = 2
	var after := await capture()
	var screen_after := sample_screen(scene, after)
	check(screen_before.size() == 135 and screen_after.size() == 135, "screen samples outside viewport")
	var maximum_change := 0.0
	for i in range(mini(screen_before.size(), screen_after.size())):
		var difference := screen_after[i] - screen_before[i]
		maximum_change = maxf(maximum_change, maxf(absf(difference.r), maxf(absf(difference.g), absf(difference.b))))
	check(maximum_change < 0.01, "scene lighting changed the paused screen")
	# The area light alone must change room pixels, not just exist as a node.
	directional.light_energy = 0
	var area_only := await capture()
	var changed_pixels := 0
	for y in range(0, before.get_height(), 8):
		for x in range(0, before.get_width(), 8):
			var difference := area_only.get_pixel(x, y) - before.get_pixel(x, y)
			if maxf(absf(difference.r), maxf(absf(difference.g), absf(difference.b))) > 0.02:
				changed_pixels += 1
	check(changed_pixels > 10, "area light did not illuminate the room")
	area.light_energy = area_energy
	directional.light_energy = directional_energy
	environment.glow_enabled = glow
	var final_frame := await capture()
	final_frame.save_png(OS.get_environment("LIBMPV_ZERO_SCREENSHOT"))
	print("SMOKE_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify({
		"errors": errors, "screen_lighting_change": maximum_change, "area_lit_samples": changed_pixels}))
	scene.queue_free()
	await create_timer(0.25).timeout
	quit(0 if errors.is_empty() else 1)
