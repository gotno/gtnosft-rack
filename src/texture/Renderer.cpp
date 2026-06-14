#include "Renderer.hpp"
#include "Catalog.hpp"
#include "../util/Util.hpp"
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

  RenderResult result;

  switch (breadcrumbs.textureType) {
    case TextureType::Panel:
      result = renderPanel(moduleWidget, recipe);
      if (result.success()) {
        renderPng(
          result.pixels,
          result.width,
          result.height,
          "render_panel_test",
          breadcrumbs.moduleSlug
        );
      }
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

  rack::app::ModuleWidget* surrogate =
    moduleWidget->getModel()->createModuleWidget(moduleWidget->getModule());

  surrogate->children.front()->setVisible(false); // panel
  hideChildren(surrogate);

  // TODO: for Fundamental:Scope, copy input cables to get proper colors
  rack::widget::FramebufferWidget* framebuffer = wrapForRendering(surrogate);
  framebuffer->step();
  DEFER({
    surrogate->module = NULL;
    delete framebuffer;
  });

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);
  return Renderer(framebuffer).render(scale);
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
  nvgluBindFramebuffer(fb->getFramebuffer());
  nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);

  float pixelRatio = std::fmax(1.f, std::floor(APP->window->pixelRatio));
  int expectedWidth =
    (int)std::ceil(std::ceil(fb->box.size.x * scale.x) * pixelRatio);
  int expectedHeight =
    (int)std::ceil(std::ceil(fb->box.size.y * scale.y) * pixelRatio);

  if (!override) {
    if (width != expectedWidth || height != expectedHeight) {
      WARN(
        "renderPixels expected::actual %dx%d::%dx%d, adjusting scale and re-rendering",
        expectedWidth, expectedHeight, width, height
      );

      rack::math::Vec scaleOverride = scale;
      scaleOverride.x *= (float)expectedWidth / width;
      scaleOverride.y *= (float)expectedHeight / height;

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
  flipBitmap(pixels, width, height, 4);

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

// render pixels to png
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
