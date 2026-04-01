extends Node3D

const SPEAKER_OFFSETS := [
	Vector3(-1.4, 0.0, 0.35),
	Vector3(1.4, 0.0, 0.35),
]
const MOVE_SPEED := 3.5
const TURN_SPEED := 1.8
const VIDEO_SPIN_SPEED := 0.4

var _audio_players: Array[AudioStreamPlayer3D] = []
var _video_faces: Array[MeshInstance3D] = []
var _status_timer := 0.0
var _player: MPVPlayer
var _camera: Camera3D
var _video_cube: Node3D
var _video_material: StandardMaterial3D
var _status_label: Label
var _mpv_status_label: Label
var _perf_label: Label
var _last_video_status := ""
var _auto_quit := false
var _media_source := ""
var _player_pivot: Node3D
var _video_updates_since_tick := 0


func _ready() -> void:
	_auto_quit = OS.get_environment("LIBMPV_ZERO_AUTOQUIT") == "1"
	_media_source = _resolve_media_source()
	_create_world()
	_create_overlay()
	_create_player()

	_player.load_file(_media_source)
	_player.play()
	print("example.gd load_file issued: %s" % _media_source)


func _exit_tree() -> void:
	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()
	_video_faces.clear()


func _process(delta: float) -> void:
	if _player == null:
		return
	_update_controls(delta)
	if _video_cube:
		_video_cube.rotate_y(delta * VIDEO_SPIN_SPEED)
	_status_timer += delta
	if _status_timer < 1.0:
		return
	_status_timer = 0.0
	var audio_diag: Dictionary = _player.get_audio_diagnostics()
	var video_status := _player.get_video_status()
	var engine_fps := Engine.get_frames_per_second()
	var video_update_rate := _video_updates_since_tick
	_video_updates_since_tick = 0
	if video_status != _last_video_status:
		_last_video_status = video_status
		print("example.gd video status: %s" % video_status)
	_perf_label.text = "engine_fps=%d video_updates=%d time=%.2f / %.2f" % [
		engine_fps,
		video_update_rate,
		_player.get_time_pos(),
		_player.get_duration(),
	]
	print("example.gd perf: engine_fps=%d video_updates=%d time=%.2f / %.2f" % [
		engine_fps,
		video_update_rate,
		_player.get_time_pos(),
		_player.get_duration(),
	])
	print("example.gd audio diag: channels=%d queued=%d max=%d underruns=%d" % [
		int(audio_diag.get("channel_count", 0)),
		int(audio_diag.get("total_queued_frames", 0)),
		int(audio_diag.get("max_queued_frames", 0)),
		int(audio_diag.get("underrun_count", 0)),
	])
	print("example.gd audio per-channel queued: %s" % [audio_diag.get("queued_frames_per_channel", PackedInt32Array())])
	print("example.gd audio per-channel pulls: calls=%s frames=%s consumed=%s playing=%s" % [
		audio_diag.get("pull_calls_per_channel", PackedInt32Array()),
		audio_diag.get("pulled_frames_per_channel", PackedInt32Array()),
		audio_diag.get("consumed_frames_per_channel", PackedInt32Array()),
		audio_diag.get("playing_per_channel", []),
	])


func _create_world() -> void:
	_player_pivot = Node3D.new()
	_player_pivot.name = "PlayerPivot"
	add_child(_player_pivot)

	_camera = Camera3D.new()
	_camera.position = Vector3(0.0, 1.3, 4.8)
	_player_pivot.add_child(_camera)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-35.0, 35.0, 0.0)
	add_child(light)

	var fill_light := OmniLight3D.new()
	fill_light.position = Vector3(0.0, 2.4, 2.8)
	fill_light.light_energy = 0.7
	add_child(fill_light)

	var floor := MeshInstance3D.new()
	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(10.0, 10.0)
	floor.mesh = floor_mesh
	floor.position = Vector3(0.0, -1.0, 0.0)
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = Color(0.12, 0.12, 0.14)
	floor_material.roughness = 0.95
	floor.set_surface_override_material(0, floor_material)
	add_child(floor)

	_video_cube = Node3D.new()
	_video_cube.name = "VideoCube"
	_video_cube.position = Vector3(0.0, 0.0, 0.0)
	_video_material = StandardMaterial3D.new()
	_video_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_video_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	_video_material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	_video_material.albedo_texture_force_srgb = true
	add_child(_video_cube)
	_rebuild_video_cube_faces(2.1, 1.2, 0.12)

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

	_perf_label = Label.new()
	_perf_label.text = "engine_fps=0 video_updates=0 time=0.00 / 0.00"
	vbox.add_child(_perf_label)

	var hint_label := Label.new()
	hint_label.text = "WASD move, Q/E turn, video cube spins. Source: %s" % _media_source
	vbox.add_child(hint_label)


func _create_player() -> void:
	_player = MPVPlayer.new()
	add_child(_player)
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd mpv status: %s" % _player.get_mpv_status())

	_player.audio_channels_changed.connect(_on_audio_channels_changed)
	_player.video_size_changed.connect(_on_video_size_changed)
	_player.texture_changed.connect(_on_texture_changed)
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
		audio_player.stream_paused = false
		audio_player.play()
		_audio_players.append(audio_player)


func _on_video_size_changed(width: int, height: int) -> void:
	_status_label.text = "Video texture ready: %dx%d" % [width, height]
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd video_size_changed: %dx%d" % [width, height])

	if width > 0 and height > 0:
		var aspect := float(width) / float(height)
		_rebuild_video_cube_faces(2.2 * aspect, 2.2, 0.12)

func _on_file_loaded() -> void:
	_status_label.text = "mpv file loaded"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	print("example.gd file_loaded signal")


func _on_texture_changed() -> void:
	_video_updates_since_tick += 1
	_video_material.albedo_texture = _player.get_texture()


func _on_playback_finished() -> void:
	_status_label.text = "mpv playback finished"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	for audio_player in _audio_players:
		audio_player.stop()
	print("example.gd playback_finished signal")
	if _auto_quit:
		get_tree().create_timer(2.0).timeout.connect(_quit_after_capture)


func _quit_after_capture() -> void:
	if is_inside_tree():
		print("example.gd auto quit")
		get_tree().quit()


func _resolve_media_source() -> String:
	var env_source := OS.get_environment("LIBMPV_ZERO_MEDIA")
	if not env_source.is_empty():
		return _normalize_media_source(env_source)

	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--media="):
			return _normalize_media_source(arg.substr(8))
		if not arg.begins_with("--"):
			return _normalize_media_source(arg)

	return ProjectSettings.globalize_path("res://smoke_test_lr_sync.mp4")


func _normalize_media_source(source: String) -> String:
	var trimmed := source.strip_edges()
	if trimmed.is_empty():
		return ProjectSettings.globalize_path("res://smoke_test_lr_sync.mp4")
	if trimmed.begins_with("res://") or trimmed.begins_with("user://"):
		return ProjectSettings.globalize_path(trimmed)
	if trimmed.contains("://"):
		return trimmed
	if trimmed.length() >= 3 and trimmed[1] == ":" and (trimmed[2] == "/" or trimmed[2] == "\\"):
		return trimmed
	if trimmed.begins_with("/") or trimmed.begins_with("\\\\"):
		return trimmed
	return ProjectSettings.globalize_path("res://" + trimmed)


func _update_controls(delta: float) -> void:
	if _player_pivot == null:
		return

	var move_input := Vector2.ZERO
	if Input.is_key_pressed(KEY_A):
		move_input.x -= 1.0
	if Input.is_key_pressed(KEY_D):
		move_input.x += 1.0
	if Input.is_key_pressed(KEY_W):
		move_input.y += 1.0
	if Input.is_key_pressed(KEY_S):
		move_input.y -= 1.0

	if move_input != Vector2.ZERO:
		move_input = move_input.normalized()
		var basis := _player_pivot.global_transform.basis
		var forward := -basis.z
		forward.y = 0.0
		forward = forward.normalized()
		var right := basis.x
		right.y = 0.0
		right = right.normalized()
		_player_pivot.position += (right * move_input.x + forward * move_input.y) * MOVE_SPEED * delta

	var turn := 0.0
	if Input.is_key_pressed(KEY_Q):
		turn += 1.0
	if Input.is_key_pressed(KEY_E):
		turn -= 1.0
	if turn != 0.0:
		_player_pivot.rotate_y(turn * TURN_SPEED * delta)


func _rebuild_video_cube_faces(width: float, height: float, depth: float) -> void:
	if _video_cube == null:
		return

	for face in _video_faces:
		if is_instance_valid(face):
			face.queue_free()
	_video_faces.clear()

	var front := _make_video_face(Vector2(width, height), Vector3(0.0, 0.0, depth * 0.5), Vector3.ZERO)
	var back := _make_video_face(Vector2(width, height), Vector3(0.0, 0.0, -depth * 0.5), Vector3(0.0, 180.0, 0.0))
	var right := _make_video_face(Vector2(depth, height), Vector3(width * 0.5, 0.0, 0.0), Vector3(0.0, -90.0, 0.0))
	var left := _make_video_face(Vector2(depth, height), Vector3(-width * 0.5, 0.0, 0.0), Vector3(0.0, 90.0, 0.0))
	var top := _make_video_face(Vector2(width, depth), Vector3(0.0, height * 0.5, 0.0), Vector3(-90.0, 0.0, 0.0))
	var bottom := _make_video_face(Vector2(width, depth), Vector3(0.0, -height * 0.5, 0.0), Vector3(90.0, 0.0, 0.0))

	_video_faces.append_array([front, back, right, left, top, bottom])


func _make_video_face(size: Vector2, position: Vector3, rotation_degrees: Vector3) -> MeshInstance3D:
	var face := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = size
	face.mesh = quad
	face.position = position
	face.rotation_degrees = rotation_degrees
	face.set_surface_override_material(0, _video_material)
	_video_cube.add_child(face)
	return face
