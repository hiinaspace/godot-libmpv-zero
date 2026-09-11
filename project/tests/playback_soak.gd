extends "res://tests/transport.gd"

var ended := false
var frame_gaps: Array[float] = []

func rss_kib() -> int:
	# procfs reports a zero file length; read lines instead of get_as_text().
	var status := FileAccess.open("/proc/self/status", FileAccess.READ)
	if status == null:
		return 0
	while not status.eof_reached():
		var line := status.get_line()
		if line.begins_with("VmRSS:"):
			return line.trim_prefix("VmRSS:").strip_edges().to_int()
	return 0

func _run() -> void:
	var scene: Node = load("res://example_vr.tscn").instantiate()
	scene.get_node("ReflectionProbe").free()
	root.add_child(scene)
	player = scene.get_node("MPVPlayer")
	player.playback_error.connect(func(message: String): errors.append(message))
	player.playback_finished.connect(func(): ended = true)
	check(await wait_for(func(): return player.get_playback_position() > 0.5), "initial playback")
	var seconds := float(OS.get_environment("LIBMPV_ZERO_TEST_SECONDS"))
	if seconds <= 0:
		seconds = 600
	var started := Time.get_ticks_msec()
	var deadline := started + int(seconds * 1000)
	var next_sample := started
	var last_frame := Time.get_ticks_usec()
	var previous_position := -1.0
	var previous_hash := 0
	var changing_frames := 0
	var completed_files := 0
	var warm_rss := 0
	var peak_rss := 0
	var captured := false
	while Time.get_ticks_msec() < deadline and errors.is_empty():
		await process_frame
		var now := Time.get_ticks_usec()
		frame_gaps.append(float(now - last_frame) / 1000)
		last_frame = now
		if ended:
			completed_files += 1
			ended = false
			previous_position = -1.0
			player.load(OS.get_environment("LIBMPV_ZERO_MEDIA"))
			player.play()
			check(await wait_for(func(): return player.get_playback_position() > 0.5), "EOF reload")
			last_frame = Time.get_ticks_usec()
		if Time.get_ticks_msec() < next_sample:
			continue
		# A reload can cross a sampling deadline. Do not catch up with several
		# adjacent samples and misreport an unchanged video PTS as a stall.
		next_sample = Time.get_ticks_msec() + 1000
		var position := player.get_playback_position()
		check(position > previous_position, "playback clock stalled")
		previous_position = position
		var audio := player.get_audio_diagnostics()
		check(audio.max_queued_frames <= audio.capacity_frames, "audio queue exceeds capacity")
		if audio.consumed_frames_per_channel.size() == 2:
			check(absi(audio.consumed_frames_per_channel[0] - audio.consumed_frames_per_channel[1]) <= 512, "channel clocks diverged")
		var live := player.output_texture.get_live_texture()
		if live != null:
			var frame := live.get_image()
			if frame != null and not frame.is_empty():
				var frame_hash := hash(frame.get_data())
				if frame_hash != previous_hash:
					changing_frames += 1
				previous_hash = frame_hash
		var rss := rss_kib()
		check(rss > 0, "could not read resident memory")
		peak_rss = maxi(peak_rss, rss)
		var elapsed := float(Time.get_ticks_msec() - started) / 1000
		if not captured and elapsed >= 3:
			root.get_texture().get_image().save_png(OS.get_environment("LIBMPV_ZERO_SCREENSHOT"))
			captured = true
		if warm_rss == 0 and elapsed >= minf(30, seconds / 2):
			warm_rss = rss
		check(warm_rss == 0 or rss - warm_rss < 256 * 1024, "resident memory grew by more than 256 MiB after warmup")
		print("SOAK ", JSON.stringify({"seconds": elapsed, "position": position, "completed_files": completed_files,
			"rss_kib": rss, "static_memory": OS.get_static_memory_usage(), "audio": audio}))
	check(changing_frames >= seconds * 0.75, "video did not keep changing")
	if frame_gaps.is_empty():
		frame_gaps.append(0)
	frame_gaps.sort()
	print("SMOKE_RESULT ", "PASS" if errors.is_empty() else "FAIL", " ", JSON.stringify({"errors": errors,
		"changing_frames": changing_frames, "completed_files": completed_files, "warm_rss_kib": warm_rss, "peak_rss_kib": peak_rss,
		"frame_gap_p50_ms": frame_gaps[int(frame_gaps.size() * 0.50)],
		"frame_gap_p99_ms": frame_gaps[int(frame_gaps.size() * 0.99)], "frame_gap_max_ms": frame_gaps.back()}))
	scene.queue_free()
	await create_timer(0.25).timeout
	quit(0 if errors.is_empty() else 1)
