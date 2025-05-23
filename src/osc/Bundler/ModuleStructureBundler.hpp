#pragma once

#include "Bundler.hpp"


struct ModuleStructureBundler : Bundler {
  ModuleStructureBundler(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  typedef std::pair<std::string, std::string> SlugPair;
  static std::map<SlugPair, int32_t> structureIds;
  static inline int32_t structureIdCounter = 0;

  int32_t id;
  std::string pluginSlug, moduleSlug;

  rack::plugin::Model* findModel();
  rack::app::ModuleWidget* makeModuleWidget();
  void cleanup(rack::app::ModuleWidget* moduleWidget);

  int32_t numParams{0}, numInputs{0}, numOutputs{0}, numLights{0};

  float px2cm(const float& px) const;
  rack::math::Vec vec2cm(const rack::math::Vec& px) const;

  void addLightMessages(rack::app::ModuleWidget* moduleWidget);
  void addLightMessage(rack::app::LightWidget* lightWidget, int32_t paramId = -1);
  void addParamMessages(rack::app::ModuleWidget* moduleWidget);
  void addPortMessages(rack::app::ModuleWidget* moduleWidget);
};
