#include "ModuleCacheGuard.hpp"

#include "ModuleLightsBundler.hpp"
#include "ModuleParamsBundler.hpp"
#include "../../texture/Renderer.hpp"
#include "../../bench/Bench.hpp"

ModuleCacheGuard::ModuleCacheGuard(int64_t _moduleId): moduleId(_moduleId) {
  visible = false;
}

ModuleCacheGuard::~ModuleCacheGuard() {
  ModuleLightsBundler::lights.erase(moduleId);
  ModuleParamsBundler::params.erase(moduleId);
  BENCH(
    if (Renderer::overlayCache.contains(moduleId))
      bench::count("overlay_cache.evict_destroyed");
  )
  Renderer::evictOverlay(moduleId);
}

void ModuleCacheGuard::ensure(
  rack::app::ModuleWidget* moduleWidget,
  int64_t moduleId
) {
  for (rack::widget::Widget* child : moduleWidget->children) {
    if (dynamic_cast<ModuleCacheGuard*>(child)) return;
  }
  moduleWidget->addChild(new ModuleCacheGuard(moduleId));
}

void ModuleCacheGuard::clearAll() {
  ModuleLightsBundler::lights.clear();
  ModuleParamsBundler::params.clear();
  Renderer::clearOverlayCache();
}
