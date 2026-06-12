// Public Visual Governor implementation section. Contains UI, config, and governor logic.
// Included by Addon.cpp to keep this refactor source-compatible.

struct SectionView { uint8_t* start; size_t size; std::string name; bool executable; };

std::vector<SectionView> GetMainModuleSections()
{
    std::vector<SectionView> out;
    HMODULE base = GetModuleHandleW(nullptr);
    if (!base) { return out; }
    auto* mz = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (mz->e_magic != IMAGE_DOS_SIGNATURE) { return out; }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(base) + mz->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) { return out; }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        char name[9]{};
        std::memcpy(name, sec[i].Name, 8);
        uint32_t chars = sec[i].Characteristics;
        bool exec = (chars & IMAGE_SCN_MEM_EXECUTE) != 0;
        bool read = (chars & IMAGE_SCN_MEM_READ) != 0;
        if (!read) { continue; }
        out.push_back({ reinterpret_cast<uint8_t*>(base) + sec[i].VirtualAddress, static_cast<size_t>(std::max(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData)), name, exec });
    }
    return out;
}

std::string BytesHex(const uint8_t* p, size_t n)
{
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < n; ++i)
    {
        if (i) { ss << ' '; }
        ss << std::setw(2) << static_cast<unsigned>(p[i]);
    }
    return ss.str();
}

float FpsToMs(float fps)
{
    return 1000.0f / std::max(1.0f, fps);
}

float TargetFrameMs()
{
    return FpsToMs(g_cfg.targetFps);
}

float DownMarginFps()
{
    return std::max(0.0f, g_cfg.targetFps * g_cfg.downMarginPercent * 0.01f);
}

float UpMarginFps()
{
    return std::max(0.0f, g_cfg.targetFps * g_cfg.upMarginPercent * 0.01f);
}

float DownshiftFrameMs()
{
    return FpsToMs(std::max(1.0f, g_cfg.targetFps - DownMarginFps()));
}

float StableProbeFrameMs()
{
    return FpsToMs(std::max(1.0f, g_cfg.targetFps - UpMarginFps()));
}

float EffectiveMinimumFps()
{
    // If the user targets 60 FPS but leaves an old 100 FPS emergency minimum,
    // that minimum is mathematically impossible and would pin the gearbox at lowest.
    // Keep it useful by capping emergency to below the target.
    float impossibleCap = std::max(10.0f, g_cfg.targetFps - std::max(5.0f, DownMarginFps()));
    return std::min(g_cfg.minFps, impossibleCap);
}

float EmergencyFrameMs()
{
    return FpsToMs(EffectiveMinimumFps());
}

float EffectiveProbeWarmupSeconds()
{
    const float maxWarmup = std::max(0.0f, g_cfg.upProbeSeconds - 0.25f);
    return std::min(std::clamp(g_cfg.probeWarmupSeconds, 0.0f, 5.0f), maxWarmup);
}

void ResetFrameStats(float frameMs)
{
    frameMs = std::clamp(frameMs, 0.1f, 1000.0f);
    g_state.frameMs = frameMs;
    g_state.fastFrameMs = frameMs;
    g_state.slowFrameMs = frameMs;
    g_state.p95FrameMs = frameMs;
    g_state.fps = 1000.0f / std::max(0.1f, frameMs);
    g_state.emaFps = g_state.fps;
    g_state.frameMsIndex = 0;
    g_state.frameMsCount = 0;
    g_state.frameMsWindow.fill(frameMs);
}

void PushFrameSample(float frameMs)
{
    frameMs = std::clamp(frameMs, 0.1f, 1000.0f);
    g_state.frameMs = frameMs;
    g_state.fps = 1000.0f / std::max(0.1f, frameMs);
    if (g_state.fastFrameMs <= 0.01f || g_state.slowFrameMs <= 0.01f)
    {
        ResetFrameStats(frameMs);
    }
    else
    {
        const float fastA = std::clamp(g_cfg.frameFastAlpha, 0.02f, 0.90f);
        const float slowA = std::clamp(g_cfg.frameSlowAlpha, 0.005f, 0.40f);
        g_state.fastFrameMs = (fastA * frameMs) + ((1.0f - fastA) * g_state.fastFrameMs);
        g_state.slowFrameMs = (slowA * frameMs) + ((1.0f - slowA) * g_state.slowFrameMs);
        g_state.emaFps = 1000.0f / std::max(0.1f, g_state.slowFrameMs);
    }

    g_state.frameMsWindow[static_cast<size_t>(g_state.frameMsIndex)] = frameMs;
    g_state.frameMsIndex = (g_state.frameMsIndex + 1) % static_cast<int>(g_state.frameMsWindow.size());
    g_state.frameMsCount = std::min<int>(g_state.frameMsCount + 1, static_cast<int>(g_state.frameMsWindow.size()));

    if (g_state.frameMsCount > 0)
    {
        std::array<float, 240> samples = g_state.frameMsWindow;
        auto begin = samples.begin();
        auto end = begin + g_state.frameMsCount;
        std::sort(begin, end);
        size_t idx = static_cast<size_t>(std::clamp<int>(g_state.frameMsCount * 95 / 100, 0, g_state.frameMsCount - 1));
        g_state.p95FrameMs = samples[idx];
    }
}

float FailedProbeBackoffSeconds()
{
    float backoff = std::max(0.0f, g_cfg.upProbeBackoffSeconds);
    for (int i = 1; i < g_state.failedProbeCount; ++i)
    {
        backoff *= 2.0f;
        if (backoff >= g_cfg.maxProbeBackoffSeconds) { break; }
    }
    return std::min(backoff, g_cfg.maxProbeBackoffSeconds);
}

const char* GovernorPresetName(int preset)
{
    switch (preset)
    {
    case 0: return "Swift";
    case 2: return "Calm";
    case 1:
    default: return "Balanced";
    }
}

void ApplyGovernorPreset(int preset)
{
    preset = std::clamp(preset, 0, 2);
    g_cfg.governorPreset = preset;
    g_cfg.nexusLoadingGuardEnabled = true;
    g_cfg.stagedApplyDelayMs = 12;
    g_cfg.liveWritesEnabled = true;
    g_cfg.stagedApplyEnabled = true;
    g_cfg.controlLimitLive = true;
    g_cfg.controlQualityLive = true;
    g_cfg.minTier = std::clamp(g_cfg.minTier, 0, kMaxGovernorTier);
    g_cfg.maxTier = std::clamp(g_cfg.maxTier, 0, kMaxGovernorTier);

    if (preset == 0) // Swift: quicker reaction and faster probing, based around the tested tier map.
    {
        g_cfg.downMarginPercent = 7.5f;
        g_cfg.upMarginPercent = 18.5f;
        g_cfg.downSeconds = 0.95f;
        g_cfg.downRepeatSeconds = 0.55f;
        g_cfg.cooldownSeconds = 0.45f;
        g_cfg.upSeconds = 7.0f;
        g_cfg.upProbeSeconds = 3.5f;
        g_cfg.upProbeBackoffSeconds = 24.0f;
        g_cfg.maxProbeBackoffSeconds = 120.0f;
        g_cfg.probeWarmupSeconds = 1.1f;
        g_cfg.probeRollbackSeconds = 1.10f;
        g_cfg.postLoadingGraceSeconds = 2.0f;
        g_cfg.frameFastAlpha = 0.30f;
        g_cfg.frameSlowAlpha = 0.060f;
        g_cfg.tierLimitValues = { 4, 3, 2, 1, 0 };
        g_cfg.tierQualityValues = { 4, 3, 2, 1, 0 };
    }
    else if (preset == 2) // Calm: less churn, slower probing, keeps the tested reduction shape.
    {
        g_cfg.downMarginPercent = 9.8f;
        g_cfg.upMarginPercent = 23.5f;
        g_cfg.downSeconds = 2.0f;
        g_cfg.downRepeatSeconds = 1.05f;
        g_cfg.cooldownSeconds = 0.9f;
        g_cfg.upSeconds = 22.0f;
        g_cfg.upProbeSeconds = 6.5f;
        g_cfg.upProbeBackoffSeconds = 55.0f;
        g_cfg.maxProbeBackoffSeconds = 240.0f;
        g_cfg.probeWarmupSeconds = 1.8f;
        g_cfg.probeRollbackSeconds = 2.00f;
        g_cfg.postLoadingGraceSeconds = 4.0f;
        g_cfg.frameFastAlpha = 0.22f;
        g_cfg.frameSlowAlpha = 0.040f;
        g_cfg.tierLimitValues = { 4, 3, 2, 1, 0 };
        g_cfg.tierQualityValues = { 4, 3, 2, 1, 0 };
    }
    else // Balanced: latest meta-event tuned config converted to percentages.
    {
        g_cfg.downMarginPercent = 8.4f;
        g_cfg.upMarginPercent = 20.6f;
        g_cfg.downSeconds = 1.3f;
        g_cfg.downRepeatSeconds = 0.75f;
        g_cfg.cooldownSeconds = 0.70f;
        g_cfg.upSeconds = 10.0f;
        g_cfg.upProbeSeconds = 4.5f;
        g_cfg.upProbeBackoffSeconds = 35.0f;
        g_cfg.maxProbeBackoffSeconds = 160.0f;
        g_cfg.probeWarmupSeconds = 1.5f;
        g_cfg.probeRollbackSeconds = 1.35f;
        g_cfg.postLoadingGraceSeconds = 2.5f;
        g_cfg.frameFastAlpha = 0.26f;
        g_cfg.frameSlowAlpha = 0.055f;
        g_cfg.tierLimitValues = { 4, 3, 2, 1, 0 };
        g_cfg.tierQualityValues = { 4, 3, 2, 1, 0 };
    }

    ResetAutoDecisionTimers(false);
    ResetFrameStats(g_state.frameMs > 0.1f ? g_state.frameMs : TargetFrameMs());
    std::ostringstream ss;
    ss << "preset applied name=" << GovernorPresetName(preset)
       << " downPct=" << g_cfg.downMarginPercent
       << " upPct=" << g_cfg.upMarginPercent
       << " down=" << g_cfg.downSeconds
       << " repeat=" << g_cfg.downRepeatSeconds
       << " stable=" << g_cfg.upSeconds
       << " probe=" << g_cfg.upProbeSeconds;
    AutoStateLog(ss.str());
}

NexusLinkData_t* CurrentNexusLink()
{
    if (!g_api || !g_api->DataLink_Get) { return nullptr; }
    return static_cast<NexusLinkData_t*>(g_api->DataLink_Get(DL_NEXUS_LINK));
}

bool UpdateNexusLoadingGuard(float dt)
{
    if (!g_cfg.nexusLoadingGuardEnabled) { return false; }

    NexusLinkData_t* link = CurrentNexusLink();
    if (!link) { return false; }

    const bool gameplay = link->IsGameplay;
    if (!g_state.nexusTimingInitialized)
    {
        g_state.nexusTimingInitialized = true;
        g_state.nexusWasGameplay = gameplay;
        g_state.nexusInLoading = false;
        g_state.nexusLoadingSeconds = 0.0f;
        return false;
    }

    if (g_state.nexusWasGameplay && !gameplay)
    {
        g_state.nexusInLoading = true;
        g_state.nexusLoadingSeconds = 0.0f;
        VisualBackendNotifyLoadingEnter();
        if (!g_state.inLoadingGuard)
        {
            std::ostringstream ss;
            ss << "freeze reason=nexus-loading-enter fps=" << std::fixed << std::setprecision(1) << g_state.fps
               << " ema=" << g_state.emaFps
               << " target=" << g_cfg.targetFps;
            AutoStateLog(ss.str());
        }
    }
    else if (!g_state.nexusWasGameplay && gameplay && g_state.nexusInLoading)
    {
        g_state.nexusInLoading = false;
        g_state.inLoadingGuard = false;
        g_state.postLoadingGrace = g_cfg.postLoadingGraceSeconds;
        ResetFrameStats(g_state.frameMs > 0.1f ? g_state.frameMs : TargetFrameMs()); // do not carry loading/menu FPS into gameplay decisions.
        std::ostringstream ss;
        ss << "unfreeze reason=nexus-loading-exit grace=" << g_state.postLoadingGrace
           << " fps=" << std::fixed << std::setprecision(1) << g_state.fps
           << " loadSeconds=" << g_state.nexusLoadingSeconds;
        AutoStateLog(ss.str());
        g_state.autoState = "post-loading-grace";
        g_state.nexusWasGameplay = gameplay;
        return false;
    }

    g_state.nexusWasGameplay = gameplay;

    if (g_state.nexusInLoading || !gameplay)
    {
        g_state.nexusLoadingSeconds += dt;
        g_state.inLoadingGuard = true;
        g_state.autoState = "nexus-loading";
        g_state.lowTimer = g_state.highTimer = g_state.emergencyTimer = 0.0f;
        g_state.inUpshiftProbe = false;
        g_state.probeRollbackTier = -1;
        return true;
    }

    return false;
}

void NormalizeGovernorTierRange()
{
    g_cfg.minTier = std::clamp(g_cfg.minTier, 0, kMaxGovernorTier);
    g_cfg.maxTier = std::clamp(g_cfg.maxTier, 0, kMaxGovernorTier);
    if (g_cfg.minTier > g_cfg.maxTier) { std::swap(g_cfg.minTier, g_cfg.maxTier); }
}

int ClampTierToConfiguredRange(int tier)
{
    NormalizeGovernorTierRange();
    return std::clamp(std::clamp(tier, 0, kMaxGovernorTier), g_cfg.minTier, g_cfg.maxTier);
}

bool AutomaticTierMovementLocked()
{
    NormalizeGovernorTierRange();
    return g_cfg.minTier == g_cfg.maxTier;
}

void AutoStateLog(const std::string& line)
{
    g_state.status = line;
    TraceLine(line);
    AutoChangeLogLine(line);
}

void ResetAutoDecisionTimers(bool keepBackoff = true)
{
    g_state.lowTimer = 0.0f;
    g_state.highTimer = 0.0f;
    g_state.emergencyTimer = 0.0f;
    g_state.cooldown = 0.0f;
    g_state.probeTimer = 0.0f;
    g_state.probeBadTimer = 0.0f;
    g_state.inUpshiftProbe = false;
    g_state.probeRollbackTier = -1;
    g_state.hadDownshiftSeries = false;
    if (!keepBackoff) { g_state.probeBackoff = 0.0f; g_state.failedProbeCount = 0; }
}

void ApplyTier(int tier, const char* reason)
{
    const int requestedTier = std::clamp(tier, 0, kMaxGovernorTier);
    tier = ClampTierToConfiguredRange(requestedTier);
    if (tier == g_state.currentTier)
    {
        if (requestedTier != tier)
        {
            std::ostringstream ss;
            ss << "Governor tier request clamped/ignored requested=" << requestedTier
               << " current=" << g_state.currentTier
               << " range=" << g_cfg.minTier << ".." << g_cfg.maxTier
               << " reason=" << (reason ? reason : "auto");
            AutoStateLog(ss.str());
        }
        return;
    }

    int oldTier = g_state.currentTier;
    int oldLimit = GovernorTierToLimitValue(oldTier);
    int oldQuality = GovernorTierToQualityValue(oldTier);
    int limitValue = GovernorTierToLimitValue(tier);
    int qualityValue = GovernorTierToQualityValue(tier);
    g_state.currentTier = tier;

    std::ostringstream ss;
    ss << "Governor tier " << oldTier << " -> " << tier
       << " reason=" << (reason ? reason : "auto")
       << " fps=" << std::fixed << std::setprecision(1) << g_state.fps
       << " ema=" << g_state.emaFps
       << " target=" << g_cfg.targetFps
       << " values limit " << oldLimit << ":" << ModelValueName(oldLimit) << " -> " << limitValue << ":" << ModelValueName(limitValue)
       << " quality " << oldQuality << ":" << ModelValueName(oldQuality) << " -> " << qualityValue << ":" << ModelValueName(qualityValue);
    g_state.status = ss.str();
    TraceLine(g_state.status);
    AutoChangeLogLine(g_state.status);
    ApplyLiveModelValues(limitValue, qualityValue, reason ? reason : "governor-tier");
    ApplyExtraGearTier(tier, reason ? reason : "governor-tier");
}

void ForceApplyCurrentTier(const char* reason, bool forceGraphicsRows = true)
{
    int tier = ClampTierToConfiguredRange(g_state.currentTier);
    g_state.currentTier = tier;
    int limitValue = GovernorTierToLimitValue(tier);
    int qualityValue = GovernorTierToQualityValue(tier);

    std::ostringstream ss;
    ss << "Force current tier apply tier=" << tier
       << " reason=" << (reason ? reason : "force-current-tier")
       << " limit=" << limitValue << ":" << ModelValueName(limitValue)
       << " quality=" << qualityValue << ":" << ModelValueName(qualityValue)
       << " forceGraphicsRows=" << (forceGraphicsRows ? "yes" : "no");
    g_state.status = ss.str();
    TraceLine(ss.str());
    AutoChangeLogLine(ss.str());

    // This intentionally bypasses ApplyTier's same-tier early-return. Reapplying
    // T0 -> T0 is the common manual/testing case and also the path used when the
    // user locks min/max to T0 after GW2 has loaded a Low/Lowest XML profile.
    ApplyLiveModelValues(limitValue, qualityValue, reason ? reason : "force-current-tier");
    QueueExtraGearTier(tier, reason ? reason : "force-current-tier", forceGraphicsRows);
}


bool StartupProfileApplyBlockedByLoading()
{
    if (g_state.stagedActive) { return true; }
    if (g_state.inLoadingGuard || g_state.nexusInLoading || g_state.postLoadingGrace > 0.0f) { return true; }

    // v64: startup profile sync can call renderer-side graphics fallbacks. The v63
    // trace showed a BgfxDraw frameBuffer assertion at map:0 while startup rows were
    // applying before GW2 had a real map/framebuffer. Treat initial non-gameplay as
    // unsafe too; this means the startup sync waits for the first actual gameplay map
    // instead of poking renderer globals on character select / early boot.
    if (g_cfg.nexusLoadingGuardEnabled)
    {
        NexusLinkData_t* link = CurrentNexusLink();
        if (link)
        {
            const bool gameplay = link->IsGameplay;
            if (!gameplay) { return true; }
        }
    }

    return false;
}

void UpdateStartupProfileApply(float dt)
{
    if (!g_cfg.startupProfileApplyEnabled || g_state.startupProfileApplyDone) { return; }
    if (dt <= 0.0f || dt > 5.0f) { return; }

    g_state.startupProfileApplyTimer += dt;
    if (g_state.startupProfileApplyTimer < g_cfg.startupProfileApplyDelaySeconds) { return; }

    if (StartupProfileApplyBlockedByLoading())
    {
        if (!g_state.startupProfileApplyWaiting)
        {
            g_state.startupProfileApplyWaiting = true;
            TraceLine("Startup profile sync armed, waiting for safe non-loading state.");
        }
        return;
    }


    int tier = ClampTierToConfiguredRange(g_state.currentTier);
    int limitValue = GovernorTierToLimitValue(tier);
    int qualityValue = GovernorTierToQualityValue(tier);

    std::ostringstream ss;
    ss << "Startup profile sync applying tier=" << tier
       << " limit=" << limitValue << ":" << ModelValueName(limitValue)
       << " quality=" << qualityValue << ":" << ModelValueName(qualityValue)
       << " delay=" << std::fixed << std::setprecision(1) << g_state.startupProfileApplyTimer << "s";
    g_state.status = ss.str();
    TraceLine(g_state.status);
    AutoChangeLogLine(g_state.status);
    ForceApplyCurrentTier("startup-profile-sync", true);
    g_state.startupProfileApplyDone = true;
    g_state.startupProfileApplyWaiting = false;
}

void UpdateGovernor(float dt)
{
    if (dt <= 0.0f || dt > 5.0f) { return; }

    const float frameMs = std::clamp(dt * 1000.0f, 0.1f, 1000.0f);
    PushFrameSample(frameMs);

    if (g_state.cooldown > 0.0f) { g_state.cooldown = std::max(0.0f, g_state.cooldown - dt); }
    if (g_state.probeBackoff > 0.0f) { g_state.probeBackoff = std::max(0.0f, g_state.probeBackoff - dt); }
    if (g_state.postLoadingGrace > 0.0f) { g_state.postLoadingGrace = std::max(0.0f, g_state.postLoadingGrace - dt); }

    if (!g_cfg.controllerEnabled)
    {
        if (g_state.autoWasEnabled)
        {
            AutoStateLog("auto disabled; decision timers reset");
            ResetAutoDecisionTimers(false);
            g_state.inLoadingGuard = false;
            g_state.autoState = "idle";
        }
        g_state.autoWasEnabled = false;
        return;
    }

    NormalizeGovernorTierRange();
    const int rangeTier = ClampTierToConfiguredRange(g_state.currentTier);
    if (g_state.autoWasEnabled && rangeTier != g_state.currentTier)
    {
        g_state.currentTier = rangeTier;
        ResetAutoDecisionTimers(false);
        ForceApplyCurrentTier("auto-range-safety-clamp", true);
        return;
    }

    if (AutomaticTierMovementLocked() && g_state.autoWasEnabled)
    {
        g_state.currentTier = g_cfg.minTier;
        g_state.lowTimer = 0.0f;
        g_state.highTimer = 0.0f;
        g_state.emergencyTimer = 0.0f;
        g_state.inUpshiftProbe = false;
        g_state.probeRollbackTier = -1;
        g_state.autoState = "tier-locked";
        return;
    }

    if (!g_state.autoWasEnabled)
    {
        g_state.currentTier = ClampTierToConfiguredRange(g_cfg.startTier);
        ResetAutoDecisionTimers(false);
        ResetFrameStats(frameMs);
        g_state.autoState = "enabled";
        std::ostringstream ss;
        ss << "auto enabled mode=frame-time-gearbox preset=" << GovernorPresetName(g_cfg.governorPreset)
           << " startTier=" << g_state.currentTier
           << " targetFps=" << g_cfg.targetFps
           << " targetMs=" << std::fixed << std::setprecision(2) << TargetFrameMs()
           << " downMs=" << DownshiftFrameMs()
           << " stableMs=" << StableProbeFrameMs()
           << " nexusLoadingGuard=" << (g_cfg.nexusLoadingGuardEnabled ? "on" : "off")
           << " loadingDetection=nexus-only";
        AutoStateLog(ss.str());
    }
    g_state.autoWasEnabled = true;

    if (AutomaticTierMovementLocked())
    {
        g_state.currentTier = g_cfg.minTier;
        g_state.lowTimer = 0.0f;
        g_state.highTimer = 0.0f;
        g_state.emergencyTimer = 0.0f;
        g_state.inUpshiftProbe = false;
        g_state.probeRollbackTier = -1;
        g_state.autoState = "tier-locked";
        return;
    }

    if (g_state.stagedActive) { return; }

    // Preferred Comfy-style loading detection via NexusLinkData::IsGameplay.
    if (UpdateNexusLoadingGuard(dt)) { return; }

    // After loading, throw away transitional samples and give the world a moment to settle.
    if (g_state.postLoadingGrace > 0.0f)
    {
        ResetFrameStats(frameMs);
        g_state.lowTimer = g_state.highTimer = g_state.emergencyTimer = 0.0f;
        g_state.probeBadTimer = 0.0f;
        g_state.autoState = "post-loading-grace";
        return;
    }

    // v82: legacy 60 FPS loading/menu fallback removed. Nexus IsGameplay is the only loading detector.

    const float targetMs = TargetFrameMs();
    const float downMs = DownshiftFrameMs();
    const float stableMs = StableProbeFrameMs();
    const float emergencyMs = EmergencyFrameMs();

    // DRS-style gearbox health tests. Fast EMA catches current load, slow EMA avoids panic,
    // and P95 detects repeated spikes without letting one bad frame make the gearbox chatter.
    // v29: upshift readiness and upshift probe validation are both leaky-integrator signals.
    // A capped 144 FPS scene can have occasional P95 spikes or single-frame hitchlets. Those should
    // slow an upshift probe down, not instantly roll it back. Rollback now requires sustained real
    // over-budget frame-time, while downshift still reacts quickly.
    const float effectiveMinFps = EffectiveMinimumFps();
    const bool emergencyBad = (g_state.fastFrameMs >= emergencyMs) || (g_state.emaFps <= effectiveMinFps);
    const bool overBudget = ((g_state.fastFrameMs > downMs) && (g_state.slowFrameMs > targetMs)) ||
                            ((g_state.p95FrameMs > downMs * 1.12f) && (g_state.slowFrameMs > stableMs));
    const bool stableBase = (g_state.slowFrameMs <= stableMs) && !emergencyBad;
    const bool stableStrong = stableBase &&
                              (g_state.fastFrameMs <= downMs * 1.08f) &&
                              (g_state.p95FrameMs <= downMs * 1.18f);
    const bool stableSoft = stableBase &&
                            (g_state.fastFrameMs <= downMs * 1.35f) &&
                            (g_state.p95FrameMs <= downMs * 1.55f);
    const bool stableCandidate = !overBudget && (stableStrong || stableSoft);

    g_state.dbgTargetMs = targetMs;
    g_state.dbgDownMs = downMs;
    g_state.dbgStableMs = stableMs;
    g_state.dbgEmergencyMs = emergencyMs;
    g_state.dbgEmergencyBad = emergencyBad;
    g_state.dbgOverBudget = overBudget;
    g_state.dbgStableBase = stableBase;
    g_state.dbgStableStrong = stableStrong;
    g_state.dbgStableSoft = stableSoft;
    g_state.dbgStableCandidate = stableCandidate;
    g_state.dbgProbeMayJudge = false;
    g_state.dbgProbeHardBad = false;
    g_state.dbgProbeSoftBad = false;
    g_state.stableCreditRate = 0.0f;
    g_state.stableCreditDrainRate = 0.0f;

    if (emergencyBad) { g_state.emergencyTimer += dt; }
    else { g_state.emergencyTimer = 0.0f; }

    if (g_state.emergencyTimer >= g_cfg.emergencySeconds && g_state.currentTier < g_cfg.maxTier)
    {
        g_state.inUpshiftProbe = false;
        g_state.probeRollbackTier = -1;
        g_state.probeBadTimer = 0.0f;
        ApplyTier(g_cfg.maxTier, "emergency-frame-time");
        g_state.lowTimer = g_state.highTimer = g_state.emergencyTimer = 0.0f;
        g_state.hadDownshiftSeries = true;
        g_state.cooldown = g_cfg.cooldownSeconds;
        g_state.autoState = "emergency";
        ResetFrameStats(frameMs);
        return;
    }

    // Probe validation: good FPS at a cap proves nothing, so upshifts are experiments.
    if (g_state.inUpshiftProbe)
    {
        g_state.autoState = "upshift-probe";
        g_state.probeTimer += dt;

        const bool probeMayJudge = g_state.probeTimer >= EffectiveProbeWarmupSeconds();
        // Probe rollback is intentionally less twitchy than normal downshift.
        // A one-tier upshift is an experiment; minor P95 jitter at a frame cap should not immediately
        // undo it. Hard bad rolls back quickly; soft bad only leaks into the rollback timer slowly.
        const bool probeHardBad = emergencyBad ||
                                  ((g_state.fastFrameMs > downMs * 1.20f) && (g_state.slowFrameMs > targetMs * 1.05f)) ||
                                  (g_state.slowFrameMs > stableMs * 1.08f) ||
                                  ((g_state.p95FrameMs > downMs * 1.45f) && (g_state.slowFrameMs > targetMs * 1.02f));
        const bool probeSoftBad = !probeHardBad &&
                                  (((g_state.fastFrameMs > downMs) && (g_state.slowFrameMs > stableMs)) ||
                                   ((g_state.p95FrameMs > downMs * 1.30f) && (g_state.slowFrameMs > targetMs * 1.02f)));
        g_state.dbgProbeMayJudge = probeMayJudge;
        g_state.dbgProbeHardBad = probeHardBad;
        g_state.dbgProbeSoftBad = probeSoftBad;
        if (probeMayJudge && probeHardBad) { g_state.probeBadTimer += dt; }
        else if (probeMayJudge && probeSoftBad) { g_state.probeBadTimer += dt * 0.30f; }
        else { g_state.probeBadTimer = std::max(0.0f, g_state.probeBadTimer - dt * 0.85f); }

        if (g_state.probeBadTimer >= g_cfg.probeRollbackSeconds)
        {
            int rollback = std::clamp(g_state.probeRollbackTier, g_cfg.minTier, g_cfg.maxTier);
            ++g_state.failedProbeCount;
            const float backoff = FailedProbeBackoffSeconds();
            std::ostringstream ss;
            ss << "probe failed rollback tier " << g_state.currentTier << " -> " << rollback
               << " fps=" << std::fixed << std::setprecision(1) << g_state.fps
               << " ema=" << g_state.emaFps
               << " fastMs=" << std::setprecision(2) << g_state.fastFrameMs
               << " slowMs=" << g_state.slowFrameMs
               << " p95Ms=" << g_state.p95FrameMs
               << " hardBad=" << (probeHardBad ? "yes" : "no")
               << " softBad=" << (probeSoftBad ? "yes" : "no")
               << " badTimer=" << g_state.probeBadTimer
               << " backoff=" << backoff
               << " failures=" << g_state.failedProbeCount;
            AutoStateLog(ss.str());
            ApplyTier(rollback, "probe-rollback");
            g_state.inUpshiftProbe = false;
            g_state.probeRollbackTier = -1;
            g_state.probeTimer = 0.0f;
            g_state.probeBadTimer = 0.0f;
            g_state.highTimer = 0.0f;
            g_state.probeBackoff = backoff;
            g_state.cooldown = g_cfg.cooldownSeconds;
            ResetFrameStats(frameMs);
            return;
        }

        if (g_state.probeTimer >= g_cfg.upProbeSeconds)
        {
            // The probe duration expiring is not enough by itself. If the current end-state
            // is already bad enough that the normal governor would reject this tier, treat
            // the probe as failed and start the same backoff as a rollback timer failure.
            // This avoids "accepted" probes that immediately downshift a few frames later.
            if (!stableCandidate || probeHardBad)
            {
                int rollback = std::clamp(g_state.probeRollbackTier, g_cfg.minTier, g_cfg.maxTier);
                ++g_state.failedProbeCount;
                const float backoff = FailedProbeBackoffSeconds();
                std::ostringstream ss;
                ss << "probe failed end-state rollback tier " << g_state.currentTier << " -> " << rollback
                   << " fps=" << std::fixed << std::setprecision(1) << g_state.fps
                   << " ema=" << g_state.emaFps
                   << " fastMs=" << std::setprecision(2) << g_state.fastFrameMs
                   << " slowMs=" << g_state.slowFrameMs
                   << " p95Ms=" << g_state.p95FrameMs
                   << " stable=" << (stableCandidate ? "yes" : "no")
                   << " overBudget=" << (overBudget ? "yes" : "no")
                   << " hardBad=" << (probeHardBad ? "yes" : "no")
                   << " softBad=" << (probeSoftBad ? "yes" : "no")
                   << " badTimer=" << g_state.probeBadTimer
                   << " backoff=" << backoff
                   << " failures=" << g_state.failedProbeCount;
                AutoStateLog(ss.str());
                ApplyTier(rollback, "probe-rollback");
                g_state.inUpshiftProbe = false;
                g_state.probeRollbackTier = -1;
                g_state.probeTimer = 0.0f;
                g_state.probeBadTimer = 0.0f;
                g_state.highTimer = 0.0f;
                g_state.probeBackoff = backoff;
                g_state.cooldown = g_cfg.cooldownSeconds;
                ResetFrameStats(frameMs);
                return;
            }

            std::ostringstream ss;
            ss << "probe accepted tier=" << g_state.currentTier
               << " fps=" << std::fixed << std::setprecision(1) << g_state.fps
               << " ema=" << g_state.emaFps
               << " fastMs=" << std::setprecision(2) << g_state.fastFrameMs
               << " slowMs=" << g_state.slowFrameMs
               << " p95Ms=" << g_state.p95FrameMs
               << " duration=" << g_state.probeTimer;
            AutoStateLog(ss.str());
            g_state.inUpshiftProbe = false;
            g_state.probeRollbackTier = -1;
            g_state.probeTimer = 0.0f;
            g_state.probeBadTimer = 0.0f;
            g_state.highTimer = 0.0f;
            g_state.hadDownshiftSeries = false;
            g_state.failedProbeCount = 0;
            g_state.autoState = "stable";
            ResetFrameStats(frameMs);
        }
        return;
    }

    if (g_state.cooldown > 0.0f)
    {
        g_state.autoState = "settle";
        g_state.lowTimer = 0.0f;
        g_state.highTimer = 0.0f;
        return;
    }

    // Downshift swiftly but with proof over time. Repeat downshifts use a shorter hold.
    // If the lowest allowed tier is already active, there is nowhere further down to go,
    // so keep the downshift timer disabled instead of showing/counting useless credit.
    const bool canDownshift = g_state.currentTier < g_cfg.maxTier;
    if (overBudget)
    {
        g_state.highTimer = std::max(0.0f, g_state.highTimer - dt * 2.0f);
        g_state.stableCreditDrainRate = 2.0f;

        if (!canDownshift)
        {
            g_state.lowTimer = 0.0f;
            g_state.autoState = "lowest-tier-over-budget";
            return;
        }

        g_state.lowTimer += dt;
        // Do not hard-reset the upshift stability credit on every small over-budget blip.
        // A real fight/load spike will drain it quickly; one bad frame will not erase 10s of stability.
        const float required = g_state.hadDownshiftSeries ? g_cfg.downRepeatSeconds : g_cfg.downSeconds;
        g_state.autoState = g_state.hadDownshiftSeries ? "repeat-over-budget" : "over-budget";
        if (g_state.lowTimer >= required)
        {
            std::ostringstream reason;
            reason << (g_state.hadDownshiftSeries ? "repeat-downshift-frame-time" : "downshift-frame-time")
                   << " fastMs=" << std::fixed << std::setprecision(2) << g_state.fastFrameMs
                   << " slowMs=" << g_state.slowFrameMs
                   << " p95Ms=" << g_state.p95FrameMs
                   << " downMs=" << downMs;
            std::string reasonText = reason.str();
            ApplyTier(g_state.currentTier + 1, reasonText.c_str());
            g_state.lowTimer = 0.0f;
            g_state.hadDownshiftSeries = true;
            g_state.cooldown = g_cfg.cooldownSeconds;
            g_state.autoState = "downshift";
            ResetFrameStats(frameMs);
        }
        return;
    }

    g_state.lowTimer = 0.0f;

    // Reaching the target only means the current tier works. Upshift only as a slow one-tier probe.
    // v27: stable-hold is now credit-based. Strong stability earns full credit; softer capped-FPS
    // stability earns partial credit. Non-bad uncertainty decays slowly instead of resetting.
    if (stableCandidate && g_state.currentTier > g_cfg.minTier && g_state.probeBackoff <= 0.0f)
    {
        const float creditRate = stableStrong ? 1.0f : 0.35f;
        g_state.stableCreditRate = creditRate;
        g_state.highTimer += dt * creditRate;
        g_state.autoState = stableStrong ? "stable-hold" : "stable-hold-soft";
        if (g_state.highTimer >= g_cfg.upSeconds)
        {
            int oldTier = g_state.currentTier;
            int newTier = std::max(g_cfg.minTier, oldTier - 1);
            g_state.probeRollbackTier = oldTier;
            g_state.inUpshiftProbe = true;
            g_state.probeTimer = 0.0f;
            g_state.probeBadTimer = 0.0f;
            g_state.highTimer = 0.0f;
            ApplyTier(newTier, "probe-upshift-frame-time");
            g_state.autoState = "upshift-probe";
            ResetFrameStats(frameMs);
            return;
        }
    }
    else
    {
        if (emergencyBad)
        {
            g_state.highTimer = 0.0f;
            g_state.stableCreditDrainRate = 999.0f;
        }
        else
        {
            // Let stable credit decay through jitter/cap artifacts instead of dropping to zero.
            g_state.highTimer = std::max(0.0f, g_state.highTimer - dt * 0.35f);
            g_state.stableCreditDrainRate = 0.35f;
        }
        g_state.autoState = (g_state.probeBackoff > 0.0f) ? "probe-backoff" : "hold";
    }
}

