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

  int64_t moduleId{0};
  uint8_t componentId{0}; // param/input/output id

  TextureType textureType{TextureType::Unknown};

  uint8_t frameIdx{0};

  int64_t textureId;
  void setTextureId(int64_t id) { textureId = id; }

  // panel
  Breadcrumbs(std::string _pluginSlug, std::string _moduleSlug):
    pluginSlug(_pluginSlug),
    moduleSlug(_moduleSlug),
    textureType(TextureType::Panel) {}

  // overlay
  Breadcrumbs(int64_t _moduleId):
    moduleId(_moduleId), textureType(TextureType::Overlay) {}

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

// Module widgets shared by all the test renders of one module structure,
// so ingest doesn't build (and add to the engine) a widget per texture.
// Switch_frame needs ParamQuantities, so it renders from the caller's
// connected widget; everything else renders from an unconnected one, as
// a standalone render would.
struct ProbeWidgets {
  // connected must outlive this
  ProbeWidgets(rack::app::ModuleWidget* connected);
  ~ProbeWidgets();
  ProbeWidgets(const ProbeWidgets&) = delete;
  ProbeWidgets& operator=(const ProbeWidgets&) = delete;

  rack::app::ModuleWidget* get(TextureType type);

private:
  rack::app::ModuleWidget* connected;
  rack::app::ModuleWidget* unconnected{nullptr};
  bool unconnectedFailed{false};
};

struct Catalog {
  static RenderResult pullTexture(uint64_t id, Recipe recipe);

  static int64_t pullPanelId(rack::app::ModuleWidget* widget);
  static int64_t pullOverlayId(rack::app::ModuleWidget* widget);
  static std::vector<int64_t> pullIds(
    ParamType type,
    rack::app::ParamWidget* widget,
    ProbeWidgets& probes
  );
  static int64_t pullId(
    PortType type,
    rack::app::PortWidget* widget,
    ProbeWidgets& probes
  );

private:
  static inline std::unordered_map<uint64_t, int64_t, IdentiHash> registry;
  static inline std::unordered_map<int64_t, Breadcrumbs> textureBreadcrumbs;

  static inline std::unordered_map<
    std::string, // moduleSlug
    std::unordered_map<std::string /* pluginSlug */, int64_t /* textureId */>
  > panelTextureIds;
  static inline std::unordered_map<
    int64_t, // moduleId
    int64_t // textureId
  > overlayTextureIds;

  static int64_t makeId();
  static int64_t ingest(Breadcrumbs breadcrumbs, ProbeWidgets& probes);
  static uint64_t hashBitmap(uint8_t* pixels);
};
