extends SceneTree

# Runs the actual Steam Audio scene. One screenshot is a test-only GPU readback.
func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	# Godot 4.7's ReflectionProbe leaks seven RD textures even with MPVPlayer
	# removed. Keep that independent engine issue out of the player lifetime test.
	scene.get_node("ReflectionProbe").free()
	root.add_child(scene)
	var player: MPVPlayer = scene.get_node("MPVPlayer")
	var failed := false
	var captured := false
	var frame_hashes := {}
	var started := Time.get_ticks_msec()
	var elapsed := 0.0
	while elapsed < 16.0:
		await create_timer(1.0).timeout
		elapsed += 1.0
		var stats := player.get_audio_diagnostics()
		print("SMOKE ", JSON.stringify({"second": (Time.get_ticks_msec() - started) / 1000.0, "position": player.get_playback_position(),
			"duration": player.get_duration(), "video": player.get_video_status(), "audio": stats}))
		var live := player.output_texture.get_live_texture()
		if live != null and player.get_playback_position() > 0.5:
			var frame := live.get_image()
			if frame != null and not frame.is_empty():
				frame_hashes[frame.get_data().hex_encode().md5_text()] = true
		if not captured and player.get_playback_position() > 3.0:
			await RenderingServer.frame_post_draw
			var capture := OS.get_environment("LIBMPV_ZERO_SCREENSHOT")
			if not capture.is_empty():
				root.get_texture().get_image().save_png(capture)
			captured = true
		if player.get_duration() > 0 and player.get_playback_position() >= player.get_duration() - 0.1:
			break
	var final_audio := player.get_audio_diagnostics()
	failed = not captured or frame_hashes.size() < 2 or final_audio.channel_count != 2
	for count in final_audio.consumed_frames_per_channel:
		failed = failed or count < final_audio.sample_rate * 2
	print("SMOKE_RESULT ", "FAIL" if failed else "PASS", " ", JSON.stringify({
		"audio": final_audio, "distinct_video_frames": frame_hashes.size()}))
	scene.queue_free()
	await process_frame
	await process_frame
	quit(1 if failed else 0)
