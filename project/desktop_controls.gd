extends CanvasLayer

var player: MPVPlayer
var open_media: Callable
var pause_button: Button
var timeline: HSlider
var position_label: Label
var source_dialog: ConfirmationDialog
var source_input: LineEdit
var dragging := false

func button(row: HBoxContainer, text: String, action: Callable) -> Button:
	var result := Button.new()
	result.text = text
	result.focus_mode = Control.FOCUS_NONE
	result.pressed.connect(action)
	row.add_child(result)
	return result

func _ready() -> void:
	var panel := PanelContainer.new()
	add_child(panel)
	panel.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	panel.offset_left = 16
	panel.offset_right = -16
	panel.offset_top = -86
	panel.offset_bottom = -12
	var rows := VBoxContainer.new()
	panel.add_child(rows)
	var progress := HBoxContainer.new()
	rows.add_child(progress)
	timeline = HSlider.new()
	timeline.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	timeline.focus_mode = Control.FOCUS_NONE
	timeline.step = 0.1
	timeline.drag_started.connect(func(): dragging = true)
	timeline.drag_ended.connect(func(changed: bool):
		dragging = false
		if changed: player.seek(timeline.value))
	progress.add_child(timeline)
	position_label = Label.new()
	progress.add_child(position_label)
	var actions := HBoxContainer.new()
	rows.add_child(actions)
	pause_button = button(actions, "Pause [Space]", toggle_pause)
	button(actions, "−5 s [←]", func(): seek_by(-5))
	button(actions, "+5 s [→]", func(): seek_by(5))
	button(actions, "Open [O]", show_source)
	button(actions, "Fullscreen [F]", toggle_fullscreen)
	source_dialog = ConfirmationDialog.new()
	source_dialog.title = "Open video"
	source_dialog.ok_button_text = "Play"
	source_input = LineEdit.new()
	source_input.placeholder_text = "URL or absolute file path"
	source_input.custom_minimum_size.x = 520
	source_dialog.add_child(source_input)
	add_child(source_dialog)
	source_dialog.confirmed.connect(load_source)
	source_input.text_submitted.connect(func(_text: String): load_source())
	get_window().files_dropped.connect(func(files: PackedStringArray):
		if not files.is_empty(): open_media.call(files[0]))

func _process(_delta: float) -> void:
	if not is_instance_valid(player):
		return
	var position := player.get_playback_position()
	var duration := player.get_duration()
	timeline.max_value = maxf(duration, 1)
	timeline.editable = duration > 0
	if not dragging:
		timeline.set_value_no_signal(position)
	position_label.text = "%s / %s" % [timestamp(position), timestamp(duration)]
	pause_button.text = "Pause [Space]" if player.is_playing() else "Play [Space]"

func timestamp(seconds: float) -> String:
	return "%d:%02d:%02d" % [int(seconds) / 3600, int(seconds) / 60 % 60, int(seconds) % 60]

func toggle_pause() -> void:
	if player.is_playing():
		player.pause()
	else:
		if player.get_duration() > 0 and player.get_playback_position() >= player.get_duration() - 0.1:
			player.seek(0)
		player.play()

func seek_by(seconds: float) -> void:
	player.seek(clampf(player.get_playback_position() + seconds, 0, maxf(player.get_duration(), 0)))

func toggle_fullscreen() -> void:
	get_window().mode = Window.MODE_WINDOWED if get_window().mode == Window.MODE_FULLSCREEN else Window.MODE_FULLSCREEN

func show_source() -> void:
	source_dialog.popup_centered()
	source_input.grab_focus()

func load_source() -> void:
	if not source_input.text.strip_edges().is_empty():
		open_media.call(source_input.text)
		source_dialog.hide()

func _unhandled_key_input(event: InputEvent) -> void:
	if not event.is_pressed() or event.is_echo() or source_dialog.visible:
		return
	match event.keycode:
		KEY_SPACE: toggle_pause()
		KEY_LEFT: seek_by(-5)
		KEY_RIGHT: seek_by(5)
		KEY_O: show_source()
		KEY_F: toggle_fullscreen()
		_: return
	get_viewport().set_input_as_handled()
