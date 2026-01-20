#include "ModuleStructureBundler.hpp"
#include "../../util/Util.hpp"

ModuleStructureBundler::ModuleStructureBundler(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): Bundler("ModuleStructureBundler"),
  id(structureIdCounter),
  pluginSlug(_pluginSlug),
  moduleSlug(_moduleSlug)
{
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  rack::app::ModuleWidget* moduleWidget =
    gtnosft::util::makeConnectedModuleWidget(model);
  if (!moduleWidget) return;

  rack::math::Vec panelSize = gtnosft::util::vec2cm(moduleWidget->box.size);

  if (shouldLog) {
    INFO("ModuleStructureBundler: %s:%s", pluginSlug.c_str(), moduleSlug.c_str());
    INFO("  panel: %f/%f", panelSize.x, panelSize.y);
  }

  addLightMessages(moduleWidget);
  addParamMessages(moduleWidget);
  addPortMessages(moduleWidget);

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

  delete moduleWidget;
  ++structureIdCounter;
}

void ModuleStructureBundler::addLightMessage(
  rack::app::LightWidget* lightWidget,
  int32_t paramId
) {
  rack::math::Vec size = gtnosft::util::vec2cm(lightWidget->box.size);
  rack::math::Vec pos = gtnosft::util::vec2cm(lightWidget->box.pos);
  int32_t lightId = numLights;
  bool defaultVisible = lightWidget->isVisible();
  std::string bgColorHex = rack::color::toHexString(lightWidget->bgColor);

  if (shouldLog) {
    if (paramId == -1) INFO("  - light %d", lightId);
    if (paramId != -1) INFO("  - light %d (param %d)", lightId, paramId);
  }

  messages.emplace_back(
    "/set/module_structure/light",
    [this, lightId, paramId, size, pos, defaultVisible, bgColorHex](
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
        << bgColorHex.c_str()
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
    size = gtnosft::util::vec2cm(paramWidget->box.size);
    pos = gtnosft::util::vec2cm(paramWidget->box.pos);

    // struct
    name = pq->getLabel(); // (name or #<paramId>)
    description = pq->getDescription();
    defaultValue = pq->getDefaultValue();
    minValue = pq->getMinValue();
    maxValue = pq->getMaxValue();
    defaultVisible = paramWidget->isVisible();
    snap = pq->snapEnabled;

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

    if (shouldLog) {
      std::string typeString;
      switch(type) {
        case ParamType::Button:
          typeString = "button";
          break;
        case ParamType::Switch:
          typeString = "switch";
          break;
        case ParamType::Knob:
          typeString = "knob";
          break;
        case ParamType::Slider:
          typeString = "slider";
          break;
        case ParamType::Unknown:
          typeString = "???";
          break;
      }
      INFO("  - param %d (%s) %s", paramId, typeString.c_str(), name.c_str());
      INFO(
        "    min/max val %f/%f (default %f) %s",
        minValue, maxValue, defaultValue, snap ? "snap" : ""
      );
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
      // some knobs do min/max angle some other way?
      bool isVult = pluginSlug.find("Vult") != std::string::npos;
      bool isSlimechild = pluginSlug.find("SlimeChild") != std::string::npos;
      bool needsOverride = isVult || isSlimechild;
      float minAngle = needsOverride ? -0.75f * M_PI : knobWidget->minAngle;
      float maxAngle = needsOverride ? 0.75f * M_PI : knobWidget->maxAngle;

      if (shouldLog) {
        INFO("    min/max angle %f/%f (actual)", knobWidget->minAngle, knobWidget->maxAngle);
      }

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
      rack::math::Vec handleSize =
        gtnosft::util::vec2cm(sliderWidget->handle->getBox().size);
      rack::math::Vec minHandlePos =
        gtnosft::util::vec2cm(sliderWidget->minHandlePos);
      rack::math::Vec maxHandlePos =
        gtnosft::util::vec2cm(sliderWidget->maxHandlePos);
      bool horizontal = sliderWidget->horizontal;

      if (shouldLog) {
        INFO("    %s", horizontal ? "horizontal" : "vertical");
      }

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

      if (shouldLog) {
        INFO("    %s", momentary ? "momentary" : "latch");
      }

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

      if (shouldLog) {
        INFO("    %d frames", numFrames);
      }

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
    size = gtnosft::util::vec2cm(portWidget->box.size);
    pos = gtnosft::util::vec2cm(portWidget->box.pos);
    name = portWidget->getPortInfo()->name;
    description = portWidget->getPortInfo()->description;
    defaultVisible = portWidget->isVisible();

    if (shouldLog) {
      INFO(
        "  - port %d (%s) %s",
        portId, type == PortType::Input ? "input" : "output", name.c_str()
      );
    }

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
