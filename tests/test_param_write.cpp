#include "test.hpp"
#include "osc/ParamWrite.hpp"

#include <cmath>
#include <limits>

namespace pw = gtnosft::param_write;

TEST(param_write_only_overrides_the_noise_plethora_encoder) {
  const auto* entry = pw::findOverride("Befaco", "NoisePlethora", 6);
  CHECK(entry);
  CHECK(!pw::findOverride("Befaco", "NoisePlethora", 5));
  CHECK(!pw::findOverride("Befaco", "Muxlicer", 6));
  CHECK(!pw::findOverride("Other", "NoisePlethora", 6));
  if (!entry) return;
  pw::EncoderState state(0.f, entry->stepSize);
  CHECK(state.setTarget(0.124f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(0.125f));
  CHECK_EQ(state.appliedValue, 0.125f);
}

TEST(param_write_encoder_activates_for_small_and_large_absolute_changes) {
  CHECK(pw::planDrag(0.f, 0.001f, 0.125f) == pw::DragAction::SetValue);
  CHECK(pw::planDrag(0.f, 0.125f, 0.125f) == pw::DragAction::SetValue);
  CHECK(pw::planDrag(0.5f, 0.625f, 0.125f) == pw::DragAction::SetValue);
  CHECK(pw::planDrag(0.5f, 0.25f, 0.125f) == pw::DragAction::SetValue);
}

TEST(param_write_unchanged_encoder_value_does_not_start_a_drag) {
  CHECK(pw::planDrag(0.5f, 0.5f, 0.125f) == pw::DragAction::Unchanged);
}

TEST(param_write_encoder_rejects_nonfinite_or_excessive_step_counts) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (float invalid : {nan, inf, -inf}) {
    CHECK(pw::planDrag(invalid, 0.f, 0.125f) == pw::DragAction::Invalid);
    CHECK(pw::planDrag(0.f, invalid, 0.125f) == pw::DragAction::Invalid);
    CHECK(pw::planDrag(invalid, invalid, 0.125f) == pw::DragAction::Invalid);
  }
  const float max = std::numeric_limits<float>::max();
  CHECK(pw::planDrag(-max, max, 0.125f) == pw::DragAction::Invalid);
  CHECK(pw::planDrag(0.f, 268435456.f, 0.125f) == pw::DragAction::Invalid);
  CHECK(pw::planDrag(0.f, -268435456.f, 0.125f) == pw::DragAction::Invalid);
}

TEST(param_write_encoder_preserves_fractional_updates_and_duplicate_final_values) {
  pw::EncoderState state(0.f, 0.125f);
  CHECK(state.setTarget(0.040f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(0.090f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(0.130f));
  CHECK_EQ(state.appliedValue, 0.125f);
  CHECK_EQ(state.logicalValue, 0.130f);
  CHECK(state.setTarget(0.130f));
  CHECK_EQ(state.appliedValue, 0.125f);
  CHECK_EQ(state.logicalValue, 0.130f);
  CHECK(state.setTarget(0.250f));
  CHECK_EQ(state.appliedValue, 0.250f);
}

TEST(param_write_encoder_full_rotation_does_not_depend_on_update_frequency) {
  for (int direction : {-1, 1}) {
    for (int frames : {8, 60, 90, 144, 1000}) {
      pw::EncoderState state(0.f, 0.125f);
      float reference = 0.f;
      int totalSteps = 0;
      for (int frame = 1; frame <= frames; ++frame) {
        float target = direction
          * std::round(float(frame) / frames * 1000.f) * 0.001f;
        CHECK(state.setTarget(target));
        int steps = int(8.f * (state.appliedValue - reference));
        if (steps != 0) reference = state.appliedValue;
        totalSteps += steps;
      }
      CHECK_EQ(totalSteps, direction * 8);
      CHECK_EQ(state.appliedValue, float(direction));
      CHECK_EQ(state.logicalValue, float(direction));
    }
  }
}

TEST(param_write_encoder_preserves_remainders_through_direction_changes) {
  pw::EncoderState state(0.f, 0.125f);
  CHECK(state.setTarget(0.130f));
  CHECK_EQ(state.appliedValue, 0.125f);
  CHECK(state.setTarget(0.010f));
  CHECK_EQ(state.appliedValue, 0.125f);
  CHECK(state.setTarget(0.f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(-0.130f));
  CHECK_EQ(state.appliedValue, -0.125f);
  CHECK_EQ(state.logicalValue, -0.130f);
  CHECK(state.setTarget(-0.250f));
  CHECK_EQ(state.appliedValue, -0.250f);
}

TEST(param_write_encoder_rebases_logical_state_after_native_edits) {
  pw::EncoderState state(0.f, 0.125f);
  CHECK(state.setTarget(0.130f));
  state.observe(0.125f);
  CHECK_EQ(state.logicalValue, 0.130f);
  state.observe(0.5f);
  CHECK_EQ(state.logicalValue, 0.5f);
  CHECK_EQ(state.appliedValue, 0.5f);
  CHECK(state.setTarget(0.625f));
  CHECK_EQ(state.appliedValue, 0.625f);

  CHECK(state.setTarget(0.650f));
  state.observe(0.625f, true);
  CHECK_EQ(state.logicalValue, 0.625f);
  CHECK_EQ(state.appliedValue, 0.625f);
}

TEST(param_write_encoder_rejected_targets_leave_logical_and_applied_values_unchanged) {
  pw::EncoderState state(0.f, 0.125f);
  CHECK(state.setTarget(0.130f));
  for (float invalid : {
    std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::infinity(),
    268435456.f
  }) {
    CHECK(!state.setTarget(invalid));
    CHECK_EQ(state.logicalValue, 0.130f);
    CHECK_EQ(state.appliedValue, 0.125f);
  }
}

TEST(param_write_encoder_native_fractional_baselines_do_not_lose_steps) {
  pw::EncoderState state(-0.13f, 0.125f);
  CHECK(state.setTarget(-0.005f));
  CHECK_EQ(int(8.f * (state.appliedValue - -0.13f)), 1);
  CHECK_EQ(state.logicalValue, -0.005f);

  pw::EncoderState reverse(0.13f, 0.125f);
  CHECK(reverse.setTarget(0.005f));
  CHECK_EQ(int(8.f * (reverse.appliedValue - 0.13f)), -1);
  CHECK_EQ(reverse.logicalValue, 0.005f);
}

TEST(param_write_encoder_does_not_round_substep_targets_to_full_steps) {
  pw::EncoderState state(0.f, 0.125f);
  CHECK(state.setTarget(0.124999f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(-0.124999f));
  CHECK_EQ(state.appliedValue, 0.f);
}

TEST(param_write_encoder_rejects_steps_that_cannot_be_represented_by_the_module) {
  pw::EncoderState state(0.125f, 0.125f);
  CHECK(!state.setTarget(2097152.25f));
  CHECK_EQ(state.appliedValue, 0.125f);
  CHECK_EQ(state.logicalValue, 0.125f);
}

TEST(param_write_encoder_accumulates_using_its_configured_step_size) {
  pw::EncoderState state(0.f, 0.25f);
  CHECK(state.setTarget(0.130f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(0.260f));
  CHECK_EQ(state.appliedValue, 0.25f);
  CHECK_EQ(state.logicalValue, 0.260f);
  CHECK(state.setTarget(0.760f));
  CHECK_EQ(state.appliedValue, 0.75f);
  CHECK(state.setTarget(-0.010f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(-0.260f));
  CHECK_EQ(state.appliedValue, -0.25f);
}

TEST(param_write_encoder_handles_decimal_step_sizes) {
  pw::EncoderState state(0.f, 0.1f);
  CHECK(state.setTarget(0.090f));
  CHECK_EQ(state.appliedValue, 0.f);
  CHECK(state.setTarget(0.310f));
  CHECK(std::fabs(state.appliedValue - 0.3f) < 0.000001f);
  CHECK_EQ(int(state.appliedValue / 0.1f), 3);
  float previous = state.appliedValue;
  CHECK(state.setTarget(0.410f));
  CHECK_EQ(int((state.appliedValue - previous) / 0.1f), 1);
}

TEST(param_write_encoder_rejects_invalid_step_sizes_without_changing_values) {
  for (float invalid : {
    0.f, -0.125f,
    std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::infinity()
  }) {
    pw::EncoderState state(0.f, invalid);
    CHECK(!state.setTarget(0.250f));
    CHECK_EQ(state.logicalValue, 0.f);
    CHECK_EQ(state.appliedValue, 0.f);
  }
}

TEST(param_write_encoder_step_count_limit_uses_its_configured_step_size) {
  CHECK(pw::planDrag(0.f, 134217728.f, 0.125f) == pw::DragAction::Invalid);
  CHECK(pw::planDrag(0.f, 134217728.f, 0.25f) == pw::DragAction::SetValue);
}
