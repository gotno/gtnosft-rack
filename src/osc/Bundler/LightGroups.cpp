#include "LightGroups.hpp"

#include <cmath>

namespace {

constexpr float BOX_EPSILON = 1e-3f;

bool sameBox(const rack::math::Rect& a, const rack::math::Rect& b) {
  return std::fabs(a.pos.x - b.pos.x) < BOX_EPSILON
    && std::fabs(a.pos.y - b.pos.y) < BOX_EPSILON
    && std::fabs(a.size.x - b.size.x) < BOX_EPSILON
    && std::fabs(a.size.y - b.size.y) < BOX_EPSILON;
}

void addToGroups(
  std::vector<LightGroup>& groups,
  rack::app::LightWidget* lightWidget,
  int32_t paramId
) {
  for (LightGroup& group : groups) {
    if (group.paramId == paramId && sameBox(group.front()->box, lightWidget->box)) {
      group.widgets.push_back(lightWidget);
      return;
    }
  }
  groups.push_back(LightGroup{paramId, {lightWidget}});
}

NVGcolor sourceOver(NVGcolor dst, NVGcolor src) {
  float a = src.a + dst.a * (1.f - src.a);
  if (a <= 0.f) return nvgRGBAf(0.f, 0.f, 0.f, 0.f);

  float dw = dst.a * (1.f - src.a);
  return nvgRGBAf(
    (src.r * src.a + dst.r * dw) / a,
    (src.g * src.a + dst.g * dw) / a,
    (src.b * src.a + dst.b * dw) / a,
    a
  );
}

} // namespace

bool LightGroup::isVisible() const {
  for (auto* widget : widgets)
    if (widget->isVisible()) return true;
  return false;
}

NVGcolor LightGroup::color() const {
  if (widgets.size() == 1) return widgets.front()->color;

  NVGcolor c = nvgRGBAf(0.f, 0.f, 0.f, 0.f);
  for (auto* widget : widgets)
    if (widget->isVisible()) c = rack::color::screen(c, widget->color);
  return c;
}

NVGcolor LightGroup::bgColor() const {
  if (widgets.size() == 1) return widgets.front()->bgColor;

  bool anyVisible = isVisible();
  NVGcolor c = nvgRGBAf(0.f, 0.f, 0.f, 0.f);
  for (auto* widget : widgets)
    if (!anyVisible || widget->isVisible()) c = sourceOver(c, widget->bgColor);
  return c;
}

std::vector<LightGroup> collectLightGroups(rack::app::ModuleWidget* moduleWidget) {
  using namespace rack::app;
  using namespace rack::widget;

  std::vector<LightGroup> groups;

  // panel lights
  for (Widget* widget : moduleWidget->children) {
    if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget))
      addToGroups(groups, lightWidget, -1);
  }

  // param lights
  for (ParamWidget* paramWidget : moduleWidget->getParams()) {
    rack::engine::ParamQuantity* pq = paramWidget->getParamQuantity();
    int32_t paramId = pq ? pq->paramId : -1;
    for (Widget* widget : paramWidget->children) {
      if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget))
        addToGroups(groups, lightWidget, paramId);
    }
  }

  return groups;
}
