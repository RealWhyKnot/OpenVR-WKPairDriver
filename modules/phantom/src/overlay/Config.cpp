#include "Config.h"

#include "Win32Paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

std::wstring ConfigDir()
{
    return openvr_pair::common::WkOpenVrSubdirectoryPath(L"profiles", true);
}

std::wstring ConfigPath()
{
    const std::wstring dir = ConfigDir();
    if (dir.empty()) return {};
    return dir + L"\\phantom.txt";
}

uint32_t ParseMsClamped(const char* val, uint32_t lo, uint32_t hi)
{
    const long n = std::strtol(val, nullptr, 10);
    if (n < (long)lo) return lo;
    if (n > (long)hi) return hi;
    return static_cast<uint32_t>(n);
}

} // namespace

PhantomConfig LoadPhantomConfig()
{
    PhantomConfig cfg;
    const std::wstring path = ConfigPath();
    if (path.empty()) return cfg;

    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return cfg;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = std::strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (std::strcmp(key, "master_enabled") == 0) {
            cfg.master_enabled = (std::atoi(val) != 0);
        } else if (std::strcmp(key, "blend_out_ms") == 0) {
            cfg.blend_out_ms = ParseMsClamped(val, 0, 1000);
        } else if (std::strcmp(key, "blend_in_ms") == 0) {
            cfg.blend_in_ms = ParseMsClamped(val, 0, 2000);
        } else if (std::strcmp(key, "reckon_hold_ms") == 0) {
            cfg.reckon_hold_ms = ParseMsClamped(val, 0, 1000);
        } else if (std::strcmp(key, "synth_hold_ms") == 0) {
            cfg.synth_hold_ms = ParseMsClamped(val, 0, 10000);
        } else if (std::strcmp(key, "lost_hold_ms") == 0) {
            cfg.lost_hold_ms = ParseMsClamped(val, 0, 60000);
        } else if (std::strncmp(key, "dropout_enabled.", 16) == 0) {
            const std::string serial(key + 16);
            cfg.dropout_enabled[serial] = (std::atoi(val) != 0);
        }
    }
    std::fclose(f);
    return cfg;
}

void SavePhantomConfig(const PhantomConfig& cfg)
{
    const std::wstring path = ConfigPath();
    if (path.empty()) return;

    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return;
    std::fprintf(f, "master_enabled=%d\n",  cfg.master_enabled ? 1 : 0);
    std::fprintf(f, "blend_out_ms=%u\n",    (unsigned)cfg.blend_out_ms);
    std::fprintf(f, "blend_in_ms=%u\n",     (unsigned)cfg.blend_in_ms);
    std::fprintf(f, "reckon_hold_ms=%u\n",  (unsigned)cfg.reckon_hold_ms);
    std::fprintf(f, "synth_hold_ms=%u\n",   (unsigned)cfg.synth_hold_ms);
    std::fprintf(f, "lost_hold_ms=%u\n",    (unsigned)cfg.lost_hold_ms);
    for (const auto& kv : cfg.dropout_enabled) {
        if (kv.second) {
            std::fprintf(f, "dropout_enabled.%s=1\n", kv.first.c_str());
        }
    }
    std::fclose(f);
}
