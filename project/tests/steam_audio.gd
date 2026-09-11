extends SceneTree

var errors: Array[String] = []

func check(ok: bool, message: String) -> void:
	if not ok:
		errors.append(message)

func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	var room := Node3D.new()
	root.add_child(room)
	room.add_child(ClassDB.instantiate("SteamAudioConfig"))
	var listener: Node3D = ClassDB.instantiate("SteamAudioListener")
	room.add_child(listener)
	var speaker: AudioStreamPlayer3D = ClassDB.instantiate("SteamAudioPlayer")
	speaker.stream_paused = true
	speaker.set("ambisonics", false)
	speaker.set("reflection", false)
	speaker.set("occlusion", false)
	room.add_child(speaker)
	var wav := AudioStreamWAV.new()
	wav.format = AudioStreamWAV.FORMAT_16_BITS
	wav.mix_rate = 48000
	wav.loop_mode = AudioStreamWAV.LOOP_FORWARD
	wav.loop_end = 48000
	var pcm := PackedByteArray()
	pcm.resize(48000 * 2)
	for index in range(48000):
		pcm.encode_s16(index * 2, 4096)
	wav.data = pcm
	var retired: Array[AudioStreamPlayback] = []
	for index in range(8):
		speaker.call("play_stream", wav)
		var playback := speaker.get_stream_playback()
		retired.append(playback)
		var samples := playback.mix_audio(1.0, 512)
		check(samples.size() == 512, "initial block size")
		check(samples[256].length() > 0.01, "initial audio is audible")
	var current := speaker.get_stream_playback()
	for count in [64, 128, 256, 1024, 2048, 73, 512]:
		var samples := current.mix_audio(1.0, count)
		check(samples.size() == count, "mixer must preserve request size %d" % count)
		var incorrect := 0
		for sample in samples:
			if absf(sample.x - 0.125) > 0.001 or absf(sample.y - 0.125) > 0.001:
				incorrect += 1
		check(incorrect == 0, "mixer must preserve every source sample without accumulating old output")
	current = null
	# Compare a nonconstant source against Godot's direct playback so request
	# partitioning cannot hide duplicated, reordered, or dropped PCM samples.
	for index in range(48000):
		pcm.encode_s16(index * 2, (index * 7919) % 16384 - 8192)
	wav.data = pcm
	var direct := wav.instantiate_playback()
	direct.start()
	speaker.call("play_stream", wav)
	current = speaker.get_stream_playback()
	var mismatched := 0
	for count in [73, 1024, 64, 2048, 256, 128, 512]:
		var expected := direct.mix_audio(1.0, count)
		var actual := current.mix_audio(1.0, count)
		check(actual.size() == expected.size(), "partitioned block length")
		for index in range(mini(actual.size(), expected.size())):
			if actual[index].distance_to(expected[index]) > 0.00001:
				mismatched += 1
	check(mismatched == 0, "partitioned requests changed %d source samples" % mismatched)
	direct.stop()
	direct = null
	retired.append(current)
	current = null
	var retained_stream := speaker.stream
	speaker.free()
	# Include a playback created after its former owner has been freed.
	retired.append(retained_stream.instantiate_playback())
	for playback in retired:
		playback.start()
		var samples := playback.mix_audio(1.0, 512)
		check(samples.size() == 512, "retired block size")
		for sample in samples:
			check(sample == Vector2.ZERO, "retired playback must return initialized silence")
		playback.stop()
	retired.clear()
	retained_stream = null
	room.queue_free()
	await create_timer(0.25).timeout
	print("STEAM_AUDIO_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify(errors))
	quit(0 if errors.is_empty() else 1)
