#pragma once

#include "Bundler.hpp"

#include <tuple>

enum class ParamType {
  Unknown, Knob, Slider, Button, Switch
};

enum class PortType {
  Unknown, Input, Output
};

enum class LightShape {
  Unknown, Round, Rectangle
};

struct ModuleStructureBundler : Bundler {
  ModuleStructureBundler(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  // typedef std::pair<std::string, std::string> SlugPair;
  // static std::map<SlugPair, int32_t> structureIds;
  static inline int32_t structureIdCounter = 0;
  int32_t id;

  std::string pluginSlug, moduleSlug;

  typedef std::tuple<std::string, std::string, int> ParamTuple;
  std::map<ParamTuple, ParamType> paramTypeOverrides = {
    {{"Befaco", "Muxlicer", 0}, ParamType::Switch}
  };
  bool needsParamTypeOverride(int paramId);
  ParamType getParamTypeOverride(int paramId);

  int32_t numParams{0}, numInputs{0}, numOutputs{0}, numLights{0};

  void addLightMessages(rack::app::ModuleWidget* moduleWidget);
  void addLightMessage(rack::app::LightWidget* lightWidget, int32_t paramId = -1);
  void addParamMessages(rack::app::ModuleWidget* moduleWidget);
  void addPortMessages(rack::app::ModuleWidget* moduleWidget);
};
