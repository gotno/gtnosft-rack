#pragma once

#include "rack.hpp"

namespace gtnosft {
namespace util {

float px2cm(const float& px);
rack::math::Vec vec2cm(const rack::math::Vec& pxVec);

rack::plugin::Model* findModel(std::string pluginSlug, std::string moduleSlug);
rack::app::ModuleWidget* makeModuleWidget(rack::plugin::Model* model);
rack::app::ModuleWidget* makeConnectedModuleWidget(rack::plugin::Model* model);

} // namespace util
} // namespace gtnosft
