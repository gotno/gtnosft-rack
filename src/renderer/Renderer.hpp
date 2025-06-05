#pragma once

#include "rack.hpp"

struct ModuleWidgetContainer : rack::widget::Widget {
  void draw(const DrawArgs& args) override {
    Widget::draw(args);
    Widget::drawLayer(args, 1);
  }
};

enum RenderStatus {
  Unknown,
  Failure,
  Success,
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

  RenderResult(): status(RenderStatus::Unknown) {}

  RenderResult(uint8_t* pixels, int width, int height):
    pixels(pixels), width(width), height(height), status(RenderStatus::Success) {}

  RenderResult(const std::string& _statusMessage)
    : status(RenderStatus::Failure), statusMessage(_statusMessage) {}
};

struct Renderer {
  Renderer(rack::widget::FramebufferWidget* framebuffer);
  ~Renderer();

  RenderResult render();

  rack::widget::FramebufferWidget* framebuffer = NULL;

  static RenderResult MODEL_NOT_FOUND(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static RenderResult MODULE_WIDGET_ERROR(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static RenderResult WIDGET_NOT_FOUND(
    std::string caller,
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int id
  );

  static RenderResult renderPanel(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static RenderResult renderKnob(
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int id,
    std::map<std::string, RenderResult>& renderResults
  );

  static RenderResult renderSwitch(
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int32_t id,
    std::vector<RenderResult>& renderResults
  );

  static RenderResult renderSlider(
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int32_t id,
    std::map<std::string, RenderResult>& renderResults
  );

  static RenderResult renderPort(
    const std::string& pluginSlug,
    const std::string& moduleSlug,
    int id,
    rack::engine::Port::Type type
  );

  static rack::plugin::Model* findModel(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  static rack::widget::FramebufferWidget* findFramebuffer(
    rack::widget::Widget* widget
  );

  // static rack::app::ModuleWidget* getModuleWidget(int64_t moduleId);

  static rack::app::ModuleWidget* makeModuleWidget(rack::plugin::Model* model);
  static rack::app::ModuleWidget* makeConnectedModuleWidget(
    rack::plugin::Model* model
  );

  // wrap ModuleWidget in container and framebuffer
  static rack::widget::FramebufferWidget* wrapWidget(rack::widget::Widget* widget);

  // render FramebufferWidget to rgba pixel array
  uint8_t* renderPixels(rack::widget::FramebufferWidget* fb, int& width, int& height, float zoom = 3.f);

  // remove shadoes/screws/params/ports/lights from widget
  static void abandonChildren(rack::widget::Widget* mw);

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
