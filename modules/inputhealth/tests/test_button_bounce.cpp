#include <gtest/gtest.h>

#include "InputHealthObservation.h"

TEST(InputHealthButtonBounce, CountsRapidAlternatingTransitions)
{
	inputhealth::ComponentStats stats;

	inputhealth::ObserveBooleanSample(stats, true, 1000000ULL);
	EXPECT_EQ(stats.press_count, 1u);
	EXPECT_EQ(stats.bounce_transition_count, 0u);

	inputhealth::ObserveBooleanSample(stats, false, 1003000ULL);
	EXPECT_EQ(stats.press_count, 1u);
	EXPECT_EQ(stats.bounce_transition_count, 1u);
	EXPECT_EQ(stats.bounce_max_interval_us, 3000u);

	inputhealth::ObserveBooleanSample(stats, true, 1006000ULL);
	EXPECT_EQ(stats.press_count, 2u);
	EXPECT_EQ(stats.bounce_transition_count, 2u);
	EXPECT_EQ(stats.bounce_max_interval_us, 3000u);
}

TEST(InputHealthButtonBounce, IgnoresOrdinaryPressSpacing)
{
	inputhealth::ComponentStats stats;

	inputhealth::ObserveBooleanSample(stats, true, 1000000ULL);
	inputhealth::ObserveBooleanSample(stats, false, 1100000ULL);
	inputhealth::ObserveBooleanSample(stats, true, 1200000ULL);

	EXPECT_EQ(stats.press_count, 2u);
	EXPECT_EQ(stats.bounce_transition_count, 0u);
	EXPECT_EQ(stats.bounce_max_interval_us, 0u);
}

TEST(InputHealthButtonBounce, ResetPassiveClearsBounceState)
{
	inputhealth::ComponentStats stats;

	inputhealth::ObserveBooleanSample(stats, true, 1000000ULL);
	inputhealth::ObserveBooleanSample(stats, false, 1003000ULL);
	ASSERT_EQ(stats.bounce_transition_count, 1u);

	inputhealth::ComponentStatsResetPassive(stats);
	EXPECT_EQ(stats.press_count, 0u);
	EXPECT_EQ(stats.bounce_transition_count, 0u);
	EXPECT_EQ(stats.bounce_max_interval_us, 0u);
	EXPECT_EQ(stats.last_raw_transition_us, 0u);
}
