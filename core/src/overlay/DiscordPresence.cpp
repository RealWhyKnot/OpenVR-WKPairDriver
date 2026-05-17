#include "DiscordPresence.h"

#include "ShellContext.h"

// pair_discord_rpc links statically; do not define DISCORD_DYNAMIC_LIB so the
// header decls fall through to plain external linkage rather than dllimport.
#include <discord_rpc.h>

#include "LogPaths.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace WKOpenVR {

namespace {

constexpr const char* kAppId = "1504757904253779988";
constexpr const char* kLargeImageKey  = "logo";
constexpr const char* kLargeImageText = "WKOpenVR";

// Minimum wall-clock interval between Discord_UpdatePresence calls.
// The SDK rate-limits on its own, but we avoid hammering it on every frame.
constexpr double kUpdateIntervalSec = 2.0;
constexpr double kTickIntervalSec   = 2.0;

bool g_initialized = false;
std::atomic<bool> g_connected{false};

int64_t     g_startTimestamp = 0;
std::string g_state;
std::string g_details;

using Clock = std::chrono::steady_clock;
Clock::time_point g_lastTick;
Clock::time_point g_lastUpdate;

// Toggle + profile.
bool         g_enabled = true;
std::wstring g_profileRoot;

// Diagnostic log. Lives at %LocalAppDataLow%\WKOpenVR\Logs\discord_log.<ts>.txt
// alongside the per-feature logs. Opened lazily on the first event so an
// instance that never enables presence does not leave an empty log file.
FILE*      g_log = nullptr;
std::mutex g_logMutex;

void LogV(const char* fmt, va_list args)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_log) {
        std::wstring path = openvr_pair::common::TimestampedLogPath(L"discord_log");
        if (!path.empty()) {
            _wfopen_s(&g_log, path.c_str(), L"a");
        }
        if (!g_log) {
            g_log = stderr;
        }
    }
    auto       now     = std::chrono::system_clock::now();
    std::time_t nowT   = std::chrono::system_clock::to_time_t(now);
    std::tm     local{};
    localtime_s(&local, &nowT);
    fprintf(g_log, "[%02d:%02d:%02d] ",
        local.tm_hour, local.tm_min, local.tm_sec);
    vfprintf(g_log, fmt, args);
    fputc('\n', g_log);
    fflush(g_log);
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

std::wstring ProfileFilePath()
{
    if (g_profileRoot.empty()) return {};
    return g_profileRoot + L"\\discord.txt";
}

// Reads `enabled=on|off` from discord.txt. Missing file or unparseable
// content leaves the default (`true`) intact so first launch behaves the same
// as it has historically.
bool ReadEnabledPref(bool defaultValue)
{
    const std::wstring path = ProfileFilePath();
    if (path.empty()) return defaultValue;
    std::ifstream f(path);
    if (!f.is_open()) return defaultValue;
    std::string line;
    while (std::getline(f, line)) {
        // Trim \r and surrounding whitespace.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        line.erase(0, lead);
        if (line.rfind("enabled=", 0) == 0) {
            std::string value = line.substr(8);
            if (value == "on" || value == "1" || value == "true")  return true;
            if (value == "off" || value == "0" || value == "false") return false;
        }
    }
    return defaultValue;
}

void WriteEnabledPref(bool value)
{
    const std::wstring path = ProfileFilePath();
    if (path.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        Log("[prefs] write failed: cannot open discord.txt");
        return;
    }
    fprintf(f, "enabled=%s\n", value ? "on" : "off");
    fclose(f);
}

void PushPresence()
{
    if (!g_initialized) return;

    DiscordRichPresence rp{};
    rp.state          = g_state.empty()   ? nullptr : g_state.c_str();
    rp.details        = g_details.empty() ? nullptr : g_details.c_str();
    rp.startTimestamp = g_startTimestamp;
    rp.largeImageKey  = kLargeImageKey;
    rp.largeImageText = kLargeImageText;

    Discord_UpdatePresence(&rp);
    g_lastUpdate = Clock::now();

    Log("[push] state='%s' details='%s'",
        g_state.c_str(), g_details.c_str());
}

void OnReady(const DiscordUser* user)
{
    g_connected = true;
    if (user && user->username && user->username[0] != '\0') {
        Log("[ready] connected as %s", user->username);
    } else {
        Log("[ready] connected");
    }
}

void OnDisconnected(int code, const char* message)
{
    g_connected = false;
    Log("[disconnected] code=%d message=%s", code, message ? message : "");
}

void OnErrored(int code, const char* message)
{
    g_connected = false;
    Log("[error] code=%d message=%s", code, message ? message : "");
}

void StartSdk()
{
    if (g_initialized) return;

    DiscordEventHandlers handlers{};
    handlers.ready        = OnReady;
    handlers.disconnected = OnDisconnected;
    handlers.errored      = OnErrored;

    Log("[init] starting; app_id=%s", kAppId);
    Discord_Initialize(kAppId, &handlers, /*autoRegister=*/0, /*steamId=*/nullptr);

    // Discord_Initialize does not surface a synchronous error return; the
    // errored/disconnected callbacks fire asynchronously if the client is
    // unavailable. We mark ourselves initialized and let the first callback
    // (or its absence) indicate the actual connection state.
    g_initialized = true;

    const auto now = std::chrono::system_clock::now();
    g_startTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    g_state   = "Idle";
    g_details = "WKOpenVR";

    g_lastTick   = Clock::now();
    g_lastUpdate = Clock::now();

    PushPresence();
}

void StopSdk()
{
    if (!g_initialized) return;
    Log("[shutdown] clearing presence and releasing Discord IPC");
    Discord_ClearPresence();
    Discord_Shutdown();
    g_initialized = false;
    g_connected   = false;
    g_state.clear();
    g_details.clear();
}

} // namespace

void DiscordPresence_Init(const openvr_pair::overlay::ShellContext &context)
{
    g_profileRoot = context.profileRoot;
    g_enabled = ReadEnabledPref(true);
    Log("[init] enabled=%s profile_root=%ls",
        g_enabled ? "on" : "off",
        g_profileRoot.empty() ? L"(none)" : g_profileRoot.c_str());

    if (!g_enabled) {
        Log("[init] skipped: disabled by discord.txt");
        return;
    }
    StartSdk();
}

void DiscordPresence_Shutdown()
{
    StopSdk();
}

void DiscordPresence_Tick()
{
    if (!g_initialized) return;

    const auto now = Clock::now();
    const double secSinceTick = std::chrono::duration<double>(now - g_lastTick).count();
    if (secSinceTick < kTickIntervalSec) return;

    g_lastTick = now;
    Discord_RunCallbacks();
}

void DiscordPresence_SetState(const char* state, const char* details)
{
    if (!g_initialized) return;

    const std::string newState   = state   ? state   : "";
    const std::string newDetails = details ? details : "";

    if (newState == g_state && newDetails == g_details) return;

    g_state   = newState;
    g_details = newDetails;

    const auto now = Clock::now();
    const double secSinceUpdate = std::chrono::duration<double>(now - g_lastUpdate).count();
    if (secSinceUpdate >= kUpdateIntervalSec) {
        PushPresence();
    }
}

bool DiscordPresence_IsEnabled()
{
    return g_enabled;
}

void DiscordPresence_SetEnabled(bool enabled)
{
    if (enabled == g_enabled) return;
    Log("[toggle] enabled=%s -> %s",
        g_enabled ? "on" : "off",
        enabled   ? "on" : "off");
    g_enabled = enabled;
    WriteEnabledPref(g_enabled);
    if (g_enabled) {
        StartSdk();
    } else {
        StopSdk();
    }
}

bool DiscordPresence_IsConnected()
{
    return g_connected.load();
}

void DiscordPresence_LogInfo(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}

} // namespace WKOpenVR
