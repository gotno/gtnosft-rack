#include "Renderer.hpp"

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
  const std::string& moduleSlug
) {
  rack::plugin::Model* model = findModel(pluginSlug, moduleSlug);
  if (!model) return MODEL_NOT_FOUND("renderPanel", pluginSlug, moduleSlug);

  rack::app::ModuleWidget* moduleWidget = makeModuleWidget(model);
  if (!moduleWidget) return MODULE_WIDGET_ERROR("renderPanel", pluginSlug, moduleSlug);

  abandonChildren(moduleWidget);
  rack::widget::FramebufferWidget* framebuffer = wrapWidget(moduleWidget);

  RenderResult result = Renderer(framebuffer).render();
  delete framebuffer;

  return result;
}

// static
RenderResult Renderer::renderSwitch(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  std::vector<RenderResult>& renderResults
) {
  rack::plugin::Model* model = findModel(pluginSlug, moduleSlug);
  if (!model) {
    MODEL_NOT_FOUND("renderSwitch", pluginSlug, moduleSlug);
  }

  rack::app::ModuleWidget* moduleWidget = makeConnectedModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR("renderSwitch", pluginSlug, moduleSlug);

  rack::app::ParamWidget* switchWidget = moduleWidget->getParam(id);
  if (!switchWidget)
    return WIDGET_NOT_FOUND("renderSwitch-param", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(switchWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderSwitch-fb", pluginSlug, moduleSlug, id);

  abandonChildren(framebuffer);

  rack::engine::ParamQuantity* pq = switchWidget->getParamQuantity();
  pq->setMin();

  for (int i = 0; i <= pq->getMaxValue(); ++i) {
    pq->setValue(i);
    switchWidget->step();
    renderResults.push_back(
      Renderer(framebuffer).render()
    );
  }

  delete moduleWidget;
  return RenderResult();
};

// static
RenderResult Renderer::renderKnob(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  std::map<std::string, RenderResult>& renderResults
) {
  rack::plugin::Model* model = findModel(pluginSlug, moduleSlug);
  if (!model) {
    MODEL_NOT_FOUND("renderKnob", pluginSlug, moduleSlug);
  }

  rack::app::ModuleWidget* moduleWidget = makeModuleWidget(model);
  if (!moduleWidget)
    return MODULE_WIDGET_ERROR("renderKnob", pluginSlug, moduleSlug);

  rack::widget::Widget* knobWidget = moduleWidget->getParam(id);
  if (!knobWidget)
    return WIDGET_NOT_FOUND("renderKnob-param", pluginSlug, moduleSlug, id);

  rack::widget::FramebufferWidget* framebuffer = findFramebuffer(knobWidget);
  if (!framebuffer)
    return WIDGET_NOT_FOUND("renderKnob-fb", pluginSlug, moduleSlug, id);

  knobWidget->removeChild(framebuffer);
  delete moduleWidget;

  abandonChildren(framebuffer);

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

    renderResults["bg"] = Renderer(framebuffer).render();
  }

  if (mg) {
    if (bg) bg->visible = false;
    mg->visible = true;
    if (fg) fg->visible = false;

    renderResults["mg"] = Renderer(framebuffer).render();
  }

  if (fg) {
    if (bg) bg->visible = false;
    if (mg) mg->visible = false;
    fg->visible = true;

    renderResults["fg"] = Renderer(framebuffer).render();
  }

  delete framebuffer;
  return RenderResult();
}

// static
RenderResult Renderer::renderPort(
  const std::string& pluginSlug,
  const std::string& moduleSlug,
  int32_t id,
  rack::engine::Port::Type type
) {
  rack::plugin::Model* model = findModel(pluginSlug, moduleSlug);
  if (!model) return MODEL_NOT_FOUND("renderPort", pluginSlug, moduleSlug);

  rack::app::ModuleWidget* moduleWidget = makeModuleWidget(model);
  if (!moduleWidget) return MODULE_WIDGET_ERROR("renderPort", pluginSlug, moduleSlug);

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

  portWidget->removeChild(framebuffer);
  delete moduleWidget;

  abandonChildren(framebuffer);
  RenderResult result = Renderer(framebuffer).render();

  delete framebuffer;
  return result;
}

// static
rack::plugin::Model* Renderer::findModel(
  const std::string& pluginSlug,
  const std::string& moduleSlug
) {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }
  return NULL;
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

// static
// static rack::app::ModuleWidget* Renderer::getModuleWidget(int64_t moduleId) {
// }

// static
rack::app::ModuleWidget* Renderer::makeModuleWidget(rack::plugin::Model* model) {
  return model->createModuleWidget(NULL);
}

// static
rack::app::ModuleWidget* Renderer::makeConnectedModuleWidget(
  rack::plugin::Model* model
) {
  rack::engine::Module* module = model->createModule();
  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(module);
  APP->engine->addModule(module);
  return moduleWidget;
}

Renderer::Renderer(rack::widget::FramebufferWidget* _framebuffer):
  framebuffer(_framebuffer) {}
Renderer::~Renderer() {}

RenderResult Renderer::render() {
  try {
    int width, height;
    uint8_t* pixels = renderPixels(framebuffer, width, height, 3.f);

    return RenderResult(pixels, width, height);
  } catch (std::exception& e) {
    return RenderResult(e.what());
  } catch (...) {
    return RenderResult("unknown Renderer error");
  }
}

// static
rack::widget::FramebufferWidget* Renderer::wrapWidget(
  rack::widget::Widget* widget
) {
  rack::widget::FramebufferWidget* fbcontainer =
    new rack::widget::FramebufferWidget;
  ModuleWidgetContainer* container = new ModuleWidgetContainer;

  fbcontainer->addChild(container);
  container->box.size = widget->box.size;
  fbcontainer->box.size = widget->box.size;
  container->addChild(widget);

  return fbcontainer;
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
void Renderer::abandonChildren(rack::widget::Widget* widget) {
  std::vector<rack::widget::Widget*> childrenToAbandon;

  for (auto& child : widget->children) {
    if (
      dynamic_cast<rack::app::CircularShadow*>(child)
        || dynamic_cast<rack::app::SvgScrew*>(child)
        || dynamic_cast<rack::app::ParamWidget*>(child)
        || dynamic_cast<rack::app::PortWidget*>(child)
        || dynamic_cast<rack::app::LightWidget*>(child)
    ) {
      childrenToAbandon.push_back(child);
    }
  }

  for (auto& child : childrenToAbandon) {
    widget->removeChild(child);
    delete child;
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
//     wrapModuleWidget(
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
//   widget::FramebufferWidget* fb = wrapModuleWidget(surrogate);
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
