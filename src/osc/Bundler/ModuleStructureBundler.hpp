#pragma once

#include "Bundler.hpp"

// TODO: static map of <pluginSlug, moduleSlug> to "short id"
//       send in /set/module_structure
//       reference it anywhere else
struct ModuleStructureBundler : Bundler {
  ModuleStructureBundler(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );
  ~ModuleStructureBundler() override;

  std::string pluginSlug, moduleSlug;

  rack::plugin::Model* findModel();
  rack::app::ModuleWidget* moduleWidget;

  float px2cm(const float& px) const;
  rack::math::Vec vec2cm(const rack::math::Vec& px) const;

  void addLightMessages();
  void addParamMessages();
  void addPortMessages();
};
