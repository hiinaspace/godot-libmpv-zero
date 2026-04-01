extends Node3D

const USE_VULKAN_BACKEND := true
const VIDEO_BACKEND_SOFTWARE := 0
const VIDEO_BACKEND_VULKAN := 1
const SPEAKER_OFFSETS := [
	Vector3(-1.2, 0.0, 0.2),
	Vector3(1.2, 0.0, 0.2),
]

var _audio_players: Array[AudioStreamPlayer3D] = []
var _status_timer := 0.0
var _player: MPVPlayer
var _video_plane: MeshInstance3D
var _video_material: StandardMaterial3D
var _status_label: Label
var _mpv_status_label: Label


func _ready() -> void:
	_create_world()
	_create_overlay()
	_create_player()

	var smoke_test_path := ProjectSettings.globalize_path("res://smoke_test_lr_sync.mp4")
	_player.load_file(smoke_test_path)
	_player.play()
	print("example.gd load_file issued: %s" % smoke_test_path)


func _exit_tree() -> void:
	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()


func _process(delta: float) -> void:
	if _player == null:
		return
	_status_timer += delta
	if _status_timer < 1.0:
		return
	_status_timer = 0.0
	print("example.gd video status: %s" % _player.get_video_status())


func _create_world() -> void:
	var camera := Camera3D.new()
	camera.position = Vector3(0.0, 0.0, 3.0)
	add_child(camera)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35.0, 35.0, 0.0)
	add_child(light)

	_video_plane = MeshInstance3D.new()
	_video_plane.name = "VideoPlane"
	var quad := QuadMesh.new()
	quad.size = Vector2(2.0, 2.0)
	_video_plane.mesh = quad
	_video_material = StandardMaterial3D.new()
	_video_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_video_material.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	_video_plane.set_surface_override_material(0, _video_material)
	add_child(_video_plane)

	for i in range(2):
		var marker := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 0.08
		sphere.height = 0.16
		marker.mesh = sphere
		marker.position = SPEAKER_OFFSETS[i]
		var material := StandardMaterial3D.new()
		material.albedo_color = Color(0.95, 0.45, 0.18) if i == 0 else Color(0.18, 0.65, 0.95)
		marker.set_surface_override_material(0, material)
		add_child(marker)


func _create_overlay() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)

	var panel := PanelContainer.new()
	panel.position = Vector2(16, 16)
	panel.size = Vector2(360, 84)
	canvas.add_child(panel)

	var vbox := VBoxContainer.new()
	panel.add_child(vbox)

	_status_label = Label.new()
	_status_label.text = "Waiting for MPVPlayer..."
	vbox.add_child(_status_label)

	_mpv_status_label = Label.new()
	_mpv_status_label.text = "mpv: not initialized"
	vbox.add_child(_mpv_status_label)

	var hint_label := Label.new()
	hint_label.text = "Video alternates left/right flashes to match speaker tones."
	vbox.add_child(hint_label)


func _create_player() -> void:
	_player = MPVPlayer.new()
	_player.set_video_backend(VIDEO_BACKEND_VULKAN if USE_VULKAN_BACKEND else VIDEO_BACKEND_SOFTWARE)
	add_child(_player)
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd mpv status: %s" % _player.get_mpv_status())

	_player.audio_channels_changed.connect(_on_audio_channels_changed)
	_player.video_size_changed.connect(_on_video_size_changed)
	_player.file_loaded.connect(_on_file_loaded)
	_player.playback_finished.connect(_on_playback_finished)


func _on_audio_channels_changed(count: int) -> void:
	_status_label.text = "Audio channels ready: %d" % count
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()

	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()

	for i in range(count):
		var audio_player := AudioStreamPlayer3D.new()
		audio_player.name = "Speaker%d" % i
		audio_player.stream = _player.get_audio_stream_for_channel(i)
		audio_player.position = SPEAKER_OFFSETS[min(i, SPEAKER_OFFSETS.size() - 1)]
		audio_player.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE
		audio_player.max_distance = 12.0
		audio_player.unit_size = 2.0
		add_child(audio_player)
		audio_player.play()
		var playback := audio_player.get_stream_playback()
		if playback:
			_player.attach_audio_playback(i, playback)
			_audio_players.append(audio_player)


func _on_video_size_changed(width: int, height: int) -> void:
	_status_label.text = "Video texture ready: %dx%d" % [width, height]
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd video_size_changed: %dx%d" % [width, height])

	if width > 0 and height > 0:
		var quad := _video_plane.mesh as QuadMesh
		if quad:
			var aspect := float(width) / float(height)
			quad.size = Vector2(2.4 * aspect, 2.4)

	_video_material.albedo_texture = _player.get_texture()


func _on_file_loaded() -> void:
	_status_label.text = "mpv file loaded"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd file_loaded signal")


func _on_playback_finished() -> void:
	_status_label.text = "mpv playback finished"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd playback_finished signal")
