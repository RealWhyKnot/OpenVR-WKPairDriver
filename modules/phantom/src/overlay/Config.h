#pragma once

#include "BlendCurves.h"

#include <cstdint>
#include <string>
#include <unordered_map>

// Persisted Phantom overlay state. Saved to
// %LocalAppDataLow%\WKOpenVR\profiles\phantom.txt as plain key=value lines,
// matching the smoothing.txt / inputhealth.txt convention so a user can
// inspect or hand-edit if needed.
struct PhantomConfig
{
    // Master switch. When false the driver hot-path fast-paths to passthrough
    // for every device (pose history still records so a later flip-on has
    // back-history available, but no synthesis is applied).
    bool master_enabled = false;

    // Per-tracker dropout opt-in, keyed by serial. A device absent from the
    // map (or mapped to false) does not get dropout bridging even when
    // master_enabled is true. Allows the user to opt in only the body
    // trackers and leave HMD / controllers on passthrough.
    std::unordered_map<std::string, bool> dropout_enabled;

    // Tunable timeout-ladder values, all in milliseconds. Defaults track
    // BlendCurves.h. A future overlay may surface fewer than all of these as
    // sliders; the config persists whatever the overlay sets and the driver
    // treats 0 as "use the compiled-in default" for graceful skew handling.
    uint32_t blend_out_ms   = phantom::DefaultTimings::kBlendOutMs;
    uint32_t blend_in_ms    = phantom::DefaultTimings::kBlendInMs;
    uint32_t reckon_hold_ms = phantom::DefaultTimings::kReckonHoldMs;
    uint32_t synth_hold_ms  = phantom::DefaultTimings::kSynthHoldMs;
    uint32_t lost_hold_ms   = phantom::DefaultTimings::kLostHoldMs;
};

// Load from disk. On any read / parse error the on-disk file is ignored and
// a default-constructed PhantomConfig is returned.
PhantomConfig LoadPhantomConfig();

// Save to disk. Best-effort: failures are silently swallowed and the next
// save retries. Driver values land via IPC regardless of persistence success.
void SavePhantomConfig(const PhantomConfig& cfg);
