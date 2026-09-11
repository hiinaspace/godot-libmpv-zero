extends SceneTree

var errors: Array[String] = []
var player: MPVPlayer

func _initialize() -> void:
	_run.call_deferred()

func check(condition: bool, message: String) -> void:
	if not condition:
		errors.append(message)
		print("TRANSPORT_FAILURE ", message)

func wait_for(condition: Callable, seconds: float = 8.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000)
	while Time.get_ticks_msec() < deadline:
		if condition.call():
			return true
		await process_frame
	return false

func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	scene.get_node("ReflectionProbe").free() # Independent engine texture leak.
	root.add_child(scene)
	player = scene.get_node("MPVPlayer")
	player.playback_error.connect(func(message: String): errors.append(message))
	check(await wait_for(func(): return player.get_playback_position() > 1.0), "initial playback")
	for cycle in range(4):
		player.pause()
		check(await wait_for(func(): return player.is_paused()), "pause acknowledged")
		await create_timer(0.15).timeout
		var paused_position := player.get_playback_position()
		var before_pause := player.get_audio_diagnostics()
		await create_timer(0.5).timeout
		check(absf(player.get_playback_position() - paused_position) < 0.05, "position moves while paused")
		check(player.get_audio_diagnostics().consumed_frames_per_channel == before_pause.consumed_frames_per_channel,
			"audio consumed while paused")

		player.seek(2.0)
		check(await wait_for(func(): return absf(player.get_playback_position() - 2.0) < 0.2), "paused seek")
		check(player.is_paused(), "seek unexpectedly resumes playback")
		player.play()
		check(await wait_for(func(): return player.get_playback_position() > 2.8), "resume advances")
		player.seek(0.5)
		check(await wait_for(func(): return player.get_playback_position() < 1.2), "playing seek backwards")
		check(await wait_for(func(): return player.get_playback_position() > 1.5), "playback after seek")
		var audio := player.get_audio_diagnostics()
		check(audio.channel_count == 2, "stereo channel configuration")
		check(abs(audio.consumed_frames_per_channel[0] - audio.consumed_frames_per_channel[1]) <= 512,
			"channel clocks diverged")
		print("TRANSPORT ", JSON.stringify({"cycle": cycle, "position": player.get_playback_position(), "audio": audio}))
		player.stop()
		await create_timer(0.15).timeout
		player.load(OS.get_environment("LIBMPV_ZERO_MEDIA"))
		player.play()
		check(await wait_for(func(): return player.get_playback_position() > 1.0 and player.is_playing()), "reload plays")

	# AudioServer retires stopped playbacks asynchronously. Test their release
	# while the engine is running separately from immediate application shutdown.
	var retired: Array[WeakRef] = []
	for name in ["LeftSpeaker", "RightSpeaker"]:
		var speaker: Node = scene.get_node("EmissiveScreen/" + name)
		retired.append(weakref(speaker.get_stream_playback()))
		retired.append(weakref(speaker.get_inner_stream_playback()))
	scene.queue_free()
	check(await wait_for(func(): return retired.all(func(ref): return ref.get_ref() == null), 2.0), "audio playback objects were not released")
	await process_frame
	print("SMOKE_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify({"errors": errors}))
	quit(0 if errors.is_empty() else 1)
