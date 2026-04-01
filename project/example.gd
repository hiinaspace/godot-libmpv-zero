extends Control


func _ready() -> void:
	var status_label := Label.new()
	status_label.text = "Waiting for MPVPlayer..."
	status_label.position = Vector2(16, 16)
	add_child(status_label)
	var mpv_status_label := Label.new()
	mpv_status_label.text = "mpv: not initialized"
	mpv_status_label.position = Vector2(16, 32)
	add_child(mpv_status_label)

	var player := MPVPlayer.new()
	add_child(player)
	mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
	print("example.gd mpv status: %s" % player.get_mpv_status())

	player.audio_channels_changed.connect(func(count: int) -> void:
		status_label.text = "Audio channels ready: %d" % count
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
	)

	player.video_size_changed.connect(func(_width: int, _height: int) -> void:
		status_label.text = "Video texture ready"
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
		var rect := TextureRect.new()
		rect.texture = player.get_texture()
		rect.position = Vector2(16, 64)
		rect.custom_minimum_size = Vector2(256, 256)
		rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		rect.stretch_mode = TextureRect.STRETCH_SCALE
		add_child(rect)
	)

	player.file_loaded.connect(func() -> void:
		status_label.text = "mpv file loaded"
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
		print("example.gd file_loaded signal")
	)

	player.playback_finished.connect(func() -> void:
		status_label.text = "mpv playback finished"
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
		print("example.gd playback_finished signal")
	)

	var smoke_test_path := ProjectSettings.globalize_path("res://smoke_test.ppm")
	player.load_file(smoke_test_path)
	player.play()
	print("example.gd load_file issued: %s" % smoke_test_path)
