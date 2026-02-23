#include "Catalog.hpp"
#include "Renderer.hpp"

// ParamType, PortType
#include "../osc/Bundler/ModuleStructureBundler.hpp"

#include "rapidhash/rapidhash.h"

int64_t Catalog::ingest(Breadcrumbs breadcrumbs) {
  RenderResult render = Renderer::renderTexture(breadcrumbs, Recipe(8, 8));

  if (render.empty() || render.failure()) {
    INFO("Catalog::ingest %s", render.empty() ? "empty" : "failure");
    return -1;
  }

  uint64_t hash = hashBitmap(render.pixels);

  INFO("Catalog::ingest cache: %s", registry.contains(hash) ? "hit" : "miss");

  if (!registry.contains(hash)) {
    int64_t textureId = -1;

    while (textureId < 0 || textureBreadcrumbs.contains(textureId))
      textureId = rack::random::u64() % (1ull << 53);

    registry.emplace(hash, textureId);
    textureBreadcrumbs.emplace(textureId, breadcrumbs);
  }

  delete[] render.pixels;

  return registry.at(hash);
}

uint64_t Catalog::hashBitmap(uint8_t* pixels) {
  return rapidhash(pixels, 256);
}

RenderResult Catalog::pullTexture(uint64_t id, Recipe recipe) {
  if (!textureBreadcrumbs.contains(id)) return RenderResult();
  return Renderer::renderTexture(textureBreadcrumbs.at(id), recipe);
}

std::vector<int64_t> Catalog::pullIds(ParamType type, rack::app::ParamWidget* widget) {
  std::vector<int64_t> textureIds;

  switch (type) {
    case ParamType::Knob:
      {
        std::vector<TextureType> types{
          TextureType::Knob_bg,
          TextureType::Knob_mg,
          TextureType::Knob_fg,
        };
        for (TextureType& type : types) {
          textureIds.push_back(Catalog::ingest(
            Breadcrumbs(
              widget->module->model->plugin->slug,
              widget->module->model->slug,
              widget->paramId,
              type
            )
          ));
        }
      }
      break;
    case ParamType::Slider:
      {
        std::vector<TextureType> types{
          TextureType::Slider_track,
          TextureType::Slider_handle
        };
        for (TextureType& type : types) {
          textureIds.push_back(Catalog::ingest(
            Breadcrumbs(
              widget->module->model->plugin->slug,
              widget->module->model->slug,
              widget->paramId,
              type
            )
          ));
        }
      }
      break;
    case ParamType::Button:
    case ParamType::Switch:
      {
        rack::engine::ParamQuantity* pq = widget->getParamQuantity();
        for (int i = 0; i <= pq->getMaxValue(); ++i) {
          textureIds.push_back(Catalog::ingest(
            Breadcrumbs(
              widget->module->model->plugin->slug,
              widget->module->model->slug,
              widget->paramId,
              TextureType::Switch_frame,
              i
            )
          ));
        }
      }
      break;
    case ParamType::Unknown:
      break;
  }

  return textureIds;
}

int64_t Catalog::pullId(PortType portType, rack::app::PortWidget* widget) {
  TextureType textureType;
  if (portType == PortType::Input) textureType = TextureType::Port_input;
  if (portType == PortType::Output) textureType = TextureType::Port_output;

  return Catalog::ingest(
    Breadcrumbs(
      widget->module->model->plugin->slug,
      widget->module->model->slug,
      widget->portId,
      textureType
    )
  );
}
