#include "test.hpp"
#include "util/ParamVisibility.hpp"

namespace visibility = gtnosft::param_visibility;

TEST(param_visibility_last_visible_control_wins_each_stack) {
  visibility::Controls controls = {
    {10.f, 20.f, true},
    {50.f, 60.f, true},
    {10.f, 20.f, true},
    {10.f, 20.f, true},
  };
  visibility::stackedControls(controls);
  CHECK(!controls[0].visible);
  CHECK(controls[1].visible);
  CHECK(!controls[2].visible);
  CHECK(controls[3].visible);
}

TEST(param_visibility_hidden_replacements_do_not_suppress_base_controls) {
  visibility::Controls controls = {
    {10.f, 20.f, true},
    {10.f, 20.f, false},
    {10.f, 20.f, false},
  };
  visibility::stackedControls(controls);
  CHECK(controls[0].visible);
  CHECK(!controls[1].visible);
  CHECK(!controls[2].visible);
}

TEST(param_visibility_stacks_use_center_tolerance_not_partial_overlap) {
  visibility::Controls controls = {
    {10.f, 20.f, true},
    {10.0005f, 19.9995f, true},
    {10.002f, 20.f, true},
    {10.f, 20.002f, true},
  };
  visibility::stackedControls(controls);
  CHECK(!controls[0].visible);
  CHECK(controls[1].visible);
  CHECK(controls[2].visible);
  CHECK(controls[3].visible);
}

TEST(param_visibility_mode_changes_select_only_the_active_layer) {
  for (int active = 0; active < 5; ++active) {
    visibility::Controls controls = {
      {10.f, 20.f, true},
      {10.f, 20.f, active == 1},
      {10.f, 20.f, active == 2},
      {10.f, 20.f, active == 3},
      {10.f, 20.f, active == 4},
    };
    visibility::stackedControls(controls);
    for (int i = 0; i < 5; ++i)
      CHECK(controls[i].visible == (i == active));
  }
}

TEST(param_visibility_empty_and_fully_hidden_stacks_stay_hidden) {
  visibility::Controls empty;
  visibility::stackedControls(empty);
  CHECK(empty.empty());
  visibility::Controls controls = {
    {10.f, 20.f, false},
    {10.f, 20.f, false},
  };
  visibility::stackedControls(controls);
  CHECK(!controls[0].visible);
  CHECK(!controls[1].visible);
}
