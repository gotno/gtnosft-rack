#include "osc/RttEstimator.hpp"
#include "test.hpp"

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

TEST(rtt_estimator_uses_max_rto_before_the_first_sample) {
  RttEstimator estimator;
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{200000000});
  CHECK_EQ(estimator.srtt().count(), int64_t{0});
}

TEST(rtt_estimator_first_sample_sets_srtt_and_half_variation) {
  RttEstimator estimator;
  estimator.addSample(20ms);
  CHECK_EQ(estimator.srtt().count(), int64_t{20000000});
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{60000000});
}

TEST(rtt_estimator_granularity_floors_the_variation_term) {
  RttEstimator estimator;
  estimator.addSample(20ms);
  CHECK_EQ(estimator.rto(50ms).count(), int64_t{70000000});
}

TEST(rtt_estimator_smooths_later_samples) {
  RttEstimator estimator;
  estimator.addSample(20ms);
  estimator.addSample(40ms);
  CHECK_EQ(estimator.srtt().count(), int64_t{22500000});
  CHECK_EQ(estimator.rto(1ms).count(), int64_t{72500000});
}

TEST(rtt_estimator_clamps_to_min_and_max) {
  RttEstimator low;
  low.addSample(1ms);
  CHECK_EQ(low.rto(1ms).count(), int64_t{10000000});

  RttEstimator high;
  high.addSample(150ms);
  CHECK_EQ(high.rto(1ms).count(), int64_t{200000000});
}

TEST(rtt_estimator_backoff_doubles_until_a_new_sample) {
  RttEstimator estimator;
  estimator.addSample(20ms);
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{60000000});
  estimator.backoff();
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{120000000});
  estimator.backoff();
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{200000000});
  estimator.addSample(20ms);
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{50000000});
}

TEST(rtt_estimator_backoff_is_capped) {
  RttEstimator estimator;
  estimator.addSample(1ms);
  for (int i = 0; i < 5; ++i) estimator.backoff();
  CHECK_EQ(estimator.rto(1ms).count(), int64_t{96000000});
  estimator.backoff();
  CHECK_EQ(estimator.rto(1ms).count(), int64_t{96000000});
}

TEST(rtt_estimator_reset_forgets_samples) {
  RttEstimator estimator;
  estimator.addSample(20ms);
  estimator.backoff();
  estimator.reset();
  CHECK_EQ(estimator.rto(16ms).count(), int64_t{200000000});
  CHECK_EQ(estimator.srtt().count(), int64_t{0});
}
