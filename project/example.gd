extends Control


func _ready() -> void:
	var status_label := Label.new()
	status_label.text = "Waiting for Phase0TextureProbe..."
	status_label.position = Vector2(16, 16)
	add_child(status_label)

	var probe := Phase0TextureProbe.new()
	add_child(probe)

	probe.texture_ready.connect(func(texture: Texture2D) -> void:
		status_label.text = "Probe ready"
		var rect := TextureRect.new()
		rect.texture = texture
		rect.position = Vector2(16, 48)
		rect.custom_minimum_size = Vector2(256, 256)
		rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		rect.stretch_mode = TextureRect.STRETCH_SCALE
		add_child(rect)
	)

	probe.probe_failed.connect(func(reason: String) -> void:
		status_label.text = "Probe failed: %s" % reason
	)
