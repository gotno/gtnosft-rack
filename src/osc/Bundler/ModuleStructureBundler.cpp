#include "ModuleStructureBundler.hpp"

ModuleStructureBundler::ModuleStructureBundler(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): Bundler("ModuleStructureBundler"), pluginSlug(_pluginSlug), moduleSlug(_moduleSlug) {
  rack::app::ModuleWidget* moduleWidget = makeModuleWidget();
  if (!moduleWidget) return;

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

  cleanup(moduleWidget);
}

void ModuleStructureBundler::addLightMessages(rack::app::ModuleWidget* moduleWidget) {
  using namespace rack::app;
  using namespace rack::widget;

  int32_t id = 0;
  rack::math::Vec size, pos;

  for (Widget* widget : moduleWidget->children) {
    if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget)) {
      size = vec2cm(lightWidget->box.size);
      pos = vec2cm(lightWidget->box.pos);

      messages.emplace_back(
        "/set/module_structure/light",
        [this, id, size, pos](osc::OutboundPacketStream& pstream) {
          // TODO: we're assuming lights with a perfectly square size are
          //       circular. we should render the widget and check for
          //       transparent corners instead, because some square lights
          //       are probably rectangles
          int32_t lightShape = size.x == size.y ? 0 : 1;

          pstream << pluginSlug.c_str()
            << moduleSlug.c_str()
            << id
            << lightShape
            << size.x
            << size.y
            << pos.x
            << pos.y
            ;
        }
      );

      ++id;
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
  int32_t id, type;
  rack::math::Vec size, pos;
  std::string name, description;

  for (rack::app::PortWidget* portWidget : moduleWidget->getPorts()) {
    id = portWidget->portId;
    type = portWidget->type == rack::engine::Port::INPUT ? 0 : 1;
    size = vec2cm(portWidget->box.size);
    pos = vec2cm(portWidget->box.pos);
    name = portWidget->getPortInfo()->name;
    description = portWidget->getPortInfo()->description;

    messages.emplace_back(
      "/set/module_structure/port",
      [this, id, type, name, description, size, pos](
        osc::OutboundPacketStream& pstream
      ) {
        pstream << pluginSlug.c_str()
          << moduleSlug.c_str()
          << id
          << type
          << name.c_str()
          << description.c_str()
          << size.x
          << size.y
          << pos.x
          << pos.y
          ;
      }
    );
  }
}

rack::plugin::Model* ModuleStructureBundler::findModel() {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }
  return NULL;
}

rack::app::ModuleWidget* ModuleStructureBundler::makeModuleWidget() {
  rack::plugin::Model* model = findModel();
  if (!model) {
    WARN(
      "ModuleStructureBundler unable to find Model for %s:%s",
      pluginSlug.c_str(),
      moduleSlug.c_str()
    );
    return NULL;
  }

  rack::engine::Module* module = model->createModule();
  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(module);
  APP->engine->addModule(module);
  return moduleWidget;
}

void ModuleStructureBundler::cleanup(rack::app::ModuleWidget* moduleWidget) {
  delete moduleWidget;
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
