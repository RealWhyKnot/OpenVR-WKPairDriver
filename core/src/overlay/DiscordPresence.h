#pragma once

namespace openvr_pair::overlay {
struct ShellContext;
}

namespace WKOpenVR {

// Initialize Discord Rich Presence. Safe to call when Discord is not running
// or not installed -- logs once on failure, then no-ops for the session.
// Reads the persisted enabled flag from <profileRoot>\discord.txt; when the
// flag is off the SDK is never touched and Tick/SetState/Shutdown all no-op.
void DiscordPresence_Init(const openvr_pair::overlay::ShellContext &context);

// Shut down and release the Discord connection. Call once at overlay exit.
void DiscordPresence_Shutdown();

// Pump Discord SDK callbacks. Call once per frame (or gated to ~2s intervals).
void DiscordPresence_Tick();

// Update the presence state/details strings directly.
// Called by PresenceComposer; not normally called from plugin code.
void DiscordPresence_SetState(const char* state, const char* details);

// Live toggle. Persists the new value to discord.txt and connects/disconnects
// the SDK so the change takes effect without restarting the overlay. Coexists
// with VRCX or other rich-presence tools by relinquishing the Discord IPC
// pipe when turned off.
bool DiscordPresence_IsEnabled();
void DiscordPresence_SetEnabled(bool enabled);

// True once the SDK's ready callback has fired. Useful for surfacing connection
// state in the UI so the user can see whether they are actually talking to
// Discord rather than just toggling a flag.
bool DiscordPresence_IsConnected();

// Write a diagnostic line to discord_log.<ts>.txt. Used by the composer and
// any other in-tree caller that wants its events recorded alongside the SDK
// push events. printf-style formatting; the function appends a newline.
void DiscordPresence_LogInfo(const char *fmt, ...);

} // namespace WKOpenVR
