extends "res://tests/transport.gd"

# Stall only the two Godot consumers, without pausing mpv. A timer-driven AO
# keeps advancing and accumulating PCM; a consumer-driven clock stops at its
# bounded queue and resumes without replaying an ever-growing backlog.
func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	root.add_child(scene)
	player = scene.get_node("MPVPlayer")
	player.playback_error.connect(func(message: String): errors.append(message))
	check(await wait_for(func(): return player.get_playback_position() > 1.0), "initial playback")
	for cycle in range(3):
		for name in ["LeftSpeaker", "RightSpeaker"]:
			scene.get_node("EmissiveScreen/" + name).stream_paused = true
		await create_timer(0.25).timeout
		var position := player.get_playback_position()
		var before := player.get_audio_diagnostics()
		await create_timer(0.6).timeout
		var after := player.get_audio_diagnostics()
		check(absf(player.get_playback_position() - position) < 0.15, "mpv clock advances while consumers are stalled")
		check(after.max_queued_frames <= after.capacity_frames, "audio exceeds its capacity")
		check(after.consumed_frames_per_channel == before.consumed_frames_per_channel, "stalled consumer reads audio")
		print("CONSUMER_STALL ", JSON.stringify({"cycle": cycle, "position_delta": player.get_playback_position() - position, "audio": after}))
		for name in ["LeftSpeaker", "RightSpeaker"]:
			scene.get_node("EmissiveScreen/" + name).stream_paused = false
		check(await wait_for(func(): return player.get_playback_position() > position + 0.5), "playback recovers after stall")
	player.stop()
	scene.queue_free()
	await create_timer(0.25).timeout
	print("SMOKE_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify({"errors": errors}))
	quit(0 if errors.is_empty() else 1)
