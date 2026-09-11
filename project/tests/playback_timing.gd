extends "res://tests/transport.gd"

var bright := false
var flashes := 0
var max_readback_seconds := 0.0

func sample_video() -> void:
	var live := player.output_texture.get_live_texture()
	if live == null:
		return
	var before := Time.get_unix_time_from_system()
	var frame := live.get_image()
	var after := Time.get_unix_time_from_system()
	max_readback_seconds = maxf(max_readback_seconds, after - before)
	if frame == null or frame.is_empty():
		return
	var color := frame.get_pixel(frame.get_width() / 2, frame.get_height() / 2)
	var now_bright := color.r > 0.8 and color.g > 0.8 and color.b > 0.8
	if now_bright and not bright:
		flashes += 1
		print("AV_FLASH ", JSON.stringify({"time": (before + after) / 2, "position": player.get_playback_position()}))
		if flashes == 3:
			frame.save_png(OS.get_environment("LIBMPV_ZERO_SCREENSHOT").get_base_dir().path_join("video.png"))
	bright = now_bright

func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	scene.get_node("ReflectionProbe").free()
	root.add_child(scene)
	player = scene.get_node("MPVPlayer")
	player.playback_error.connect(func(message: String): errors.append(message))
	check(await wait_for(func(): return player.get_playback_position() > 0.5), "initial playback")
	var seconds := float(OS.get_environment("LIBMPV_ZERO_TEST_SECONDS"))
	if seconds <= 0:
		seconds = 30
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	var next_report := Time.get_ticks_msec()
	var next_stall := Time.get_ticks_msec()
	var stalls := 0
	var inject_stalls := OS.get_environment("LIBMPV_ZERO_TEST_RENDER_STALLS") == "1"
	while Time.get_ticks_msec() < deadline:
		await RenderingServer.frame_post_draw
		sample_video()
		var phase := fmod(player.get_playback_position(), 1.0)
		if inject_stalls and Time.get_ticks_msec() >= next_stall and phase > 0.3 and phase < 0.5:
			# Delay only Godot's main/render thread between reference flashes.
			# The audio mixer, capture process and development tools keep running.
			var milliseconds: int = [50, 100, 200][stalls % 3]
			print("RENDER_STALL ", milliseconds)
			OS.delay_msec(milliseconds)
			stalls += 1
			next_stall = Time.get_ticks_msec() + 2500
		if Time.get_ticks_msec() >= next_report:
			next_report += 1000
			print("TIMING ", JSON.stringify({"position": player.get_playback_position(), "audio": player.get_audio_diagnostics(),
				"fps": Engine.get_frames_per_second(), "process_frames": Engine.get_process_frames(),
				"static_memory": OS.get_static_memory_usage()}))
	check(flashes >= seconds * 0.75, "missing video flashes")
	if inject_stalls:
		check(stalls >= int(seconds / 4), "render stalls were not exercised")
	print("SMOKE_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify({"errors": errors,
		"video_flashes": flashes, "render_stalls": stalls, "max_readback_seconds": max_readback_seconds}))
	scene.queue_free()
	await create_timer(0.25).timeout
	quit(0 if errors.is_empty() else 1)
