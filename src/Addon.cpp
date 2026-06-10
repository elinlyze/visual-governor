#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "Nexus.h"
#include "imgui.h"

namespace
{
constexpr uint32_t AddonSignature = 0x5653474Fu; // VSGO
constexpr const char* AddonName = "Visual Governor";
constexpr const char* AddonChannel = "VisualGovernor";
constexpr const char* WindowName = "Visual Governor Debug";
constexpr const char* TierOverlayWindowName = "Visual Governor Tier";
constexpr const char* KeybindToggle = "KB_VISUAL_GOVERNOR_TIER_OVERLAY";

AddonAPI_t* g_api = nullptr;
HMODULE g_module = nullptr;
std::wstring g_addonDir;
std::wstring g_configPath;
std::wstring g_tracePath;
std::wstring g_autoLogPath;
std::wstring g_xrefPath;
std::wstring g_probePath;
std::mutex g_logMutex;

void AutoStateLog(const std::string& line);
void AutoChangeLogLine(const std::string& line);
void TraceLine(const std::string& line);
void ResetAutoDecisionTimers(bool keepBackoff);
const char* ModelValueName(int v);

void Log(ELogLevel level, const char* msg)
{
    if (g_api && g_api->Log) { g_api->Log(level, AddonChannel, msg); }
}

std::wstring Utf8ToWide(const char* s)
{
    if (!s || !*s) { return L""; }
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (needed <= 0) { return L""; }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), needed);
    return out;
}

std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) { return {}; }
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) { return {}; }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().wstring();
}

std::wstring AddSlash(std::wstring p)
{
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') { p.push_back(L'\\'); }
    return p;
}

std::string Hex(uint64_t v)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << v;
    return ss.str();
}

std::string NowString()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

std::string SanitizeForLog(std::string text)
{
    for (char& ch : text)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c < 32 && ch != '\n' && ch != '\r' && ch != '\t') { ch = ' '; }
    }
    return text;
}

constexpr int kStoredTierSlots = 5; // one legacy slot is still read for old configs, but no longer shown/applied


// Visual Governor is split into public governor/UI code plus a private backend.
// In the public repo, src/private is an ignored local junction to the private repo.
#include "public/ConfigModel.inc"
#include "private/Gw2NativeBridge.inc"
#include "private/Gw2ApplyEngine.inc"
#include "public/GovernorRuntime.inc"
#include "public/UiAndLifecycle.inc"

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonDefinition_t def{
        AddonSignature,
        NEXUS_API_VERSION,
        AddonName,
        { 0, 71, 1, 0 },
        "local build",
        "Automatic visual settings governor with tiered graphics profiles, startup-safe renderer gating, memtools-backed signature resolving, and optional mini tier overlay.",
        AddonLoad,
        AddonUnload,
        AF_None,
        UP_None,
        nullptr
    };
    return &def;
}
