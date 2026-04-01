extends Control

const USE_VULKAN_BACKEND := false
const VIDEO_BACKEND_SOFTWARE := 0
const VIDEO_BACKEND_VULKAN := 1

var _audio_players: Array[AudioStreamPlayer] = []


func _exit_tree() -> void:
	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()


func _ready() -> void:
	var status_label := Label.new()
	status_label.text = "Waiting for MPVPlayer..."
	status_label.position = Vector2(16, 16)
	add_child(status_label)

	var mpv_status_label := Label.new()
	mpv_status_label.text = "mpv: not initialized"
	mpv_status_label.position = Vector2(16, 32)
	add_child(mpv_status_label)

	var rect := TextureRect.new()
	rect.position = Vector2(16, 64)
	rect.custom_minimum_size = Vector2(256, 256)
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_SCALE
	add_child(rect)

	var player := MPVPlayer.new()
	player.set_video_backend(VIDEO_BACKEND_VULKAN if USE_VULKAN_BACKEND else VIDEO_BACKEND_SOFTWARE)
	add_child(player)
	mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
	print("example.gd mpv status: %s" % player.get_mpv_status())

	player.audio_channels_changed.connect(func(count: int) -> void:
		status_label.text = "Audio channels ready: %d" % count
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
		for audio_player in _audio_players:
			audio_player.queue_free()
		_audio_players.clear()

		for i in range(count):
			var audio_player := AudioStreamPlayer.new()
			audio_player.name = "SmokeAudio%d" % i
			audio_player.stream = player.get_audio_stream_for_channel(i)
			add_child(audio_player)
			audio_player.play()
			var playback := audio_player.get_stream_playback()
			if playback:
				player.attach_audio_playback(i, playback)
				_audio_players.append(audio_player)
	)

	player.video_size_changed.connect(func(width: int, height: int) -> void:
		status_label.text = "Video texture ready: %dx%d" % [width, height]
		mpv_status_label.text = "mpv: %s" % player.get_mpv_status()
		print("example.gd video_size_changed: %dx%d" % [width, height])
		rect.texture = player.get_texture()
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

	var smoke_test_path := ProjectSettings.globalize_path("res://smoke_test.mp4")
	player.load_file(smoke_test_path)
	player.play()
	print("example.gd load_file issued: %s" % smoke_test_path)
