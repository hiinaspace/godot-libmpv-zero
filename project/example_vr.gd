extends Node3D

const SPEAKER_OFFSETS := [
	Vector3(-1.4, 0.0, 0.35),
	Vector3(1.4, 0.0, 0.35),
]
const VIDEO_DISTANCE := 2.5
const VIDEO_DEPTH := 0.12
const DEFAULT_MEDIA_SOURCE := "https://file.vrg.party/ichigoova01.m3u8"

var _audio_players: Array[AudioStreamPlayer3D] = []
var _video_faces: Array[MeshInstance3D] = []
var _player: MPVPlayer
var _video_root: Node3D
var _video_material: StandardMaterial3D
var _emissive_screen: MeshInstance3D
var _xr_origin: XROrigin3D
var _xr_camera: XRCamera3D
var _media_source := ""
var xr_interface: XRInterface
var _using_steam_audio := false

func _ready() -> void:
	xr_interface = XRServer.find_interface("OpenXR")
	if xr_interface and xr_interface.is_initialized():
		print("OpenXR initialized successfully")

		# Turn off v-sync!
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

		# Change our main viewport to output to the HMD
		get_viewport().use_xr = true
	else:
		print("OpenXR not initialized, please check if your headset is connected")
		
	_media_source = _resolve_media_source()
	_xr_origin = get_node_or_null("XROrigin3D")
	_xr_camera = get_node_or_null("XROrigin3D/XRCamera3D")
	_emissive_screen = get_node_or_null("EmissiveScreen")
	if _xr_origin == null or _xr_camera == null:
		push_error("example_vr.gd expects XROrigin3D/XRCamera3D in the scene.")
		return

	_create_video_root()
	_create_player()
	_create_speakers()
	_position_video_rig()

	_player.load_file(_media_source)
	_player.play()
	print("example_vr.gd load_file issued: %s" % _media_source)
	print("example_vr.gd spatial audio backend: %s" % ("SteamAudioPlayer" if _using_steam_audio else "AudioStreamPlayer3D"))


func _exit_tree() -> void:
	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()
	_video_faces.clear()


func _create_video_root() -> void:
	if _emissive_screen != null:
		_video_root = _emissive_screen
		_emissive_screen.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
	else:
		_video_root = Node3D.new()
		_video_root.name = "VideoRoot"
		add_child(_video_root)

	var base_material: StandardMaterial3D
	if _emissive_screen != null:
		base_material = _emissive_screen.get_active_material(0) as StandardMaterial3D
		if base_material != null:
			_video_material = base_material.duplicate() as StandardMaterial3D

	if _video_material == null:
		_video_material = StandardMaterial3D.new()
		_video_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		_video_material.cull_mode = BaseMaterial3D.CULL_DISABLED
		_video_material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		_video_material.emission_enabled = true
		_video_material.emission = Color(1.0, 1.0, 1.0, 1.0)
		_video_material.emission_energy_multiplier = 7.01

	_video_material.albedo_texture_force_srgb = true

	if _emissive_screen != null:
		_emissive_screen.set_surface_override_material(0, _video_material)
	else:
		_rebuild_video_cube_faces(2.1, 1.2, VIDEO_DEPTH)


func _create_player() -> void:
	_player = MPVPlayer.new()
	add_child(_player)
	_player.audio_channels_changed.connect(_on_audio_channels_changed)
	_player.video_size_changed.connect(_on_video_size_changed)
	_player.texture_changed.connect(_on_texture_changed)
	_player.file_loaded.connect(_on_file_loaded)
	_player.playback_finished.connect(_on_playback_finished)
	print("example_vr.gd mpv status: %s" % _player.get_mpv_status())


func _create_speakers() -> void:
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
		if _emissive_screen != null:
			add_child(marker)
		else:
			_video_root.add_child(marker)


func _position_video_rig() -> void:
	if _emissive_screen != null:
		return
	var camera_basis := _xr_camera.global_transform.basis
	var camera_origin := _xr_camera.global_transform.origin
	var forward := -camera_basis.z
	forward.y = 0.0
	if forward.length_squared() < 0.0001:
		forward = Vector3.FORWARD
	forward = forward.normalized()

	_video_root.global_position = camera_origin + forward * VIDEO_DISTANCE
	_video_root.global_position.y = camera_origin.y
	_video_root.look_at(camera_origin, Vector3.UP, true)


func _on_audio_channels_changed(count: int) -> void:
	for audio_player in _audio_players:
		audio_player.queue_free()
	_audio_players.clear()

	for i in range(count):
		var audio_player := _create_spatial_audio_player()
		audio_player.name = "Speaker%d" % i
		audio_player.position = SPEAKER_OFFSETS[min(i, SPEAKER_OFFSETS.size() - 1)]
		if _emissive_screen != null:
			add_child(audio_player)
		else:
			_video_root.add_child(audio_player)
		_configure_spatial_audio_player(audio_player)
		_start_spatial_audio_player(audio_player, _player.get_audio_stream_for_channel(i))
		_audio_players.append(audio_player)


func _on_video_size_changed(width: int, height: int) -> void:
	print("example_vr.gd video_size_changed: %dx%d" % [width, height])
	if width > 0 and height > 0 and _emissive_screen == null:
		var aspect := float(width) / float(height)
		_rebuild_video_cube_faces(1.8 * aspect, 1.8, VIDEO_DEPTH)


func _on_texture_changed() -> void:
	var texture := _player.get_texture()
	_video_material.albedo_texture = texture
	_video_material.emission_texture = texture


func _on_file_loaded() -> void:
	print("example_vr.gd file_loaded signal")


func _on_playback_finished() -> void:
	for audio_player in _audio_players:
		audio_player.stop()
	print("example_vr.gd playback_finished signal")


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


func _rebuild_video_cube_faces(width: float, height: float, depth: float) -> void:
	if _video_root == null or _emissive_screen != null:
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
	_video_root.add_child(face)
	return face


func _create_spatial_audio_player() -> AudioStreamPlayer3D:
	if ClassDB.class_exists("SteamAudioPlayer"):
		var steam_player := ClassDB.instantiate("SteamAudioPlayer") as AudioStreamPlayer3D
		if steam_player != null:
			_using_steam_audio = true
			return steam_player

	_using_steam_audio = false
	return AudioStreamPlayer3D.new()


func _configure_spatial_audio_player(audio_player: AudioStreamPlayer3D) -> void:
	audio_player.max_distance = 12.0
	audio_player.unit_size = 2.0

	if _using_steam_audio:
		audio_player.set("distance_attenuation", true)
		audio_player.set("min_attenuation_distance", 1.0)
		audio_player.set("air_absorption", true)
		audio_player.set("occlusion", false)
		audio_player.set("reflection", false)
	else:
		audio_player.attenuation_model = AudioStreamPlayer3D.ATTENUATION_INVERSE_DISTANCE


func _start_spatial_audio_player(audio_player: AudioStreamPlayer3D, stream: AudioStream) -> void:
	if _using_steam_audio:
		audio_player.call("play_stream", stream, 0.0, 0.0, 1.0)
	else:
		audio_player.stream = stream
		audio_player.play()
