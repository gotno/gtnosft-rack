#pragma once

#include "rack.hpp"

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

// ModuleStructureBundler
enum class ParamType;
enum class PortType;

// Renderer
struct RenderResult;
struct Recipe;

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

// to avoid re-hashing our rapidhash'd hashes
struct IdentiHash {
	size_t operator()(uint64_t v) const noexcept {
		return static_cast<size_t>(v);
	}
};

struct Catalog {
  static RenderResult pullTexture(uint64_t id, Recipe recipe);

  static std::vector<int64_t> pullIds(
    ParamType type,
    rack::app::ParamWidget* widget
  );
  static int64_t pullId(PortType type, rack::app::PortWidget* widget);

private:
	static inline std::unordered_map<uint64_t, int64_t, IdentiHash> registry;
	static inline std::unordered_map<int64_t, Breadcrumbs> textureBreadcrumbs;

	static int64_t ingest(Breadcrumbs breadcrumbs);
	static uint64_t hashBitmap(uint8_t* pixels);
};
