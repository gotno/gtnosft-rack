#include "Renderer.hpp"

#include "../util/Util.hpp"

// static
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

// static
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

// static
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

// static
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

// static
RenderResult Renderer::renderPanel(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  float scale
) {
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  if (!model) return MODEL_NOT_FOUND("renderPanel", pluginSlug, moduleSlug);

  rack::app::ModuleWidget* moduleWidget = gtnosft::util::makeModuleWidget(model);
  if (!moduleWidget) return MODULE_WIDGET_ERROR("renderPanel", pluginSlug, moduleSlug);
  DEFER({ delete moduleWidget; });

  rack::widget::Widget* panel = moduleWidget->children.front();
  if (!panel)
    return WIDGET_NOT_FOUND("renderPanel-panel", pluginSlug, moduleSlug);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(panel);
  // TODO: brute force this if no fb found. _something_ is still renderable.
  //       use wrapModuleWidget?
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderPanel-fb", pluginSlug, moduleSlug);

  RenderResult result = Renderer(framebuffer).render(scale);

  return result;
}

// static
RenderResult Renderer::renderOverlay(int64_t moduleId, float scale) {
  rack::app::ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  if (!moduleWidget) return MODULE_NOT_FOUND("renderOverlay", moduleId);

  rack::app::ModuleWidget* surrogate =
    moduleWidget->getModel()->createModuleWidget(moduleWidget->getModule());

  // hide panel
  surrogate->children.front()->setVisible(false);

  // this.. is different from hideChildren?
  // hide params/ports/lights/screws
  for (auto it = surrogate->children.begin(); it != surrogate->children.end(); ++it) {
    if (dynamic_cast<rack::app::SvgScrew*>(*it) || dynamic_cast<rack::app::ParamWidget*>(*it) || dynamic_cast<rack::app::PortWidget*>(*it) || dynamic_cast<rack::app::LightWidget*>(*it)) {
      (*it)->setVisible(false);
    }
  }

  // TODO: for Fundamental:Scope, copy input cables to get proper colors
  rack::widget::FramebufferWidget* framebuffer = wrapForRendering(surrogate);
  framebuffer->step();
  DEFER({
    surrogate->module = NULL;
    delete framebuffer;
  });

  RenderResult result = Renderer(framebuffer).render(scale);

  return result;
}

// static
RenderResult Renderer::renderSwitch(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  std::vector<RenderResult>& renderResults,
  float scale
) {
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  if (!model) {
    MODEL_NOT_FOUND("renderSwitch", pluginSlug, moduleSlug);
  }

  rack::app::ModuleWidget* moduleWidget =
    gtnosft::util::makeConnectedModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR("renderSwitch", pluginSlug, moduleSlug);
  DEFER({ delete moduleWidget; });

  rack::app::ParamWidget* switchWidget = moduleWidget->getParam(id);
  if (!switchWidget)
    return WIDGET_NOT_FOUND("renderSwitch-param", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(switchWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderSwitch-fb", pluginSlug, moduleSlug, id);

  hideChildren(framebuffer);

  rack::engine::ParamQuantity* pq = switchWidget->getParamQuantity();
  pq->setMin();

  for (int i = 0; i <= pq->getMaxValue(); ++i) {
    pq->setValue(i);
    switchWidget->step();
    renderResults.push_back(
      Renderer(framebuffer).render(scale)
    );
  }

  return RenderResult();
};

// static
RenderResult Renderer::renderSlider(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  std::map<std::string, RenderResult>& renderResults,
  float scale
) {
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  if (!model) {
    MODEL_NOT_FOUND("renderSlider", pluginSlug, moduleSlug);
  }

  rack::app::ModuleWidget* moduleWidget = gtnosft::util::makeModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR("renderSlider", pluginSlug, moduleSlug);
  DEFER({ delete moduleWidget; });

  rack::app::SvgSlider* sliderWidget =
    dynamic_cast<rack::app::SvgSlider*>(moduleWidget->getParam(id));
  if (!sliderWidget)
    return WIDGET_NOT_FOUND("renderSlider-param", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(sliderWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderSlider-fb", pluginSlug, moduleSlug, id);

  hideChildren(framebuffer);

  rack::widget::Widget* track = sliderWidget->background;
  rack::widget::Widget* handle = sliderWidget->handle;

  if (track) {
    track->visible = true;
    framebuffer->box.size = track->box.size;
    if (handle) handle->visible = false;

    renderResults["track"] = Renderer(framebuffer).render(scale);
  }

  if (handle) {
    if (track) track->visible = false;
    handle->visible = true;
    framebuffer->box.size = handle->box.size;

    renderResults["handle"] = Renderer(framebuffer).render(scale);
  }

  return RenderResult();
}

// static
RenderResult Renderer::renderKnob(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  std::map<std::string, RenderResult>& renderResults,
  float scale
) {
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  if (!model) {
    MODEL_NOT_FOUND("renderKnob", pluginSlug, moduleSlug);
  }

  rack::app::ModuleWidget* moduleWidget = gtnosft::util::makeModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR("renderKnob", pluginSlug, moduleSlug);
  DEFER({ delete moduleWidget; });

  rack::widget::Widget* knobWidget = moduleWidget->getParam(id);
  if (!knobWidget)
    return WIDGET_NOT_FOUND("renderKnob-param", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(knobWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderKnob-fb", pluginSlug, moduleSlug, id);

  hideChildren(framebuffer);

  rack::widget::Widget* bg{NULL};
  rack::widget::Widget* mg{NULL};
  rack::widget::Widget* fg{NULL};
  rack::widget::Widget* lastWidget{NULL};

  // find each layer's widget
  for (auto& child : framebuffer->children) {
    // if this is the TransformWidget and there was a widget before, it must be the background
    if (lastWidget && dynamic_cast<rack::widget::TransformWidget*>(child)) {
      bg = lastWidget;
    }
    // the child of the transform widget is the midground
    if (dynamic_cast<rack::widget::TransformWidget*>(child)) {
      mg = child->children.front();
    }
    // if the last widget was a TransformWidget and we're still iterating, this must be the foreground
    if (dynamic_cast<rack::widget::TransformWidget*>(lastWidget)) {
      fg = child;
    }

    lastWidget = child;
  }

  // toggle layer visibility and render each
  if (bg) {
    bg->visible = true;
    if (mg) mg->visible = false;
    if (fg) fg->visible = false;

    renderResults["bg"] = Renderer(framebuffer).render(scale);
  }

  if (mg) {
    if (bg) bg->visible = false;
    mg->visible = true;
    if (fg) fg->visible = false;

    renderResults["mg"] = Renderer(framebuffer).render(scale);
  }

  if (fg) {
    if (bg) bg->visible = false;
    if (mg) mg->visible = false;
    fg->visible = true;

    renderResults["fg"] = Renderer(framebuffer).render(scale);
  }

  return RenderResult();
}

// static
RenderResult Renderer::renderPort(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  rack::engine::Port::Type type,
  float scale
) {
  rack::plugin::Model* model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  if (!model) return MODEL_NOT_FOUND("renderPort", pluginSlug, moduleSlug);

  rack::app::ModuleWidget* moduleWidget = gtnosft::util::makeModuleWidget(model);
  if (!moduleWidget) return MODULE_WIDGET_ERROR("renderPort", pluginSlug, moduleSlug);
  DEFER({ delete moduleWidget; });

  rack::widget::Widget* portWidget = NULL;
  portWidget =
    type == rack::engine::Port::INPUT
      ? moduleWidget->getInput(id)
      : moduleWidget->getOutput(id);

  if (!portWidget)
    return WIDGET_NOT_FOUND("renderPort-port", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(portWidget);

  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderPort-fb", pluginSlug, moduleSlug, id);

  hideChildren(framebuffer);
  RenderResult result = Renderer(framebuffer).render(scale);

  return result;
}

// static
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

RenderResult Renderer::render(float scale) {
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

// static
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

// static
void Renderer::clearRenderWrapper(rack::widget::FramebufferWidget* fb) {
  fb->children.front()->children.clear();
}

uint8_t* Renderer::renderPixels(
  rack::widget::FramebufferWidget* fb,
  int& width,
  int& height,
  float zoom
) {
  fb->render(rack::math::Vec(zoom, zoom));

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

// static
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
