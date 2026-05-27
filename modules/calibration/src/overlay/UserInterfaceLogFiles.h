#pragma once

#include "MotionRecording.h"

#include <string>
#include <vector>

namespace spacecal::ui_logs {

struct LogsPanelState {
	std::vector<spacecal::replay::LogFileEntry> files;
	bool listBuilt = false;
	int selectedIdx = -1;
	std::string copyHint;
	double copyHintExpireTime = 0.0;
};

LogsPanelState& LogsState();
void RebuildLogsList(LogsPanelState& state);
std::wstring ResolveLogsDirectory(const LogsPanelState& state);
void DrawLogFileList(LogsPanelState& state);
void DrawSelectedLogActions(LogsPanelState& state);

} // namespace spacecal::ui_logs
