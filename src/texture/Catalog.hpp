#pragma once

enum class TextureType {
  Unknown,
  Panel,
  Overlay,
  Knob_bg,
  Knob_mg,
  Knob_fg,
  Slider_track,
  Slider_handle,
  Switch_frame,
  Port_input,
  Port_output,
};

struct Breadcrumbs {
	std::string pluginSlug{""};
	std::string moduleSlug{""};

	uint8_t componentId{0}; // param/input/output id

	TextureType textureType{TextureType::Unknown};

	uint8_t frameIdx{0};

  // panel, overlay
  Breadcrumbs(
    std::string _pluginSlug,
    std::string _moduleSlug,
    TextureType _textureType = TextureType::Panel
  ): pluginSlug(_pluginSlug),
    moduleSlug(_moduleSlug),
    textureType(_textureType) {}

  // param, port
  Breadcrumbs(
    std::string _pluginSlug,
    std::string _moduleSlug,
    uint8_t id,
    TextureType _textureType,
    uint8_t _frameIdx = 0
  ): pluginSlug(_pluginSlug),
    moduleSlug(_moduleSlug),
    componentId(id),
    textureType(_textureType),
    frameIdx(_frameIdx) {}
};
