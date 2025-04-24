#include "ModuleStructureBundler.hpp"

ModuleStructureBundler::ModuleStructureBundler(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): Bundler("ModuleStructureBundler"), pluginSlug(_pluginSlug), moduleSlug(_moduleSlug) {
  rack::plugin::Model* model = findModel();
  if (!model) {
    WARN(
      "ModuleStructureBundler unable to find Model for %s:%s",
      pluginSlug.c_str(),
      moduleSlug.c_str()
    );
    return;
  }
  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(NULL);

  rack::math::Vec panelSize = vec2cm(moduleWidget->box.size);
  messages.emplace_back(
    "/set/module_structure",
    [this, panelSize](osc::OutboundPacketStream& pstream) {
      pstream << pluginSlug.c_str()
        << moduleSlug.c_str()
        << panelSize.x
        << panelSize.y
        ;
    }
  );

  addLightMessages(moduleWidget);
  addParamMessages(moduleWidget);
  addPortMessages(moduleWidget);

  messages.emplace_back(
    "/stop/module_structure",
    [this](osc::OutboundPacketStream& pstream) {
      pstream << pluginSlug.c_str()
        << moduleSlug.c_str()
        ;
    }
  );

  delete moduleWidget;
}

void ModuleStructureBundler::addLightMessages(rack::app::ModuleWidget* moduleWidget) {
  using namespace rack::app;
  using namespace rack::widget;

  int32_t lightId = 0;

  for (Widget* widget : moduleWidget->children) {
    if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget)) {
      rack::math::Vec size = vec2cm(lightWidget->box.size);
      rack::math::Vec pos = vec2cm(lightWidget->box.pos);

      messages.emplace_back(
        "/set/module_structure/light",
        [this, lightId, size, pos](osc::OutboundPacketStream& pstream) {
          // TODO: we're assuming lights with a perfectly square size are
          //       circular. we should render the widget and check for
          //       transparent corners instead, because some square lights
          //       are probably rectangles
          int32_t lightShape = size.x == size.y ? 0 : 1;

          pstream << pluginSlug.c_str()
            << moduleSlug.c_str()
            << lightId
            << lightShape
            << size.x
            << size.y
            << pos.x
            << pos.y
            ;
        }
      );

      ++lightId;
    } // else if (rack::app::LedDisplay* display = dynamic_cast<rack::app::LedDisplay*>(widget)) {
  }
}

void ModuleStructureBundler::addParamMessages(rack::app::ModuleWidget* moduleWidget) {
  // using namespace rack::app;

  // for (ParamWidget* & paramWidget : moduleWidget->getParams()) {
  //   // int paramId = paramWidget->getParamQuantity()->paramId;

  //   for (rack::widget::Widget* & widget : paramWidget->children) {
  //     if (rack::app::LightWidget* lightWidget = dynamic_cast<rack::app::LightWidget*>(widget)) {
  //     }
  //   }
  // }
}

void ModuleStructureBundler::addPortMessages(rack::app::ModuleWidget* moduleWidget) {
}

rack::plugin::Model* ModuleStructureBundler::findModel() {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }
  return nullptr;
}

float ModuleStructureBundler::px2cm(const float& px) const {
  return px / (rack::window::SVG_DPI / rack::window::MM_PER_IN) / 10.f;
}

rack::math::Vec ModuleStructureBundler::vec2cm(const rack::math::Vec& pxVec) const {
  return rack::math::Vec(
    px2cm(pxVec.x),
    px2cm(pxVec.y)
  );
}
