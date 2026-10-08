#include "Renderer.hpp"
#include "Catalog.hpp"
#include "../util/Util.hpp"
#include "../osc/Bundler/ModuleCacheGuard.hpp"
#include "../bench/Bench.hpp"
#include "math.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

RenderResult Renderer::MODULE_NOT_FOUND(
  std::string caller,
  int64_t moduleId
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s Module not found %lld",
      caller.c_str(),
      moduleId
    )
  );
}

RenderResult Renderer::OVERLAY_BLOCKLISTED(
  std::string caller,
  int64_t moduleId
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s Overlay blocklisted %lld",
      caller.c_str(),
      moduleId
    )
  );
}

RenderResult Renderer::UNKNOWN_TEXTURE_TYPE(
  std::string caller,
  const Breadcrumbs& breadcrumbs
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s %s:%s unknown texture type %d",
      caller.c_str(),
      breadcrumbs.pluginSlug.c_str(),
      breadcrumbs.moduleSlug.c_str(),
      breadcrumbs.textureType
    )
  );
}

RenderResult Renderer::MODEL_NOT_FOUND(
  std::string caller,
  const std::string& pluginSlug,
  const std::string& moduleSlug
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s Model not found %s:%s",
      caller.c_str(),
      pluginSlug.c_str(),
      moduleSlug.c_str()
    )
  );
}

RenderResult Renderer::MODULE_WIDGET_ERROR(
  std::string caller,
  const std::string& pluginSlug,
  const std::string& moduleSlug
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s can't create ModuleWidget for %s:%s",
      caller.c_str(),
      pluginSlug.c_str(),
      moduleSlug.c_str()
    )
  );
}

RenderResult Renderer::WIDGET_NOT_FOUND(
  std::string caller,
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int id
) {
  return RenderResult(
    rack::string::f(
      "Renderer::%s Widget not found %s:%s:%d",
      caller.c_str(),
      pluginSlug.c_str(),
      moduleSlug.c_str(),
      id
    )
  );
}

RenderResult Renderer::renderTexture(
  const Breadcrumbs& breadcrumbs,
  const Recipe& recipe
) {
  BENCH(bench::stamp(bench::Stage::RenderStart);)

  // Overlays need special handling
  if (breadcrumbs.textureType == TextureType::Overlay)
    return renderOverlay(breadcrumbs.moduleId, recipe);

  rack::plugin::Model* model =
    gtnosft::util::findModel(breadcrumbs.pluginSlug, breadcrumbs.moduleSlug);
  if (!model)
    return MODEL_NOT_FOUND(
      "renderTexture",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug
    );

  // Switch_frame needs access to ParamQuantities
  rack::app::ModuleWidget* moduleWidget =
    breadcrumbs.textureType == TextureType::Switch_frame
      ? gtnosft::util::makeConnectedModuleWidget(model)
      : gtnosft::util::makeModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR(
      "renderTexture",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug
    );
  DEFER({ delete moduleWidget; });
  BENCH(bench::stamp(bench::Stage::Prepared);)

  RenderResult result;

  switch (breadcrumbs.textureType) {
    case TextureType::Panel:
      result = renderPanel(moduleWidget, recipe);
      // if (result.success()) {
      //   renderPng(
      //     result.pixels,
      //     result.width,
      //     result.height,
      //     "render_panel_test",
      //     breadcrumbs.moduleSlug + std::to_string(result.height)
      //   );
      // }
      break;
    case TextureType::Overlay:
      WARN("should not have entered `case TextureType::Overlay`");
      return RenderResult();
    case TextureType::Knob_bg:
    case TextureType::Knob_mg:
    case TextureType::Knob_fg:
      result = renderKnob(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
      break;
    case TextureType::Slider_track:
    case TextureType::Slider_handle:
      result = renderSlider(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
      break;
    case TextureType::Switch_frame:
      result = renderSwitch(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
      break;
    case TextureType::Port_input:
    case TextureType::Port_output:
      result = renderPort(
        breadcrumbs.textureType == TextureType::Port_input
          ? moduleWidget->getInput(breadcrumbs.componentId)
          : moduleWidget->getOutput(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
      break;
    default:
      return UNKNOWN_TEXTURE_TYPE("renderTexture", breadcrumbs);
  }

  // if (result.success()) {
  //   renderPng(
  //     result.pixels,
  //     result.width,
  //     result.height,
  //     "render_test",
  //     rack::string::f("%lld", breadcrumbs.textureId)
  //   );
  // }
  return result;
}

RenderResult Renderer::renderPanel(
  rack::app::ModuleWidget* moduleWidget,
  const Recipe& recipe
) {
  // hide all children except the first, which should be the panel
  auto it = std::next(moduleWidget->children.begin());
  for (; it != moduleWidget->children.end(); ++it) (*it)->setVisible(false);

  rack::widget::FramebufferWidget* wrapper = wrapForRendering(moduleWidget);

  wrapper->step();

  rack::math::Vec scale = getScaleFromRecipe(wrapper, recipe);
  RenderResult result = Renderer(wrapper).render(scale);

  removeFromWrapper(wrapper, moduleWidget);
  delete wrapper;

  return result;
}

RenderResult Renderer::renderOverlay(
  int64_t moduleId,
  const Recipe& recipe
) {
  rack::app::ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  if (!moduleWidget) return MODULE_NOT_FOUND("renderOverlay", moduleId);

  if (moduleWidget->model->slug == "OSCctrl")
    return OVERLAY_BLOCKLISTED("renderOverlay", moduleId);

  auto cached = overlayCache.find(moduleId);
  if (
    cached != overlayCache.end()
      && (
        cached->second.moduleWidget != moduleWidget
          || cached->second.surrogate->module != moduleWidget->module
      )
  ) {
    BENCH(bench::count("overlay_cache.evict_invalid");)
    evictOverlay(moduleId);
    cached = overlayCache.end();
  }

  BENCH(
    bool cacheHit = cached != overlayCache.end();
    bench::count(cacheHit ? "overlay_cache.hit" : "overlay_cache.miss");
    if (bench::Trace* trace = bench::current()) {
      trace->overlay = true;
      trace->cacheHit = cacheHit;
    }
  )

  if (cached == overlayCache.end()) {
    rack::app::ModuleWidget* surrogate =
      moduleWidget->getModel()->createModuleWidget(moduleWidget->getModule());
    // evicts this entry when the real ModuleWidget is destroyed
    ModuleCacheGuard::ensure(moduleWidget, moduleId);
    cached = overlayCache.emplace(
      moduleId,
      OverlayCacheEntry{
        moduleWidget,
        surrogate,
        wrapForRendering(surrogate),
        std::chrono::steady_clock::now()
      }
    ).first;
  }

  OverlayCacheEntry& entry = cached->second;
  entry.lastUsed = std::chrono::steady_clock::now();
  rack::app::ModuleWidget* surrogate = entry.surrogate;
  rack::widget::FramebufferWidget* framebuffer = entry.framebuffer;

  surrogate->children.front()->setVisible(false); // panel
  hideChildren(
    surrogate,
    moduleWidget->model->plugin->slug,
    moduleWidget->model->slug
  );
  BENCH(bench::stamp(bench::Stage::Prepared);)

  // Some overlays (e.g. Fundamental:Scope) rely on the attached cables for some
  // aspect of the overlay render. We'll temporarily point the real input cables
  // at the surrogate's ports for the duration of the render.
  std::vector<std::pair<rack::app::CableWidget*, rack::app::PortWidget*>>
    retargetedCables;
  for (rack::app::PortWidget* surrogatePort : surrogate->getInputs()) {
    rack::app::PortWidget* realPort =
      moduleWidget->getInput(surrogatePort->portId);
    if (!realPort) continue;
    for (rack::app::CableWidget* cw : APP->scene->rack->getCablesOnPort(realPort)) {
      retargetedCables.emplace_back(cw, cw->inputPort);
      cw->inputPort = surrogatePort;
    }
  }
  DEFER({
    for (auto& [cw, originalPort] : retargetedCables)
      cw->inputPort = originalPort;
    if (!overlayCacheEnabled) evictOverlay(moduleId);
  });

  framebuffer->step();

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);
  RenderResult result = Renderer(framebuffer).render(scale);
  // if (result.success()) {
  //   renderPng(
  //     result.pixels,
  //     result.width,
  //     result.height,
  //     "render_panel_test",
  //     "overlay" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())
  //   );
  // }
  return result;
}

static void destroyOverlayCacheEntry(Renderer::OverlayCacheEntry& entry) {
  // the surrogate shares the real Module; don't let its destructor delete it
  entry.surrogate->module = NULL;
  delete entry.framebuffer;
}

void Renderer::evictOverlay(int64_t moduleId) {
  auto it = overlayCache.find(moduleId);
  if (it == overlayCache.end()) return;

  destroyOverlayCacheEntry(it->second);
  overlayCache.erase(it);
}

void Renderer::evictIdleOverlays() {
  if (overlayCache.empty()) return;

  auto now = std::chrono::steady_clock::now();
  for (auto it = overlayCache.begin(); it != overlayCache.end();) {
    if (now - it->second.lastUsed < OVERLAY_CACHE_IDLE_TIMEOUT) {
      ++it;
      continue;
    }
    BENCH(bench::count("overlay_cache.evict_idle");)
    destroyOverlayCacheEntry(it->second);
    it = overlayCache.erase(it);
  }
}

void Renderer::clearOverlayCache() {
  for (auto& [moduleId, entry] : overlayCache) destroyOverlayCacheEntry(entry);
  overlayCache.clear();
}

RenderResult Renderer::renderSwitch(
  rack::app::ParamWidget* switchWidget,
  const Breadcrumbs& breadcrumbs,
  const Recipe& recipe
) {
  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(switchWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND(
      "renderSwitch-fb",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug,
      breadcrumbs.componentId
    );
  hideChildren(framebuffer);

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);

  rack::engine::ParamQuantity* pq = switchWidget->getParamQuantity();
  pq->setValue(breadcrumbs.frameIdx);
  switchWidget->step();

  return Renderer(framebuffer).render(scale);
}

RenderResult Renderer::renderSlider(
  rack::app::ParamWidget* paramWidget,
  const Breadcrumbs& breadcrumbs,
  const Recipe& recipe
) {
  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(paramWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND(
      "renderSlider-fb",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug,
      breadcrumbs.componentId
    );
  hideChildren(framebuffer);

  rack::app::SvgSlider* sliderWidget =
    dynamic_cast<rack::app::SvgSlider*>(paramWidget);
  rack::widget::Widget* track = sliderWidget->background;
  rack::widget::Widget* handle = sliderWidget->handle;

  if (track && breadcrumbs.textureType == TextureType::Slider_track) {
    track->visible = true;
    framebuffer->box.size = track->box.size;
    rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);
    if (handle) handle->visible = false;

    return Renderer(framebuffer).render(scale);
  }

  if (handle && breadcrumbs.textureType == TextureType::Slider_handle) {
    if (track) track->visible = false;
    handle->visible = true;
    framebuffer->box.size = handle->box.size;
    rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);

    return Renderer(framebuffer).render(scale);
  }

  return RenderResult();
}

RenderResult Renderer::renderKnob(
  rack::app::ParamWidget* knobWidget,
  const Breadcrumbs& breadcrumbs,
  const Recipe& recipe
) {
  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(knobWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND(
      "renderKnob-fb",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug,
      breadcrumbs.componentId
    );
  hideChildren(framebuffer);

  rack::widget::Widget* bg{NULL};
  rack::widget::Widget* mg{NULL};
  rack::widget::Widget* fg{NULL};
  rack::widget::Widget* lastWidget{NULL};

  // find each layer's widget
  for (auto& child : framebuffer->children) {
    rack::widget::TransformWidget* tw =
      dynamic_cast<rack::widget::TransformWidget*>(child);
    if (tw) {
      // the widget before the TransformWidget is the background
      if (lastWidget) bg = lastWidget;
      // the child of the transform widget is the midground
      mg = tw->children.front();
    }
    // the widget after the TransformWidget is the foreground
    if (dynamic_cast<rack::widget::TransformWidget*>(lastWidget)) {
      fg = child;
    }

    lastWidget = child;
  }

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);

  if (bg && breadcrumbs.textureType == TextureType::Knob_bg) {
    bg->visible = true;
    if (mg) mg->visible = false;
    if (fg) fg->visible = false;

    return Renderer(framebuffer).render(scale);
  }

  if (mg && breadcrumbs.textureType == TextureType::Knob_mg) {
    if (bg) bg->visible = false;
    mg->visible = true;
    if (fg) fg->visible = false;

    return Renderer(framebuffer).render(scale);
  }

  if (fg && breadcrumbs.textureType == TextureType::Knob_fg) {
    if (bg) bg->visible = false;
    if (mg) mg->visible = false;
    fg->visible = true;

    return Renderer(framebuffer).render(scale);
  }

  return RenderResult();
}

RenderResult Renderer::renderPort(
  rack::app::PortWidget* portWidget,
  const Breadcrumbs& breadcrumbs,
  const Recipe& recipe
) {
  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(portWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND(
      "renderPort-fb",
      breadcrumbs.pluginSlug,
      breadcrumbs.moduleSlug,
      breadcrumbs.componentId
    );
  hideChildren(framebuffer);

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);
  return Renderer(framebuffer).render(scale);
}

rack::widget::FramebufferWidget* Renderer::findFramebuffer(
  rack::widget::Widget* widget
) {
  rack::widget::FramebufferWidget* fb = NULL;
  for (auto& child : widget->children) {
    fb = dynamic_cast<rack::widget::FramebufferWidget*>(child);
    if (fb) return fb;
  }
  return NULL;
}

Renderer::Renderer(rack::widget::FramebufferWidget* _framebuffer):
  framebuffer(_framebuffer) {}
Renderer::~Renderer() {}

RenderResult Renderer::render(rack::math::Vec scale) {
  try {
    int width, height;
    uint8_t* pixels = renderPixels(framebuffer, width, height, scale);

    return RenderResult(pixels, width, height);
  } catch (std::exception& e) {
    return RenderResult(e.what());
  } catch (...) {
    return RenderResult("unknown Renderer error");
  }
}

rack::widget::FramebufferWidget* Renderer::wrapForRendering(
  rack::widget::Widget* widget
) {
  rack::widget::FramebufferWidget* fbcontainer =
    new rack::widget::FramebufferWidget;
  WidgetContainer* container = new WidgetContainer;

  fbcontainer->addChild(container);
  container->box.size = widget->box.size;
  fbcontainer->box.size = widget->box.size;
  container->addChild(widget);

  return fbcontainer;
}

void Renderer::removeFromWrapper(
  rack::widget::FramebufferWidget* fb,
  rack::widget::Widget* widget
) {
  fb->children.front()->removeChild(widget);
}

float Renderer::getScaleFromVariant(
  rack::widget::FramebufferWidget* framebuffer,
  std::variant<float, int32_t> scaleOrHeight
) {
  if (std::holds_alternative<float>(scaleOrHeight)) {
    return std::get<float>(scaleOrHeight);
  } else {
    int32_t height = std::get<int32_t>(scaleOrHeight);
    float pixelRatio = std::fmax(1.f, std::floor(APP->window->pixelRatio));
    return height / (framebuffer->box.size.y * pixelRatio);
  }
}

rack::math::Vec Renderer::getScaleFromRecipe(
  rack::widget::FramebufferWidget* framebuffer,
  const Recipe& recipe
) {
  switch (recipe.type) {
    case RenderType::Scaled:
      return rack::math::Vec(recipe.scale);
    case RenderType::Exact: {
      float pixelRatio = std::fmax(1.f, std::floor(APP->window->pixelRatio));
      float yScale =
        recipe.height / (framebuffer->box.size.y * pixelRatio);
      if (recipe.width == -1) return rack::math::Vec(yScale);

      float xScale =
        recipe.width / (framebuffer->box.size.x * pixelRatio);
      return rack::math::Vec(xScale, yScale);
    }
    default:
      assert(false && "unknown Recipe.RenderType");
  }
}

uint8_t* Renderer::renderPixels(
  rack::widget::FramebufferWidget* fb,
  int& width,
  int& height,
  rack::math::Vec scale,
  bool override
) {
  fb->render(scale);
  // GL calls are async; finish here so draw and readback time separately
  BENCH(glFinish(); bench::stamp(bench::Stage::Drawn);)
  nvgluBindFramebuffer(fb->getFramebuffer());
  nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);

  float pixelRatio = std::fmax(1.f, std::floor(APP->window->pixelRatio));
  int expectedWidth =
    (int)std::ceil(std::ceil(fb->box.size.x * scale.x) * pixelRatio);
  int expectedHeight =
    (int)std::ceil(std::ceil(fb->box.size.y * scale.y) * pixelRatio);

  if (!override) {
    if (width != expectedWidth || height != expectedHeight) {
      // WARN(
      //   "renderPixels expected::actual %dx%d::%dx%d, adjusting scale and re-rendering",
      //   expectedWidth, expectedHeight, width, height
      // );

      rack::math::Vec scaleOverride = scale;
      scaleOverride.x *= (float)expectedWidth / width;
      scaleOverride.y *= (float)expectedHeight / height;

      BENCH(if (bench::Trace* trace = bench::current()) ++trace->rerenders;)
      return renderPixels(
        fb,
        width,
        height,
        scaleOverride,
        true
      );
    }
  }

  uint8_t* pixels = new uint8_t[height * width * 4];
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  BENCH(bench::stamp(bench::Stage::ReadBack);)
  flipBitmap(pixels, width, height, 4);
  BENCH(bench::stamp(bench::Stage::Flipped);)

  nvgluBindFramebuffer(NULL);
  return pixels;
}

void Renderer::flipBitmap(uint8_t* pixels, int width, int height, int depth) {
  for (int y = 0; y < height / 2; y++) {
    int flipY = height - y - 1;
    uint8_t tmp[width * depth];
    std::memcpy(tmp, &pixels[y * width * depth], width * depth);
    std::memcpy(&pixels[y * width * depth], &pixels[flipY * width * depth], width * depth);
    std::memcpy(&pixels[flipY * width * depth], tmp, width * depth);
  }
}

std::string Renderer::makeFilename(rack::app::ModuleWidget* mw) {
  std::string f = "";
  f.append(mw->getModel()->plugin->slug.c_str());
  f.append("-");
  f.append(mw->getModel()->slug.c_str());
  return f;
}

void Renderer::renderPng(
  uint8_t* pixels,
  int width,
  int height,
  std::string directory,
  std::string filename
) {
  std::string renderPath = rack::asset::user(directory);
  rack::system::createDirectory(renderPath);
  std::string filepath = rack::system::join(renderPath, filename + ".png");
  stbi_write_png(
    filepath.c_str(),
    width,
    height,
    4,
    pixels,
    width * 4
  );
}

void Renderer::hideChildren(rack::widget::Widget* widget) {
  for (auto& child : widget->children) {
    if (
      dynamic_cast<rack::app::CircularShadow*>(child)
        || dynamic_cast<rack::app::SvgScrew*>(child)
        || dynamic_cast<rack::app::ParamWidget*>(child)
        || dynamic_cast<rack::app::PortWidget*>(child)
        || dynamic_cast<rack::app::LightWidget*>(child)
    ) {
      child->visible = false;
    }
  }
}

std::map<
  std::pair<std::string, std::string>,
  std::function<bool(rack::widget::Widget*)>
> Renderer::hideChildrenVisibilityOverride = {
    {
      {"Befaco", "NoisePlethora"},
      [](rack::widget::Widget* w) {
        if (!dynamic_cast<rack::app::LightWidget*>(w)) return false;

        rack::math::Vec displayAPos =
          rack::window::mm2px(rack::math::Vec(13.106f, 38.172f));
        if (w->box.pos.equals(displayAPos)) return true;
        rack::math::Vec displayBPos =
          rack::window::mm2px(rack::math::Vec(13.106f, 50.712f));
        if (w->box.pos.equals(displayBPos)) return true;
        return false;
      }
    },
};

void Renderer::hideChildren(rack::widget::Widget* widget, std::string pluginSlug, std::string moduleSlug) {
  for (auto& child : widget->children) {
    if (
      dynamic_cast<rack::app::CircularShadow*>(child)
        || dynamic_cast<rack::app::SvgScrew*>(child)
        || dynamic_cast<rack::app::ParamWidget*>(child)
        || dynamic_cast<rack::app::PortWidget*>(child)
        || dynamic_cast<rack::app::LightWidget*>(child)
    ) {
      std::pair<std::string, std::string> slugPair(pluginSlug, moduleSlug);
      child->visible =
        hideChildrenVisibilityOverride.count(slugPair)
          ? hideChildrenVisibilityOverride.at(slugPair)(child)
          : false;
    }
  }
}
