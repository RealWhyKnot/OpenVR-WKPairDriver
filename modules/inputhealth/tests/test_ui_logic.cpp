#include <gtest/gtest.h>

#include "InputHealthUiLogic.h"

TEST(InputHealthUiLogic, HidesDriverProblemBannerInDesktopWindow)
{
	EXPECT_FALSE(inputhealth::ui::ShouldShowDriverProblemBanner(false, true));
}

TEST(InputHealthUiLogic, ShowsDriverProblemBannerInDashboardWhenErrorExists)
{
	EXPECT_TRUE(inputhealth::ui::ShouldShowDriverProblemBanner(true, true));
}

TEST(InputHealthUiLogic, HidesDriverProblemBannerWithoutError)
{
	EXPECT_FALSE(inputhealth::ui::ShouldShowDriverProblemBanner(true, false));
	EXPECT_FALSE(inputhealth::ui::ShouldShowDriverProblemBanner(false, false));
}
