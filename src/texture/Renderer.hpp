#pragma once

#include "rack.hpp"
#include <variant>

struct WidgetContainer : rack::widget::Widget {
  void draw(const DrawArgs& args) override {
    Widget::draw(args);
    Widget::drawLayer(args, 1);
  }
};

enum RenderStatus {
  Unknown,
  Failure,
  Success,
  Empty,
};

enum class RenderType {
	Unknown,
	Scaled,
	Exact
};

// Catalog
enum class TextureType;
struct Breadcrumbs;

struct Recipe {
	RenderType type{RenderType::Unknown};
	float scale{-1.f};
	int32_t height{-1};
	int32_t width{-1};

	Recipe() = default;
	Recipe(float _scale): type(RenderType::Scaled), scale(_scale) {};
	Recipe(int32_t _height): type(RenderType::Exact), height(_height) {};
	Recipe(int32_t _height, int32_t _width):
		type(RenderType::Exact), height(_height), width(_width) {};
};

struct RenderResult {
  uint8_t* pixels;
  int width;
  int height;

  RenderStatus status{RenderStatus::Unknown};
  std::string statusMessage{""};

  bool success() {
    return status == RenderStatus::Success;
  }

  bool failure() {
    return status == RenderStatus::Failure;
  }

  bool empty() {
    return status == RenderStatus::Empty;
  }

  RenderResult(): status(RenderStatus::Empty) {}

  RenderResult(uint8_t* pixels, int width, int height):
    pixels(pixels), width(width), height(height), status(RenderStatus::Success) {}

  RenderResult(const std::string& _statusMessage)
    : status(RenderStatus::Failure), statusMessage(_statusMessage) {}
};

struct Renderer {
  Renderer(rack::widget::FramebufferWidget* framebuffer);
  ~Renderer();

  RenderResult render(rack::math::Vec scale);

  rack::widget::FramebufferWidget* framebuffer = NULL;

  static RenderResult UNKNOWN_TEXTURE_TYPE(
    std::string caller,
		const Breadcrumbs& breadcrumbs
  );

  static RenderResult MODEL_NOT_FOUND(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static RenderResult MODULE_NOT_FOUND(std::string caller, int64_t moduleId);

  static RenderResult MODULE_WIDGET_ERROR(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static RenderResult WIDGET_NOT_FOUND(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int id = -1
  );

  static RenderResult renderTexture(
		const Breadcrumbs& breadcrumbs,
		const Recipe& recipe
	);

  static RenderResult renderPanel(
		rack::app::ModuleWidget* moduleWidget,
		const Recipe& recipe
  );

  static RenderResult renderOverlay(
    int64_t moduleId,
    const Recipe& recipe
  );

  static RenderResult renderKnob(
		rack::app::ParamWidget* knobWidget,
    const Breadcrumbs& breadcrumbs,
		const Recipe& recipe
  );

  static RenderResult renderSwitch(
		rack::app::ParamWidget* switchWidget,
    const Breadcrumbs& breadcrumbs,
		const Recipe& recipe
  );

  static RenderResult renderSlider(
		rack::app::ParamWidget* paramWidget,
    const Breadcrumbs& breadcrumbs,
		const Recipe& recipe
  );

  static RenderResult renderPort(
		rack::app::PortWidget* portWidget,
    const Breadcrumbs& breadcrumbs,
		const Recipe& recipe
  );

  static rack::widget::FramebufferWidget* findFramebuffer(
    rack::widget::Widget* widget
  );

  // wrap Widget in container and framebuffer
  static rack::widget::FramebufferWidget* wrapForRendering(
    rack::widget::Widget* widget
  );
  // clear children from wrapper
  static void clearRenderWrapper(
    rack::widget::FramebufferWidget* fb
  );

  uint8_t* renderPixels(
    rack::widget::FramebufferWidget* fb,
    int& width,
    int& height,
    rack::math::Vec scale
  );

  static float getScaleFromVariant(
    rack::widget::FramebufferWidget* framebuffer,
    std::variant<float, int32_t> scaleOrHeight
  );

  static rack::math::Vec getScaleFromRecipe(
    rack::widget::FramebufferWidget* framebuffer,
    const Recipe& recipe
  );

  // remove shadows/screws/params/ports/lights from widget
  static void hideChildren(rack::widget::Widget* mw);

  void flipBitmap(uint8_t* pixels, int width, int height, int depth);
//   rack::app::ModuleWidget* makeModuleWidget();

//   // make a new ModuleWidget with no Module
//   rack::app::ModuleWidget* makeDummyModuleWidget(rack::app::ModuleWidget* mw);

//   // make a new ModuleWidget and give it the old ModuleWidget's Module
//   rack::app::ModuleWidget* makeSurrogateModuleWidget(rack::app::ModuleWidget* mw);

//   std::string makeFilename(rack::app::ModuleWidget* mw);

//   // render FramebufferWidget to png
//   void renderPng(std::string directory, std::string filename, rack::widget::FramebufferWidget* fb);

//   // remove children->front() from ModuleWidget, which should be Internal->panel
//   void abandonPanel(rack::app::ModuleWidget* mw);

//   // render module without actual data, as in library preview, save
//   void saveModulePreviewRender(rack::app::ModuleWidget* moduleWidget);

//   // render only panel framebuffer, save
//   void savePanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

//   // render only panel framebuffer, compress & send
//   void sendPanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

//   // render only panel framebuffer, send
//   void sendPanelRenderUncompressed(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

//   // render module without panel or params/ports/lights, compress & send
//   // TODO: probably more efficient to hold on to the surrogate and update its module each time
//   int32_t sendOverlayRender(rack::app::ModuleWidget* moduleWidget, float zoom = 1.f);

//   void sendModuleInfo(rack::app::ModuleWidget* moduleWidget);

//   rack::widget::FramebufferWidget* getPanelFramebuffer(
//     rack::app::ModuleWidget* moduleWidget
//   );

//   void refreshModuleWidgets();

//   void logChildren(std::string& name, rack::app::ModuleWidget* mw);
};
