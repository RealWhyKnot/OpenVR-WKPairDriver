#pragma once

namespace inputhealth::ui {

inline bool ShouldShowDriverProblemBanner(bool dashboardVisible, bool hasDriverError)
{
	return dashboardVisible && hasDriverError;
}

} // namespace inputhealth::ui
