extends Node3D

const MOVE_SPEED := 3.5
const TURN_SPEED := 1.8
const DEFAULT_MEDIA_SOURCE := "https://dn710604.ca.archive.org/0/items/BigBuckBunny_124/Content/big_buck_bunny_720p_surround.mp4"
const SEEK_STEP := 5.0

@onready var _camera: Camera3D = $Camera3D
@onready var _player: MPVPlayer = $MPVPlayer
@onready var _screen: MeshInstance3D = $Screen
@onready var _left_audio_player: Node = $Screen/LeftSpeaker
@onready var _right_audio_player: Node = $Screen/RightSpeaker

var _status_timer := 0.0
var _status_label: Label
var _mpv_status_label: Label
var _perf_label: Label
var _hint_label: Label
var _last_video_status := ""
var _auto_quit := false
var _auto_reload_after := 0.0
var _auto_reload_done := false
var _reload_in_progress := false
var _media_source := ""
var _video_updates_since_tick := 0
var _screen_base_height := 2.0

func _ready() -> void:
	_auto_quit = OS.get_environment("LIBMPV_ZERO_AUTOQUIT") == "1"
	_auto_reload_after = float(OS.get_environment("LIBMPV_ZERO_RELOAD_AFTER"))
	_media_source = _resolve_media_source()
	_capture_screen_defaults()
	_create_overlay()
	_configure_player()


func _process(delta: float) -> void:
	if _player == null:
		return

	_update_controls(delta)
	if not _auto_reload_done and _auto_reload_after > 0.0 and _player.get_playback_position() >= _auto_reload_after:
		_auto_reload_done = true
		_reload_media()

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

	_perf_label.text = "engine_fps=%d video_updates=%d time=%.2f / %.2f" % [
		engine_fps,
		video_update_rate,
		_player.get_playback_position(),
		_player.get_duration(),
	]
	_status_label.text = "Video: %s | channels=%d queued=%d max=%d underruns=%d" % [
		video_status,
		int(audio_diag.get("channel_count", 0)),
		int(audio_diag.get("total_queued_frames", 0)),
		int(audio_diag.get("max_queued_frames", 0)),
		int(audio_diag.get("underrun_count", 0)),
	]


func _capture_screen_defaults() -> void:
	if _screen.mesh is QuadMesh:
		_screen_base_height = (_screen.mesh as QuadMesh).size.y


func _create_overlay() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)

	var panel := PanelContainer.new()
	panel.position = Vector2(16, 16)
	panel.size = Vector2(420, 84)
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

	_hint_label = Label.new()
	_hint_label.text = "WASD move, Q/E turn, Space play/pause, Left/Right seek, R reload. Source: %s" % _media_source
	vbox.add_child(_hint_label)


func _configure_player() -> void:
	if _player.output_texture == null:
		_player.output_texture = MPVTexture.new()

	var active_material := _screen.get_active_material(0) as StandardMaterial3D
	if active_material != null:
		active_material.albedo_texture = _player.output_texture

	_player.left_audio_target = _player.get_path_to(_left_audio_player)
	_player.right_audio_target = _player.get_path_to(_right_audio_player)
	_player.source = _media_source
	_player.autoplay = true

	_player.video_size_changed.connect(_on_video_size_changed)
	_player.texture_changed.connect(_on_texture_changed)
	_player.file_loaded.connect(_on_file_loaded)
	_player.playback_finished.connect(_on_playback_finished)
	_player.playback_error.connect(_on_playback_error)

	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()

	_player.load(_media_source)
	_player.play()


func _on_video_size_changed(width: int, height: int) -> void:
	_status_label.text = "Video texture ready: %dx%d" % [width, height]
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()

	if width > 0 and height > 0 and _screen.mesh is QuadMesh:
		var quad := _screen.mesh as QuadMesh
		var aspect := float(width) / float(height)
		quad.size = Vector2(_screen_base_height * aspect, _screen_base_height)


func _on_file_loaded() -> void:
	_reload_in_progress = false
	_status_label.text = "mpv file loaded"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()


func _on_texture_changed() -> void:
	_video_updates_since_tick += 1


func _on_playback_finished() -> void:
	_status_label.text = "mpv playback finished"
	_mpv_status_label.text = "mpv: %s" % _player.get_mpv_status()
	if _reload_in_progress:
		return
	if _auto_quit:
		get_tree().create_timer(2.0).timeout.connect(_quit_after_capture)


func _on_playback_error(message: String) -> void:
	push_warning(message)
	print("example.gd playback_error: %s" % message)


func _quit_after_capture() -> void:
	if is_inside_tree():
		get_tree().quit()


func _unhandled_input(event: InputEvent) -> void:
	if _player == null:
		return
	if event is not InputEventKey:
		return
	if not event.pressed or event.echo:
		return

	match event.keycode:
		KEY_SPACE:
			_toggle_play_pause()
			get_viewport().set_input_as_handled()
		KEY_LEFT:
			_seek_by(-SEEK_STEP)
			get_viewport().set_input_as_handled()
		KEY_RIGHT:
			_seek_by(SEEK_STEP)
			get_viewport().set_input_as_handled()
		KEY_R:
			_reload_media()
			get_viewport().set_input_as_handled()


func _resolve_media_source() -> String:
	var env_source := OS.get_environment("LIBMPV_ZERO_MEDIA")
	if not env_source.is_empty():
		return _normalize_media_source(env_source)

	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--media="):
			return _normalize_media_source(arg.substr(8))
		if not arg.begins_with("--"):
			return _normalize_media_source(arg)

	return ProjectSettings.globalize_path(DEFAULT_MEDIA_SOURCE)


func _normalize_media_source(source: String) -> String:
	var trimmed := source.strip_edges()
	if trimmed.is_empty():
		return ProjectSettings.globalize_path(DEFAULT_MEDIA_SOURCE)
	if trimmed.begins_with("res://") or trimmed.begins_with("user://"):
		return ProjectSettings.globalize_path(trimmed)
	if trimmed.contains("://"):
		return trimmed
	if trimmed.length() >= 3 and trimmed[1] == ":" and (trimmed[2] == "/" or trimmed[2] == "\\"):
		return trimmed
	if trimmed.begins_with("/") or trimmed.begins_with("\\\\"):
		return trimmed
	return ProjectSettings.globalize_path("res://" + trimmed)


func _toggle_play_pause() -> void:
	if _player.is_playing():
		_player.pause()
	else:
		_player.play()


func _seek_by(offset_seconds: float) -> void:
	var target: float = maxf(0.0, _player.get_playback_position() + offset_seconds)
	var duration: float = _player.get_duration()
	if duration > 0.0:
		target = minf(target, duration)
	_player.seek(target)


func _reload_media() -> void:
	_reload_in_progress = true
	_player.load(_media_source)
	_player.play()


func _update_controls(delta: float) -> void:
	if _camera == null:
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
		var basis := _camera.global_transform.basis
		var forward := -basis.z
		forward.y = 0.0
		forward = forward.normalized()
		var right := basis.x
		right.y = 0.0
		right = right.normalized()
		_camera.position += (right * move_input.x + forward * move_input.y) * MOVE_SPEED * delta

	var turn := 0.0
	if Input.is_key_pressed(KEY_Q):
		turn += 1.0
	if Input.is_key_pressed(KEY_E):
		turn -= 1.0
	if turn != 0.0:
		_camera.rotate_y(turn * TURN_SPEED * delta)
