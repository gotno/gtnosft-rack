#include "ModuleStructureBundler.hpp"

ModuleStructureBundler::ModuleStructureBundler(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): Bundler("ModuleStructureBundler"),
  id(structureIdCounter),
  pluginSlug(_pluginSlug),
  moduleSlug(_moduleSlug)
{
  rack::app::ModuleWidget* moduleWidget = makeModuleWidget();
  if (!moduleWidget) return;

  addLightMessages(moduleWidget);
  addParamMessages(moduleWidget);
  addPortMessages(moduleWidget);

  rack::math::Vec panelSize = vec2cm(moduleWidget->box.size);
  messages.emplace(
    messages.begin(),
    "/set/module_structure",
    [this, panelSize](osc::OutboundPacketStream& pstream) {
      pstream << id
        << pluginSlug.c_str()
        << moduleSlug.c_str()
        << panelSize.x
        << panelSize.y
        << numParams
        << numInputs
        << numOutputs
        << numLights
        ;
    }
  );

  cleanup(moduleWidget);
  ++structureIdCounter;
}

void ModuleStructureBundler::addLightMessage(
  rack::app::LightWidget* lightWidget,
  int32_t paramId
) {
  rack::math::Vec size = vec2cm(lightWidget->box.size);
  rack::math::Vec pos = vec2cm(lightWidget->box.pos);
  int32_t lightId = numLights;
  bool defaultVisible = lightWidget->isVisible();

  messages.emplace_back(
    "/set/module_structure/light",
    [this, lightId, paramId, size, pos, defaultVisible](
      osc::OutboundPacketStream& pstream
    ) {
      // TODO: we're assuming lights with a perfectly square size are
      //       circular. we should render the widget and check for
      //       transparent corners instead, because some square lights
      //       are probably rectangles
      LightShape lightShape =
        // "basically zero"
        size.x - size.y >= 0.f && size.x - size.y < 0.01f
          ? LightShape::Round
          : LightShape::Rectangle;

      pstream << id
        << lightId
        << paramId
        << (int32_t)lightShape
        << size.x
        << size.y
        << pos.x
        << pos.y
        << defaultVisible
        ;
    }
  );

  ++numLights;
}

void ModuleStructureBundler::addLightMessages(rack::app::ModuleWidget* moduleWidget) {
  using namespace rack::app;
  using namespace rack::widget;

  // panel lights
  for (Widget* widget : moduleWidget->children) {
    if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget))
      addLightMessage(lightWidget);
  }

  // param lights
  for (ParamWidget* & paramWidget : moduleWidget->getParams()) {
    for (Widget* & widget : paramWidget->children) {
      if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget))
        addLightMessage(lightWidget, paramWidget->getParamQuantity()->paramId);
    }
  }
}

void ModuleStructureBundler::addParamMessages(rack::app::ModuleWidget* moduleWidget) {
  int32_t paramId;
  ParamType type;
  rack::math::Vec size, pos;
  std::string name, description;
  float defaultValue, minValue, maxValue;
  bool defaultVisible, snap;

  rack::app::SvgSlider* sliderWidget;
  rack::app::Knob* knobWidget;
  rack::app::Switch* switchWidget;

  for (rack::app::ParamWidget* & paramWidget : moduleWidget->getParams()) {
    rack::engine::ParamQuantity* pq = paramWidget->getParamQuantity();

    paramId = pq->paramId;
    size = vec2cm(paramWidget->box.size);
    pos = vec2cm(paramWidget->box.pos);

    // struct
    name = pq->getLabel(); // (name or #<paramId>)
    description = pq->getDescription();
    defaultValue = pq->getDefaultValue();
    minValue = pq->getMinValue();
    maxValue = pq->getMaxValue();
    defaultVisible = paramWidget->isVisible();
    snap = pq->snapEnabled;

    // state
    // std::string display = pq->getString(); // (getLabel + getDisplayValueString + getUnit)
    // float value = pq->getValue();
    // bool visible = paramWidget->isVisible();
    // tooltip: pq->getString /n pq->getDescription

    sliderWidget = NULL;
    knobWidget = NULL;
    switchWidget = NULL;

    if (needsParamTypeOverride(paramId)) {
      type = getParamTypeOverride(paramId);
    } else if ((sliderWidget = dynamic_cast<rack::app::SvgSlider*>(paramWidget))) {
      // deal with: dynamic_cast<bogaudio::VUSlider*>(sliderWidget)
      // (SliderKnob)
      type = ParamType::Slider;

    } else if ((knobWidget = dynamic_cast<rack::app::Knob*>(paramWidget))) {
      type = ParamType::Knob;

      // sometimes a knob is not a knob (looking at you, Surge),
      // but functionally, that's the best way to represent it.
      // if x and y aren't equal, use the smaller of the two.
      size = size.x > size.y ? rack::math::Vec(size.y) : rack::math::Vec(size.x);

    } else if ((switchWidget = dynamic_cast<rack::app::Switch*>(paramWidget))) {
      // deal with: dynamic_cast<bogaudio::StatefulButton*>(paramWidget);
      rack::app::SvgSwitch* svgSwitchWidget =
        dynamic_cast<rack::app::SvgSwitch*>(paramWidget);
      bool latch = svgSwitchWidget && svgSwitchWidget->latch;

      if (switchWidget->momentary || latch) {
        type = ParamType::Button;
      } else {
        type = ParamType::Switch;
      }
    } else {
      type = ParamType::Unknown;
    }

    if (type == ParamType::Unknown) {
      WARN(
        "ModuleStructureBundler unable to determine ParamType for %s:%s paramId %d",
        pluginSlug.c_str(),
        moduleSlug.c_str(),
        paramId
      );
      continue;
    }

    messages.emplace_back(
      "/set/module_structure/param",
      [
        this,
        paramId,
        type,
        name,
        description,
        size,
        pos,
        defaultValue,
        minValue,
        maxValue,
        defaultVisible,
        snap
      ](osc::OutboundPacketStream& pstream) {
        pstream << id
          << paramId
          << (int32_t)type
          << name.c_str()
          << description.c_str()
          << size.x
          << size.y
          << pos.x
          << pos.y
          << defaultValue
          << minValue
          << maxValue
          << defaultVisible
          << snap
          ;
      }
    );
    ++numParams;

    if (type == ParamType::Knob) {
      // Vult knobs do min/max angle some other way?
      bool isVult = pluginSlug.find("Vult") != std::string::npos;
      float minAngle = isVult ? -0.75f * M_PI : knobWidget->minAngle;
      float maxAngle = isVult ? 0.75f * M_PI : knobWidget->minAngle;

      messages.emplace_back(
        "/set/module_structure/param/knob",
        [this, paramId, minAngle, maxAngle](osc::OutboundPacketStream& pstream) {
          pstream << id
            << paramId
            << minAngle
            << maxAngle
            ;
        }
      );
    }

    if (type == ParamType::Slider) {
      rack::math::Vec handleSize = vec2cm(sliderWidget->handle->getBox().size);
      rack::math::Vec minHandlePos = vec2cm(sliderWidget->minHandlePos);
      rack::math::Vec maxHandlePos = vec2cm(sliderWidget->maxHandlePos);
      bool horizontal = sliderWidget->horizontal;

      messages.emplace_back(
        "/set/module_structure/param/slider",
        [
          this,
          paramId,
          handleSize,
          minHandlePos,
          maxHandlePos,
          horizontal
        ](osc::OutboundPacketStream& pstream) {
          pstream << id
            << paramId
            << handleSize.x
            << handleSize.y
            << minHandlePos.x
            << minHandlePos.y
            << maxHandlePos.x
            << maxHandlePos.y
            << horizontal
            ;
        }
      );
    }

    if (type == ParamType::Button) {
      bool momentary = switchWidget->momentary;

      messages.emplace_back(
        "/set/module_structure/param/button",
        [this, paramId, momentary](osc::OutboundPacketStream& pstream) {
          pstream << id
            << paramId
            << momentary
            ;
        }
      );
    }

    if (type == ParamType::Switch) {
      bool horizontal = size.x > size.y;
      int numFrames = maxValue + 1;

      messages.emplace_back(
        "/set/module_structure/param/switch",
        [this, paramId, horizontal, numFrames](osc::OutboundPacketStream& pstream) {
          pstream << id
            << paramId
            << numFrames
            << horizontal
            ;
        }
      );
    }
  }
}

void ModuleStructureBundler::addPortMessages(rack::app::ModuleWidget* moduleWidget) {
  int32_t portId;
  PortType type;
  rack::math::Vec size, pos;
  std::string name, description;
  bool defaultVisible;

  for (rack::app::PortWidget* portWidget : moduleWidget->getPorts()) {
    portId = portWidget->portId;
    type =
      portWidget->type == rack::engine::Port::INPUT
        ? PortType::Input
        : PortType::Output;
    size = vec2cm(portWidget->box.size);
    pos = vec2cm(portWidget->box.pos);
    name = portWidget->getPortInfo()->name;
    description = portWidget->getPortInfo()->description;
    defaultVisible = portWidget->isVisible();

    messages.emplace_back(
      "/set/module_structure/port",
      [this, portId, type, name, description, size, pos, defaultVisible](
        osc::OutboundPacketStream& pstream
      ) {
        pstream << id
          << portId
          << (int32_t)type
          << name.c_str()
          << description.c_str()
          << size.x
          << size.y
          << pos.x
          << pos.y
          << defaultVisible
          ;
      }
    );
    if (type == PortType::Input) ++numInputs;
    if (type == PortType::Output) ++numOutputs;
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

bool ModuleStructureBundler::needsParamTypeOverride(int paramId) {
  return (bool)paramTypeOverrides.count(
    std::make_tuple(pluginSlug, moduleSlug, paramId)
  );
}

ParamType ModuleStructureBundler::getParamTypeOverride(int paramId) {
  return paramTypeOverrides.at(
    std::make_tuple(pluginSlug, moduleSlug, paramId)
  );
}
