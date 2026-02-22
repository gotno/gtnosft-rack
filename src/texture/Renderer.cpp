#include "Renderer.hpp"
#include "Catalog.hpp"
#include "../util/Util.hpp"
#include "math.hpp"

// rendering at 1x scale creates 2x box.size pixels
#define MAGIC_SCALE_MULTIPLIER 2.f

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

  switch (breadcrumbs.textureType) {
    case TextureType::Panel:
      return renderPanel(moduleWidget, recipe);
    case TextureType::Overlay:
      return renderOverlay(moduleWidget, recipe);
    case TextureType::Knob_bg:
    case TextureType::Knob_mg:
    case TextureType::Knob_fg:
      return renderKnob(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
    case TextureType::Slider_track:
    case TextureType::Slider_handle:
      return renderSlider(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
    case TextureType::Switch_frame:
      return renderSwitch(
        moduleWidget->getParam(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
    case TextureType::Port_input:
    case TextureType::Port_output:
      return renderPort(
        breadcrumbs.textureType == TextureType::Port_input
          ? moduleWidget->getInput(breadcrumbs.componentId)
          : moduleWidget->getOutput(breadcrumbs.componentId),
        breadcrumbs,
        recipe
      );
    default:
      return UNKNOWN_TEXTURE_TYPE("renderTexture", breadcrumbs);
  }
}

RenderResult Renderer::renderPanel(
  rack::app::ModuleWidget* moduleWidget,
  const Recipe& recipe
) {
  rack::widget::Widget* panel = moduleWidget->children.front();
  if (!panel) {
    std::string& pluginSlug = moduleWidget->module->getModel()->plugin->slug;
    std::string& moduleSlug = moduleWidget->module->getModel()->slug;
    return WIDGET_NOT_FOUND("renderPanel-panel", pluginSlug, moduleSlug);
  }

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(panel);

  rack::widget::Widget* parent = NULL;
  if (!framebuffer) {
    INFO("no framebuffer found for module panel, falling back to wrapped widget");
    parent = panel->parent;
    panel->parent = NULL;
    framebuffer = wrapForRendering(panel);
  }

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);
  RenderResult result = Renderer(framebuffer).render(scale);

  // if parent is not NULL, we have used wrapForRendering and need to clean up
  if (parent) {
    panel->parent = parent;
    clearRenderWrapper(framebuffer);
    delete framebuffer;
  }

  return result;
}

RenderResult Renderer::renderOverlay(
  rack::app::ModuleWidget* moduleWidget,
  const Recipe& recipe
) {
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

  rack::math::Vec scale = getScaleFromRecipe(framebuffer, recipe);

  if (track && breadcrumbs.textureType == TextureType::Slider_track) {
    track->visible = true;
    framebuffer->box.size = track->box.size;
    if (handle) handle->visible = false;

    return Renderer(framebuffer).render(scale);
  }

  if (handle && breadcrumbs.textureType == TextureType::Slider_handle) {
    if (track) track->visible = false;
    handle->visible = true;
    framebuffer->box.size = handle->box.size;

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

void Renderer::clearRenderWrapper(rack::widget::FramebufferWidget* fb) {
  fb->children.front()->children.clear();
}

float Renderer::getScaleFromVariant(
  rack::widget::FramebufferWidget* framebuffer,
  std::variant<float, int32_t> scaleOrHeight
) {
  if (std::holds_alternative<float>(scaleOrHeight)) {
    return std::get<float>(scaleOrHeight);
  } else {
    int32_t height = std::get<int32_t>(scaleOrHeight);
    return height / (framebuffer->box.size.y * 2.f);
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
      float yScale =
        recipe.height / (framebuffer->box.size.y * MAGIC_SCALE_MULTIPLIER);
      if (recipe.width == -1) return rack::math::Vec(yScale);

      float xScale =
        recipe.width / (framebuffer->box.size.x * MAGIC_SCALE_MULTIPLIER);
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
  rack::math::Vec scale
) {
  fb->render(scale);

  nvgluBindFramebuffer(fb->getFramebuffer());

  nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);

  uint8_t* pixels = new uint8_t[height * width * 4];
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  flipBitmap(pixels, width, height, 4);

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

// rack::app::ModuleWidget* Renderer::makeDummyModuleWidget(
//   rack::app::ModuleWidget* mw
// ) {
//   return mw->getModel()->createModuleWidget(NULL);
// }

// // make a new ModuleWidget and give it the old ModuleWidget's Module
// rack::app::ModuleWidget* Renderer::makeSurrogateModuleWidget(
//   rack::app::ModuleWidget* mw
// ) {
//   return mw->getModel()->createModuleWidget(mw->getModule());
// }

// std::string Renderer::makeFilename(rack::app::ModuleWidget* mw) {
//   std::string f = "";
//   f.append(mw->getModule()->getModel()->plugin->slug.c_str());
//   f.append("-");
//   f.append(mw->getModule()->getModel()->slug.c_str());
//   return f;
// }


// // render FramebufferWidget to png
// void Renderer::renderPng(
//   std::string directory,
//   std::string filename,
//   rack::widget::FramebufferWidget* fb
// ) {
//   int width, height;
//   uint8_t* pixels = renderPixels(fb, width, height);

//   std::string renderPath = rack::asset::user(directory);
//   rack::system::createDirectory(renderPath);
//   std::string filepath = rack::system::join(renderPath, filename + ".png");
//   stbi_write_png(
//     filepath.c_str(),
//     width,
//     height,
//     4,
//     pixels,
//     width * 4
//   );

//   delete[] pixels;
//   nvgluBindFramebuffer(NULL);
// }

// remove params/ports/lights/screws/shadows from children
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

// // remove children->front() from ModuleWidget, which should be Internal->panel
// void Renderer::abandonPanel(rack::app::ModuleWidget* mw) {
//   auto it = mw->children.begin();
//   mw->children.erase(it);
// }

// // render module without actual data, as in library preview, save
// void Renderer::saveModulePreviewRender(
//   rack::app::ModuleWidget* moduleWidget
// ) {
//   widget::FramebufferWidget* fb =
//     wrapForRendering(
//       makeDummyModuleWidget(moduleWidget)
//     );

//   renderPng("render_module_preview", makeFilename(moduleWidget), fb);

//   delete fb;
// }

// // render only panel framebuffer, save
// void Renderer::savePanelRender(
//   rack::app::ModuleWidget* moduleWidget,
//   float zoom
// ) {
//   rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);
//   renderPng("render_panel_framebuffer", makeFilename(moduleWidget), fb);
// }

// // render only panel framebuffer, compress & send
// void Renderer::sendPanelRender(
//   rack::app::ModuleWidget* moduleWidget,
//   float zoom
// ) {
//   rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

//   int width, height;
//   uint8_t* pixels = renderPixels(fb, width, height, zoom);
// }

// // render only panel framebuffer, send
// void Renderer::sendPanelRenderUncompressed(
//   rack::app::ModuleWidget* moduleWidget,
//   float zoom
// ) {
//   rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

//   int width, height;
//   uint8_t* pixels = renderPixels(fb, width, height, zoom);
// }

// // render module without panel or params/ports/lights, compress & send
// // TODO: probably more efficient to hold on to the surrogate and update its module each time
// int32_t Renderer::sendOverlayRender(
//   rack::app::ModuleWidget* moduleWidget,
//   float zoom
// ) {
//   rack::app::ModuleWidget* surrogate = makeSurrogateModuleWidget(moduleWidget);
//   widget::FramebufferWidget* fb = wrapForRendering(surrogate);
//   abandonChildren(surrogate);
//   abandonPanel(surrogate);

//   int width, height;
//   uint8_t* pixels = renderPixels(fb, width, height, zoom);

//   surrogate->module = NULL;
//   delete fb;
// }

// rack::widget::FramebufferWidget* Renderer::getPanelFramebuffer(
//   rack::app::ModuleWidget* moduleWidget
// ) {
//   rack::widget::Widget* panel = moduleWidget->children.front();
//   if (!panel) return NULL;

//   rack::widget::FramebufferWidget* fb =
//     dynamic_cast<rack::widget::FramebufferWidget*>(panel->children.front());
//   if (!fb) return NULL;

//   return fb;
// }

// // void Renderer::refreshModuleWidgets() {
// //   moduleWidgets.clear();

// //   for (int64_t& moduleId: APP->engine->getModuleIds()) {
// //     rack::app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);

// //     std::string key = "";
// //     key.append(mw->getModule()->getModel()->plugin->slug.c_str());
// //     key.append("-");
// //     key.append(mw->getModule()->getModel()->name.c_str());

// //     moduleWidgets.emplace(key, mw);
// //   }
// // }

// void Renderer::logChildren(std::string& name, rack::app::ModuleWidget* mw) {
//   DEBUG("\n\n%s", name.c_str());
//   for (rack::widget::Widget* widget : mw->children) {
//     DEBUG(
//       "size %fx/%fy, pos %fx/%fy",
//       widget->getSize().x,
//       widget->getSize().y,
//       widget->getPosition().x,
//       widget->getPosition().y
//     );

//     if (dynamic_cast<rack::app::SvgButton*>(widget))
//       DEBUG("  is SvgButton");
//     if (dynamic_cast<rack::app::SvgKnob*>(widget))
//       DEBUG("  is SvgKnob");
//     if (dynamic_cast<rack::app::SvgPanel*>(widget))
//       DEBUG("  is SvgPanel");
//     if (dynamic_cast<rack::app::SvgPort*>(widget))
//       DEBUG("  is SvgPort");
//     if (dynamic_cast<rack::app::SvgScrew*>(widget))
//       DEBUG("  is SvgScrew");
//     if (dynamic_cast<rack::app::SvgSlider*>(widget))
//       DEBUG("  is SvgSlider");
//     if (dynamic_cast<rack::app::SvgSwitch*>(widget))
//       DEBUG("  is SvgSwitch");

//     if (dynamic_cast<rack::app::SliderKnob*>(widget))
//       DEBUG("  is SliderKnob");
//     if (dynamic_cast<rack::app::Knob*>(widget))
//       DEBUG("  is Knob");

//     if (dynamic_cast<rack::app::Switch*>(widget))
//       DEBUG("  is Switch");

//     if (dynamic_cast<rack::app::LedDisplay*>(widget))
//       DEBUG("  is LedDisplay");

//     if (dynamic_cast<rack::app::ModuleLightWidget*>(widget))
//       DEBUG("  is ModuleLightWidget");
//     if (dynamic_cast<rack::app::MultiLightWidget*>(widget))
//       DEBUG("  is MultiLightWidget");
//     if (dynamic_cast<rack::app::LightWidget*>(widget))
//       DEBUG("  is LightWidget");

//     if (dynamic_cast<rack::app::ParamWidget*>(widget))
//       DEBUG("  is ParamWidget");
//     if (dynamic_cast<rack::app::PortWidget*>(widget))
//       DEBUG("  is PortWidget");
//   }
// }
