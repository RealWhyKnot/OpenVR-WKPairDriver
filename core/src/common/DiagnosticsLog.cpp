#define _CRT_SECURE_NO_DEPRECATE
#include "DiagnosticsLog.h"

#include "BuildChannel.h"
#include "DebugLogging.h"
#include "LogPaths.h"

#include <chrono>
#include <cstdio>
#include <mutex>

#include <io.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace openvr_pair::common {
namespace {

std::mutex g_logMutex;
FILE* g_logFile = nullptr;

tm LocalTimeForLog()
{
	auto now = std::chrono::system_clock::now();
	auto nowTime = std::chrono::system_clock::to_time_t(now);
	tm value{};
	localtime_s(&value, &nowTime);
	return value;
}

void FlushDiagnosticsFile();

bool EnsureDiagnosticsLogOpen()
{
	if (!IsDebugLoggingEnabled()) return false;
	if (g_logFile) return true;

	const std::wstring path = TimestampedLogPath(L"diagnostics_log");
	if (!path.empty()) {
		g_logFile = _wfopen(path.c_str(), L"a");
	}
	if (!g_logFile) {
		g_logFile = stderr;
	}
	if (!g_logFile) return false;
	setvbuf(g_logFile, nullptr, _IONBF, 0);

	tm now = LocalTimeForLog();
	fprintf(g_logFile,
		"[%02d:%02d:%02d][pid=%lu][tid=%lu][diagnostics] opened build=%s channel=%s\n",
		now.tm_hour, now.tm_min, now.tm_sec,
		GetCurrentProcessId(), GetCurrentThreadId(),
		WKOPENVR_BUILD_STAMP, WKOPENVR_BUILD_CHANNEL);
	FlushDiagnosticsFile();
	return true;
}

void FlushDiagnosticsFile()
{
	if (!g_logFile) return;
	fflush(g_logFile);
	if (g_logFile == stderr) return;
	const int fd = _fileno(g_logFile);
	if (fd < 0) return;
	const intptr_t osHandle = _get_osfhandle(fd);
	if (osHandle == -1) return;
	FlushFileBuffers(reinterpret_cast<HANDLE>(osHandle));
}

} // namespace

void DiagnosticLogV(const char* component, const char* fmt, va_list args)
{
	if (!IsDebugLoggingEnabled()) return;

	std::lock_guard<std::mutex> lock(g_logMutex);
	if (!EnsureDiagnosticsLogOpen()) return;

	tm now = LocalTimeForLog();
	fprintf(g_logFile, "[%02d:%02d:%02d][pid=%lu][tid=%lu][%s] ",
		now.tm_hour, now.tm_min, now.tm_sec,
		GetCurrentProcessId(), GetCurrentThreadId(),
		component ? component : "general");
	vfprintf(g_logFile, fmt ? fmt : "", args);
	fputc('\n', g_logFile);
	FlushDiagnosticsFile();
}

void DiagnosticLog(const char* component, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	DiagnosticLogV(component, fmt, args);
	va_end(args);
}

} // namespace openvr_pair::common
