extends Node3D

const DEFAULT_MEDIA_SOURCE := "res://smoke_test_lr_sync.mp4"
const MOVE_SPEED := 3.0
const SNAP_TURN_ANGLE := deg_to_rad(30.0)
const STICK_DEADZONE := 0.2
const SEEK_STEP := 5.0

@onready var _xr_origin: XROrigin3D = $XROrigin3D
@onready var _xr_camera: XRCamera3D = $XROrigin3D/XRCamera3D
@onready var _emissive_screen: MeshInstance3D = $EmissiveScreen
@onready var _left_audio_player: Node = $EmissiveScreen/LeftSpeaker
@onready var _right_audio_player: Node = $EmissiveScreen/RightSpeaker
@onready var _player: MPVPlayer = $MPVPlayer

var _media_source := ""
var _screen_base_height := 2.0
var _left_controller: XRController3D
var _right_controller: XRController3D
var _auto_quit := false
var _auto_reload_after := 0.0
var _auto_reload_done := false
var _trace_frame_gaps := false
var _frame_gap_threshold_ms := 13.5
var _reload_in_progress := false
var _snap_turn_ready := true
var _left_ax_ready := true
var _left_by_ready := true
var _right_ax_ready := true
var _right_by_ready := true
var xr_interface: XRInterface

func _ready() -> void:
	_auto_quit = OS.get_environment("LIBMPV_ZERO_AUTOQUIT") == "1"
	_auto_reload_after = float(OS.get_environment("LIBMPV_ZERO_RELOAD_AFTER"))
	_trace_frame_gaps = OS.get_environment("LIBMPV_ZERO_TRACE_FRAME_GAPS") == "1"
	xr_interface = XRServer.find_interface("OpenXR")
	if xr_interface:
		if not xr_interface.is_initialized():
			xr_interface.initialize()
		if xr_interface.is_initialized():
			DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
			get_viewport().use_xr = true
		else:
			push_warning("OpenXR failed to initialize, please check if your headset is connected")
	else:
		push_warning("OpenXR not initialized, please check if your headset is connected")

	_media_source = _resolve_media_source()
	_capture_screen_defaults()
	_ensure_controllers()
	_configure_screen_material()
	_configure_player()


func _process(delta: float) -> void:
	_trace_process_gap(delta)
	_update_xr_movement(delta)
	_update_xr_media_controls()
	if not _auto_reload_done and _auto_reload_after > 0.0 and _player.get_playback_position() >= _auto_reload_after:
		_auto_reload_done = true
		_reload_media()


func _capture_screen_defaults() -> void:
	if _emissive_screen.mesh is QuadMesh:
		_screen_base_height = (_emissive_screen.mesh as QuadMesh).size.y


func _ensure_controllers() -> void:
	_left_controller = _xr_origin.get_node_or_null("LeftController")
	if _left_controller == null:
		_left_controller = XRController3D.new()
		_left_controller.name = "LeftController"
		_left_controller.tracker = "left_hand"
		_xr_origin.add_child(_left_controller)

	_right_controller = _xr_origin.get_node_or_null("RightController")
	if _right_controller == null:
		_right_controller = XRController3D.new()
		_right_controller.name = "RightController"
		_right_controller.tracker = "right_hand"
		_xr_origin.add_child(_right_controller)


func _configure_screen_material() -> void:
	_emissive_screen.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	if _player.output_texture == null:
		_player.output_texture = MPVTexture.new()

	var base_material := _emissive_screen.get_active_material(0) as StandardMaterial3D
	var material := base_material
	if material != null:
		material = material.duplicate() as StandardMaterial3D
	else:
		material = StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		material.emission_enabled = true
		material.emission = Color(1.0, 1.0, 1.0, 1.0)
		material.emission_energy_multiplier = 7.0

	material.albedo_texture_force_srgb = true
	material.albedo_texture = _player.output_texture
	material.emission_texture = _player.output_texture
	_emissive_screen.set_surface_override_material(0, material)


func _configure_player() -> void:
	_player.left_audio_target = _player.get_path_to(_left_audio_player)
	_player.right_audio_target = _player.get_path_to(_right_audio_player)
	_player.source = _media_source
	_player.autoplay = true

	_player.video_size_changed.connect(_on_video_size_changed)
	_player.file_loaded.connect(_on_file_loaded)
	_player.playback_finished.connect(_on_playback_finished)
	_player.playback_error.connect(_on_playback_error)

	_player.load(_media_source)
	_player.play()


func _on_video_size_changed(width: int, height: int) -> void:
	if width > 0 and height > 0 and _emissive_screen.mesh is QuadMesh:
		var quad := _emissive_screen.mesh as QuadMesh
		var aspect := float(width) / float(height)
		quad.size = Vector2(_screen_base_height * aspect, _screen_base_height)


func _on_file_loaded() -> void:
	if _reload_in_progress:
		_trace_frame_event("file_loaded")
	_reload_in_progress = false


func _on_playback_finished() -> void:
	if _reload_in_progress:
		_trace_frame_event("playback_finished")
	if _auto_quit:
		get_tree().create_timer(2.0).timeout.connect(_quit_after_capture)


func _on_playback_error(message: String) -> void:
	push_warning(message)
	print("example_vr.gd playback_error: %s" % message)


func _quit_after_capture() -> void:
	if is_inside_tree():
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

	return ProjectSettings.globalize_path(DEFAULT_MEDIA_SOURCE)


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
	_trace_frame_event("reload_requested")
	_player.load(_media_source)
	_player.play()


func _trace_process_gap(delta: float) -> void:
	if not _trace_frame_gaps:
		return
	var delta_ms := delta * 1000.0
	if delta_ms < _frame_gap_threshold_ms:
		return
	var label := "frame_gap"
	if _reload_in_progress:
		label = "frame_gap_during_reload"
	print("example_vr.gd %s: %.2fms pos=%.3f status=%s mpv=%s" % [
		label,
		delta_ms,
		_player.get_playback_position(),
		_player.get_video_status(),
		_player.get_mpv_status(),
	])


func _trace_frame_event(name: String) -> void:
	if not _trace_frame_gaps:
		return
	print("example_vr.gd %s at pos=%.3f status=%s mpv=%s" % [
		name,
		_player.get_playback_position(),
		_player.get_video_status(),
		_player.get_mpv_status(),
	])


func _update_button_edge(controller: XRController3D, action_name: StringName, ready: bool, callback: Callable) -> bool:
	var pressed: bool = controller != null and controller.is_button_pressed(action_name)
	if not pressed:
		return true
	if not ready:
		return false
	callback.call()
	return false


func _update_xr_media_controls() -> void:
	_left_ax_ready = _update_button_edge(_left_controller, &"ax_button", _left_ax_ready, Callable(self, "_toggle_play_pause"))
	_left_by_ready = _update_button_edge(_left_controller, &"by_button", _left_by_ready, Callable(self, "_reload_media"))
	_right_ax_ready = _update_button_edge(_right_controller, &"ax_button", _right_ax_ready, Callable(self, "_seek_forward"))
	_right_by_ready = _update_button_edge(_right_controller, &"by_button", _right_by_ready, Callable(self, "_seek_backward"))


func _seek_forward() -> void:
	_seek_by(SEEK_STEP)


func _seek_backward() -> void:
	_seek_by(-SEEK_STEP)


func _update_xr_movement(delta: float) -> void:
	if _left_controller == null or _right_controller == null:
		return

	var move_input := _left_controller.get_vector2("primary")
	if move_input.length() >= STICK_DEADZONE:
		var basis := _xr_camera.global_transform.basis
		var forward := -basis.z
		forward.y = 0.0
		forward = forward.normalized()
		var right := basis.x
		right.y = 0.0
		right = right.normalized()
		var movement := (right * move_input.x + forward * move_input.y) * MOVE_SPEED * delta
		_xr_origin.global_position += movement

	var turn_input_x := _right_controller.get_vector2("primary").x
	if abs(turn_input_x) < STICK_DEADZONE:
		_snap_turn_ready = true
		return

	if not _snap_turn_ready:
		return

	_snap_turn_ready = false
	_apply_snap_turn(-sign(turn_input_x) * SNAP_TURN_ANGLE)


func _apply_snap_turn(angle_radians: float) -> void:
	var camera_position := _xr_camera.global_position
	var origin_position := _xr_origin.global_position
	var offset := origin_position - camera_position
	offset = offset.rotated(Vector3.UP, angle_radians)

	_xr_origin.rotate_y(angle_radians)
	_xr_origin.global_position = camera_position + offset
