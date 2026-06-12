// Public Visual Governor implementation section. Contains UI, config, and governor logic.
// Included by Addon.cpp to keep this refactor source-compatible.

// Public graphics-row model.  This stays in the public tree because it only
// describes the user-visible tier table.  The private backend maps these row
// keys to GW2 internals at runtime.
enum class ExtraGearKind
{
    EnumSetting,
    BoolFlag
};

struct ExtraGearDef
{
    const char* key;
    const char* label;
    ExtraGearKind kind;
    int minValue;
    int maxValue;
    int flagBit;
    const char* const* names;
    int nameCount;
    bool advanced;
    bool experimental = false;
};

struct ExtraGearConfig
{
    bool enabled = true;
    std::array<int, kStoredTierSlots> value{ 0, 0, 0, 0, 0 };
};

constexpr size_t kExtraGearSettingCount = 17;
constexpr int kGovernorTierCount = 4;
constexpr int kMaxGovernorTier = kGovernorTierCount - 1;

struct Config
{
    bool windowVisible = false;             // standalone debug window; settings live in the Nexus addon settings panel
    bool tierOverlayVisible = true;         // tiny movable current-tier overlay
    bool tierOverlayPosValid = false;
    float tierOverlayPosX = 18.0f;
    float tierOverlayPosY = 120.0f;
    bool controllerEnabled = true;
    bool tracerEnabled = false;
    bool liveWritesEnabled = true;
    bool stagedApplyEnabled = true;
    int stagedApplyDelayMs = 12;
    // v50: when GW2 says the settings field already equals the target, force a real
    // value transition (neighbor value -> target) instead of relying only on dirty-bit
    // refresh. This catches stale renderer/options-menu state without direct memory writes.
    bool sameValueBounceEnabled = true;
    bool stagedRecoverFromLowest = true;
    bool controlLimitLive = true;  // allow the automatic tier system to control Character Model Limit
    bool controlQualityLive = true; // allow the automatic tier system to control Character Model Quality
    int manualLimitValue = 4;
    int manualQualityValue = 4;

    float targetFps = 144.0f;
    float minFps = 100.0f;
    // v26 defaults are based on the later meta-event config.
    // Margins are stored as percentages of target FPS; this scales better for 60/120/144/240 FPS users.
    // Example at 143 FPS: 8.4% ~= 12 FPS downshift margin; 20.6% ~= 29.5 FPS probe margin.
    float downMarginPercent = 8.4f;           // downshift when below target by this percentage
    float upMarginPercent = 20.6f;            // stable/probe threshold below target, as percentage
    float downSeconds = 1.3f;                 // first downshift hold
    float downRepeatSeconds = 0.75f;          // repeat downshift hold after the first step
    float upSeconds = 10.0f;                  // stable time before probing one tier up
    float upProbeSeconds = 4.5f;              // time to validate an upshift probe
    float upProbeBackoffSeconds = 35.0f;      // wait after failed probe
    float cooldownSeconds = 0.70f;            // small apply spacing after downshifts
    float emaAlpha = 0.10f;
    float emergencySeconds = 0.5f;
    bool nexusLoadingGuardEnabled = true;     // NexusLinkData::IsGameplay loading detection. Legacy 60 FPS fallback removed in v82.
    float postLoadingGraceSeconds = 2.5f;
    int minTier = 0;
    int maxTier = kMaxGovernorTier;
    int startTier = 0;
    bool autoChangeLogEnabled = false;        // off by default; enable from Debug when diagnosing
    bool startupProfileApplyEnabled = true;   // v30: one-time sync after startup so GW2 and governor begin from the same tier
    float startupProfileApplyDelaySeconds = 2.0f;
    int governorPreset = 1;                    // 0=Swift, 1=Balanced, 2=Calm
    bool frameTimeGovernor = true;             // v24: DRS-style frame-time gearbox instead of raw FPS switching
    float frameFastAlpha = 0.28f;              // FPS Drop Reaction Smoothing, stored as 0.28 = 28%
    float frameSlowAlpha = 0.055f;             // FPS Recovery Smoothing, stored as 0.055 = 5.5%
    float probeWarmupSeconds = 1.1f;           // ignore immediate apply/rebuild turbulence during an upshift probe
    float probeRollbackSeconds = 1.25f;        // sustained bad time required before rolling back a probe
    float maxProbeBackoffSeconds = 160.0f;     // exponential failed-probe backoff cap
    std::array<int, kStoredTierSlots> tierLimitValues{ 3, 1, 1, 0, 0 };
    std::array<int, kStoredTierSlots> tierQualityValues{ 3, 2, 1, 0, 0 };
    bool fullGearMatrixEnabled = true;
    bool extraGearLiveApplyEnabled = true;    // v35: full graphics matrix live-apply is enabled; individual rows can still be disabled.
    int extraGearApplySpacingMs = 50;         // current release default: keep tier-row applies responsive while still queued.
    bool extraGearSkipUnchanged = false;      // v44: manual/UI applies force rows; default off avoids stale renderer state when raw fields already match.
    std::array<ExtraGearConfig, kExtraGearSettingCount> extraGears{};
} g_cfg;

// Public/native boundary. Public governor/UI code sees only this generic backend summary.
struct VisualBackendStatus
{
    bool available = false;
    bool initialized = false;
    bool hooksInstalled = false;
    bool settingsCaptured = false;
    bool characterLimitAvailable = false;
    bool characterQualityAvailable = false;
    bool graphicsMatrixAvailable = false;
    bool shadowRefreshAvailable = false;
    bool shadowContextCaptured = false;
    bool capturedLimitContext = false;
    bool capturedQualityContext = false;
    int observedLimit = -1;
    int observedQuality = -1;
    uint64_t shadowCaptures = 0;
    uint64_t shadowRefreshes = 0;
    uint32_t renderThreadId = 0;
    uint32_t gw2ThreadId = 0;
    uint32_t currentThreadId = 0;
    uint64_t nativeApplyCalls = 0;
    uint64_t nativeApplyThreadMismatches = 0;
    std::string summary = "not initialized";
};

VisualBackendStatus GetVisualBackendStatus();
void VisualBackendInitialize();
void VisualBackendInstallHooks();
void VisualBackendShutdown();
void VisualBackendRefreshObserved();
void VisualBackendNotifyLoadingEnter();
void VisualBackendMarkRenderThread();
void VisualBackendCaptureOptionsProbeBaseline(const char* label);
void VisualBackendDiffOptionsProbe(const char* label);
void VisualBackendLogOptionsProbeKnownFields(const char* label);

const char* NameFromList(const char* const* names, int count, int v)
{
    if (!names || v < 0 || v >= count) { return "?"; }
    return names[v];
}

static const char* kOffOnNames[] = { "off", "on" };
static const char* kLowMedHighNames[] = { "low", "medium", "high" };
static const char* kLowHighNames[] = { "low", "high" };
static const char* kNoneLowMedHighNames[] = { "none", "low", "medium", "high" };
static const char* kLowMedHighUltraNames[] = { "low", "medium", "high", "ultra" };
static const char* kModelNames[] = { "lowest", "low", "medium", "high", "highest" };
static const char* kReflectionsNames[] = { "none", "terrain & sky", "all" };
static const char* kRenderSamplingNames[] = { "subsample", "native", "supersample" };
static const char* kPostProcessingNames[] = { "none", "low", "medium", "high" };
static const char* kShadowsPresetNames[] = { "none", "low", "medium", "high", "ultra", "custom" };
static const char* kShadowResolutionNames[] = { "512", "1024", "2048", "4096" };
static const char* kShadowCascadeNames[] = { "1", "2", "3", "4" };
static const char* kAntiAliasingNames[] = { "none", "FXAA", "SMAA Low", "SMAA High" };
static const char* kHideAllyCandidateNames[] = { "0", "1", "2", "3", "4" };

static const std::array<ExtraGearDef, kExtraGearSettingCount> kExtraGearDefs = {{
    { "animation", "Animation", ExtraGearKind::EnumSetting, 0, 2, -1, kLowMedHighNames, 3, false },
    { "antiAliasing", "Antialiasing", ExtraGearKind::EnumSetting, 0, 3, -1, kAntiAliasingNames, 4, true },
    { "environment", "Environment", ExtraGearKind::EnumSetting, 0, 1, -1, kLowHighNames, 2, false },
    { "lodDistance", "LOD Distance", ExtraGearKind::EnumSetting, 0, 3, -1, kLowMedHighUltraNames, 4, false },
    { "reflections", "Reflections", ExtraGearKind::EnumSetting, 0, 2, -1, kReflectionsNames, 3, false },
    { "renderSampling", "Render Sampling", ExtraGearKind::EnumSetting, 0, 2, -1, kRenderSamplingNames, 3, false },
    { "shaders", "Shaders", ExtraGearKind::EnumSetting, 0, 2, -1, kLowMedHighNames, 3, false },
    { "shadows", "Shadows", ExtraGearKind::EnumSetting, 0, 5, -1, kShadowsPresetNames, 6, false },
    { "shadowResolution", "Shadow resolution", ExtraGearKind::EnumSetting, 0, 3, -1, kShadowResolutionNames, 4, false },
    { "shadowCascades", "Shadow cascade count", ExtraGearKind::EnumSetting, 0, 3, -1, kShadowCascadeNames, 4, false },
    { "textures", "Textures", ExtraGearKind::EnumSetting, 0, 2, -1, kLowMedHighNames, 3, false },
    { "postProcessing", "Postprocessing preset", ExtraGearKind::EnumSetting, 0, 3, -1, kPostProcessingNames, 4, false },
    { "bestTextureFiltering", "Best Texture Filtering", ExtraGearKind::BoolFlag, 0, 1, 1, kOffOnNames, 2, false },
    { "effectLod", "Effect LOD", ExtraGearKind::BoolFlag, 0, 1, 4, kOffOnNames, 2, false },
    { "highResCharacter", "High-Res Character Textures", ExtraGearKind::BoolFlag, 0, 1, 5, kOffOnNames, 2, false },
    { "screenSpaceShadows", "Screen Space Shadows", ExtraGearKind::BoolFlag, 0, 1, 22, kOffOnNames, 2, false },
    { "hideAllyEffects", "Hide Ally Visual Effects candidate", ExtraGearKind::EnumSetting, 0, 4, -1, kHideAllyCandidateNames, 5, true, true },
} };


int ExtraUiToGameValue(const ExtraGearDef& d, int uiValue)
{
    // Some GW2 setters use 1-based values internally while the addon UI keeps
    // compact combo indices. Other rows use their UI value directly.
    if (std::strcmp(d.key, "shaders") == 0)
    {
        return std::clamp(uiValue, 0, 2) + 1;
    }
    // Texture Detail is one of the rows where GW2's native enum is reversed:
    // 0 = high, 1 = medium, 2 = low.  The addon UI intentionally stays
    // human-ordered as low/medium/high, so convert here.  v44 passed the UI
    // index through directly; selecting "high" sent 2 and the game showed Low.
    if (std::strcmp(d.key, "textures") == 0)
    {
        return 2 - std::clamp(uiValue, 0, 2);
    }
    // Shadow resolution and cascade count are displayed as 512/1024/2048/4096
    // and 1/2/3/4 in the addon UI, but GW2 stores/packs the child values as
    // one-based values 1..4. v38-v40 incorrectly converted these rows to
    // zero-based 0..3; that made the high tiers apply one step too low and left
    // Shadows=Custom with child values that could later trip GsContext.cpp
    // "Packing mismatch" when the native Graphics Options panel repacked them.
    if (std::strcmp(d.key, "shadowResolution") == 0 || std::strcmp(d.key, "shadowCascades") == 0)
    {
        return std::clamp(uiValue, 0, 3) + 1;
    }
    return std::clamp(uiValue, d.minValue, d.maxValue);
}

int ExtraSameValueBounceUiValue(const ExtraGearDef& d, int uiValue)
{
    const int current = std::clamp(uiValue, d.minValue, d.maxValue);
    if (d.maxValue <= d.minValue) { return current; }

    // Prefer a visually adjacent lower value for high-tier tests, then restore
    // the requested target immediately through GW2's own setter. This keeps the
    // transition legal for reversed/1-based native rows because conversion happens
    // after choosing the UI-side neighbor.
    if (current > d.minValue) { return current - 1; }
    return current + 1;
}

int ModelSameValueBounceValue(int value)
{
    value = std::clamp(value, 0, 4);
    return (value > 0) ? (value - 1) : 1;
}

bool StableEnumCombo(const char* id, int* value, const char* const* names, int count)
{
    if (!value || !names || count <= 0) { return false; }
    int v = std::clamp(*value, 0, count - 1);
    bool changed = false;
    if (v != *value)
    {
        *value = v;
        changed = true;
    }

    const char* preview = NameFromList(names, count, v);
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightRegular))
    {
        for (int i = 0; i < count; ++i)
        {
            const bool selected = (v == i);
            if (ImGui::Selectable(NameFromList(names, count, i), selected))
            {
                *value = i;
                v = i;
                changed = true;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void InitExtraGearDefaults()
{
    for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
    {
        const auto& d = kExtraGearDefs[n];
        auto& c = g_cfg.extraGears[n];
        for (int t = 0; t < kGovernorTierCount; ++t)
        {
            int span = d.maxValue - d.minValue;
            int value = d.maxValue - static_cast<int>((static_cast<float>(t) / static_cast<float>(kMaxGovernorTier)) * static_cast<float>(span) + 0.5f);
            c.value[static_cast<size_t>(t)] = std::clamp(value, d.minValue, d.maxValue);
        }
        c.value[4] = c.value[3]; // legacy hidden slot; not shown/applied.
        c.enabled = true;
    }

    // v71 release defaults: these match the author's current tested profile.
    // Tier 0 is not absolute max-everything; it is the normal high-quality baseline
    // that has proven stable for live startup sync and automatic governor movement.
    auto setRow = [](size_t n, bool enabled, std::array<int, kStoredTierSlots> values)
    {
        if (n >= g_cfg.extraGears.size()) { return; }
        g_cfg.extraGears[n].enabled = enabled;
        g_cfg.extraGears[n].value = values;
    };

    setRow(0,  true,  { 2, 1, 1, 0, 0 }); // Animation: high, medium, medium, low.
    setRow(1,  true,  { 3, 3, 2, 1, 1 }); // Antialiasing: SMAA high, SMAA high, SMAA low, FXAA.
    setRow(2,  true,  { 1, 1, 0, 0, 0 }); // Environment.
    setRow(3,  true,  { 3, 2, 1, 1, 1 }); // LOD distance.
    setRow(4,  true,  { 1, 1, 0, 0, 0 }); // Reflections.
    setRow(5,  true,  { 2, 2, 1, 1, 1 }); // Render sampling: supersample, supersample, native, native.
    setRow(6,  true,  { 2, 2, 2, 0, 0 }); // Shaders.
    setRow(7,  true,  { 1, 1, 1, 1, 1 }); // Shadows: low presets for stable live changes.
    setRow(8,  false, { 3, 2, 1, 1, 1 }); // Shadow resolution: kept for Custom, disabled by default.
    setRow(9,  false, { 3, 2, 1, 1, 1 }); // Shadow cascades: kept for Custom, disabled by default.
    setRow(10, true,  { 2, 2, 2, 2, 2 }); // Textures stay high.
    setRow(11, true,  { 0, 0, 0, 0, 0 }); // Postprocessing off.
    setRow(12, true,  { 1, 1, 1, 0, 0 }); // Best Texture Filtering.
    setRow(13, true,  { 1, 1, 1, 1, 1 }); // Effect LOD.
    setRow(14, true,  { 1, 1, 1, 0, 0 }); // High-res character textures.
    setRow(15, true,  { 1, 1, 0, 0, 0 }); // Screen-space shadows.
    setRow(16, false, { 4, 3, 1, 0, 0 }); // Hide Ally Effects candidate: opt-in only.
}
void ApplyV44FourTierDefaults()
{
    g_cfg.minTier = 0;
    g_cfg.maxTier = kMaxGovernorTier;
    g_cfg.startTier = std::clamp(g_cfg.startTier, 0, kMaxGovernorTier);
    g_cfg.tierLimitValues = { 3, 1, 1, 0, 0 };
    g_cfg.tierQualityValues = { 3, 2, 1, 0, 0 };
    InitExtraGearDefaults();
    g_cfg.extraGearSkipUnchanged = false;
    if (g_cfg.extraGearApplySpacingMs < 50) { g_cfg.extraGearApplySpacingMs = 50; }
}

struct State
{
    float fps = 0.0f;
    float uiFps = 0.0f;
    float uiFpsRefreshTimer = 0.0f;
    float emaFps = 0.0f;
    float frameMs = 0.0f;
    float fastFrameMs = 0.0f;
    float slowFrameMs = 0.0f;
    float p95FrameMs = 0.0f;
    std::array<float, 240> frameMsWindow{};
    int frameMsIndex = 0;
    int frameMsCount = 0;
    float lowTimer = 0.0f;
    float highTimer = 0.0f;
    float cooldown = 0.0f;
    float emergencyTimer = 0.0f;
    float postLoadingGrace = 0.0f;
    bool nexusTimingInitialized = false;
    bool nexusWasGameplay = false;
    bool nexusInLoading = false;
    float nexusLoadingSeconds = 0.0f;
    float probeTimer = 0.0f;
    float probeBackoff = 0.0f;
    float probeBadTimer = 0.0f;

    // Debug mirrors from the frame-time gearbox. These are intentionally not saved;
    // they make it easy to see why stable credit is or is not accumulating.
    float dbgTargetMs = 0.0f;
    float dbgDownMs = 0.0f;
    float dbgStableMs = 0.0f;
    float dbgEmergencyMs = 0.0f;
    float stableCreditRate = 0.0f;
    float stableCreditDrainRate = 0.0f;
    bool dbgEmergencyBad = false;
    bool dbgOverBudget = false;
    bool dbgStableBase = false;
    bool dbgStableStrong = false;
    bool dbgStableSoft = false;
    bool dbgStableCandidate = false;
    bool dbgProbeMayJudge = false;
    bool dbgProbeHardBad = false;
    bool dbgProbeSoftBad = false;

    int failedProbeCount = 0;
    int probeRollbackTier = -1;
    bool inLoadingGuard = false;
    bool inUpshiftProbe = false;
    bool hadDownshiftSeries = false;
    bool autoWasEnabled = false;
    float startupProfileApplyTimer = 0.0f;
    bool startupProfileApplyDone = false;
    bool startupProfileApplyWaiting = false;
    bool startupProfileApplySeenGameplay = false;
    std::string autoState = "idle";
    int currentTier = 0;
    std::atomic<int> observedLimit{-1};
    std::atomic<int> observedQuality{-1};
    std::string status = "idle";
    bool extraGearQueueActive = false;
    int extraGearQueueTier = 0;
    size_t extraGearQueueIndex = 0;
    size_t extraGearQueueEndIndex = kExtraGearSettingCount;
    float extraGearQueueTimer = 0.0f;
    float extraGearQueueBlockedSeconds = 0.0f;
    float extraGearQueueLastWaitLog = -10.0f;
    bool extraGearQueueManual = false;
    bool extraGearQueueForceAll = false;
    bool extraGearPendingActive = false;
    int extraGearPendingTier = 0;
    bool extraGearPendingManual = false;
    bool extraGearPendingForceAll = false;
    size_t extraGearPendingStartIndex = 0;
    size_t extraGearPendingEndIndex = kExtraGearSettingCount;
    std::string extraGearPendingReason;
    std::string extraGearQueueReason;
    std::string extraGearQueueWaitReason;
    bool stagedActive = false;
    int stagedLimit = 4;
    int stagedQuality = 4;
    int stagedStep = 0;
    float stagedTimer = 0.0f;
    std::string stagedReason;
    bool stagedRecoveryOrder = false;
    bool stagedQualityHardRefresh = false;

    // v55 shadow live-apply helper. GW2's native shadow setter updates Options
    // state and the runtime shadow preset tail, but repeated live preset changes
    // can still stay visually stale until the next map/context rebuild. A short
    // delayed runtime-only preset pulse forces the shadow renderer to tear down
    // and settle back to the requested preset without touching the packed
    // settings object or the unsafe vtable+0xC8 refresh path.
} g_state;

void SaveConfig()
{
    try
    {
        std::ofstream out(g_configPath, std::ios::binary | std::ios::trunc);
        out << "configVersion=83\n";
        out << "windowVisible=" << (g_cfg.windowVisible ? 1 : 0) << "\n";
        out << "tierOverlayVisible=" << (g_cfg.tierOverlayVisible ? 1 : 0) << "\n";
        out << "tierOverlayPosValid=" << (g_cfg.tierOverlayPosValid ? 1 : 0) << "\n";
        out << "tierOverlayPosX=" << g_cfg.tierOverlayPosX << "\n";
        out << "tierOverlayPosY=" << g_cfg.tierOverlayPosY << "\n";
        out << "controllerEnabled=" << (g_cfg.controllerEnabled ? 1 : 0) << "\n";
        out << "tracerEnabled=" << (g_cfg.tracerEnabled ? 1 : 0) << "\n";
        out << "liveWritesEnabled=" << (g_cfg.liveWritesEnabled ? 1 : 0) << "\n";
        out << "stagedApplyEnabled=" << (g_cfg.stagedApplyEnabled ? 1 : 0) << "\n";
        out << "stagedApplyDelayMs=" << g_cfg.stagedApplyDelayMs << "\n";
        out << "sameValueBounceEnabled=" << (g_cfg.sameValueBounceEnabled ? 1 : 0) << "\n";
        out << "stagedRecoverFromLowest=" << (g_cfg.stagedRecoverFromLowest ? 1 : 0) << "\n";
        out << "controlLimitLive=" << (g_cfg.controlLimitLive ? 1 : 0) << "\n";
        out << "controlQualityLive=" << (g_cfg.controlQualityLive ? 1 : 0) << "\n";
        out << "manualLimitValue=" << g_cfg.manualLimitValue << "\n";
        out << "manualQualityValue=" << g_cfg.manualQualityValue << "\n";
        out << "targetFps=" << g_cfg.targetFps << "\n";
        out << "minFps=" << g_cfg.minFps << "\n";
        out << "downMarginPercent=" << g_cfg.downMarginPercent << "\n";
        out << "upMarginPercent=" << g_cfg.upMarginPercent << "\n";
        // Legacy absolute-FPS margin keys for easier comparison with older configs.
        out << "downDelta=" << (g_cfg.targetFps * g_cfg.downMarginPercent * 0.01f) << "\n";
        out << "upDelta=" << (g_cfg.targetFps * g_cfg.upMarginPercent * 0.01f) << "\n";
        out << "downSeconds=" << g_cfg.downSeconds << "\n";
        out << "downRepeatSeconds=" << g_cfg.downRepeatSeconds << "\n";
        out << "upSeconds=" << g_cfg.upSeconds << "\n";
        out << "upProbeSeconds=" << g_cfg.upProbeSeconds << "\n";
        out << "upProbeBackoffSeconds=" << g_cfg.upProbeBackoffSeconds << "\n";
        out << "cooldownSeconds=" << g_cfg.cooldownSeconds << "\n";
        out << "emergencySeconds=" << g_cfg.emergencySeconds << "\n";
        out << "nexusLoadingGuardEnabled=" << (g_cfg.nexusLoadingGuardEnabled ? 1 : 0) << "\n";
        out << "postLoadingGraceSeconds=" << g_cfg.postLoadingGraceSeconds << "\n";
        out << "emaAlpha=" << g_cfg.emaAlpha << "\n";
        out << "minTier=" << g_cfg.minTier << "\n";
        out << "maxTier=" << g_cfg.maxTier << "\n";
        out << "startTier=" << g_cfg.startTier << "\n";
        out << "autoChangeLogEnabled=" << (g_cfg.autoChangeLogEnabled ? 1 : 0) << "\n";
        out << "startupProfileApplyEnabled=" << (g_cfg.startupProfileApplyEnabled ? 1 : 0) << "\n";
        out << "startupProfileApplyDelaySeconds=" << g_cfg.startupProfileApplyDelaySeconds << "\n";
        out << "governorPreset=" << g_cfg.governorPreset << "\n";
        out << "frameTimeGovernor=" << (g_cfg.frameTimeGovernor ? 1 : 0) << "\n";
        out << "frameFastAlpha=" << g_cfg.frameFastAlpha << "\n";
        out << "frameSlowAlpha=" << g_cfg.frameSlowAlpha << "\n";
        out << "probeWarmupSeconds=" << g_cfg.probeWarmupSeconds << "\n";
        out << "probeRollbackSeconds=" << g_cfg.probeRollbackSeconds << "\n";
        out << "maxProbeBackoffSeconds=" << g_cfg.maxProbeBackoffSeconds << "\n";
        for (int t = 0; t < kGovernorTierCount; ++t)
        {
            out << "tierLimit" << t << "=" << g_cfg.tierLimitValues[static_cast<size_t>(t)] << "\n";
            out << "tierQuality" << t << "=" << g_cfg.tierQualityValues[static_cast<size_t>(t)] << "\n";
        }
        out << "fullGearMatrixEnabled=" << (g_cfg.fullGearMatrixEnabled ? 1 : 0) << "\n";
        out << "extraGearLiveApplyEnabled=" << (g_cfg.extraGearLiveApplyEnabled ? 1 : 0) << "\n";
        out << "extraGearApplySpacingMs=" << g_cfg.extraGearApplySpacingMs << "\n";
        out << "extraGearSkipUnchanged=" << (g_cfg.extraGearSkipUnchanged ? 1 : 0) << "\n";
        // Legacy allGraphicsGovernorVxx markers are no longer written.
        // configVersion above replaces the old migration-marker spam.
        for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
        {
            const auto& d = kExtraGearDefs[n];
            const auto& c = g_cfg.extraGears[n];
            out << "gear." << d.key << ".enabled=" << (c.enabled ? 1 : 0) << "\n";
            for (int t = 0; t < kGovernorTierCount; ++t)
            {
                out << "gear." << d.key << ".tier" << t << "=" << c.value[static_cast<size_t>(t)] << "\n";
            }
        }
    }
    catch (...) {}
}

void LoadConfig()
{
    int configVersion = 0;
    bool sawDownMarginPercent = false;
    bool sawUpMarginPercent = false;
    bool sawLegacyDownDelta = false;
    bool sawLegacyUpDelta = false;
    bool sawAllGraphicsGovernorV35 = false;
    bool sawAllGraphicsGovernorV36 = false;
    bool sawAllGraphicsGovernorV37 = false;
    bool sawAllGraphicsGovernorV39 = false;
    bool sawAllGraphicsGovernorV40 = false;
    bool sawAllGraphicsGovernorV41 = false;
    bool sawAllGraphicsGovernorV42 = false;
    bool sawAllGraphicsGovernorV43 = false;
    bool sawAllGraphicsGovernorV44 = false;
    bool sawAllGraphicsGovernorV45 = false;
    bool sawAllGraphicsGovernorV47 = false;
    bool sawAllGraphicsGovernorV48 = false;
    bool sawAllGraphicsGovernorV50 = false;
    bool sawAllGraphicsGovernorV51 = false;
    bool sawAllGraphicsGovernorV52 = false;
    bool sawAllGraphicsGovernorV56 = false;
    bool sawAllGraphicsGovernorV57 = false;
    bool sawAllGraphicsGovernorV58 = false;
    bool sawAllGraphicsGovernorV59 = false;
    bool sawAllGraphicsGovernorV60 = false;
    bool sawAllGraphicsGovernorV63 = false;
    bool sawAllGraphicsGovernorV66 = false;
    float legacyDownDelta = 0.0f;
    float legacyUpDelta = 0.0f;
    try
    {
        std::ifstream in(g_configPath, std::ios::binary);
        std::string line;
        while (std::getline(in, line))
        {
            auto eq = line.find('=');
            if (eq == std::string::npos) { continue; }
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            auto f = [&]() { return std::stof(v); };
            auto i = [&]() { return std::stoi(v); };
            if (k == "configVersion") { configVersion = i(); }
            else if (k == "windowVisible") { g_cfg.windowVisible = i() != 0; }
            else if (k == "tierOverlayVisible") { g_cfg.tierOverlayVisible = i() != 0; }
            else if (k == "tierOverlayPosValid") { g_cfg.tierOverlayPosValid = i() != 0; }
            else if (k == "tierOverlayPosX") { g_cfg.tierOverlayPosX = f(); }
            else if (k == "tierOverlayPosY") { g_cfg.tierOverlayPosY = f(); }
            else if (k == "controllerEnabled") { g_cfg.controllerEnabled = i() != 0; }
            else if (k == "tracerEnabled") { g_cfg.tracerEnabled = i() != 0; }
            else if (k == "liveWritesEnabled") { g_cfg.liveWritesEnabled = i() != 0; }
            else if (k == "stagedApplyEnabled") { g_cfg.stagedApplyEnabled = i() != 0; }
            else if (k == "stagedApplyDelayMs") { g_cfg.stagedApplyDelayMs = i(); }
            else if (k == "sameValueBounceEnabled") { g_cfg.sameValueBounceEnabled = i() != 0; }
            else if (k == "stagedRecoverFromLowest") { g_cfg.stagedRecoverFromLowest = i() != 0; }
            else if (k == "controlLimitLive") { g_cfg.controlLimitLive = i() != 0; }
            else if (k == "controlQualityLive") { g_cfg.controlQualityLive = i() != 0; }
            else if (k == "manualLimitValue") { g_cfg.manualLimitValue = i(); }
            else if (k == "manualQualityValue") { g_cfg.manualQualityValue = i(); }
            else if (k == "targetFps") { g_cfg.targetFps = f(); }
            else if (k == "minFps") { g_cfg.minFps = f(); }
            else if (k == "downMarginPercent") { g_cfg.downMarginPercent = f(); sawDownMarginPercent = true; }
            else if (k == "upMarginPercent") { g_cfg.upMarginPercent = f(); sawUpMarginPercent = true; }
            else if (k == "downDelta") { legacyDownDelta = f(); sawLegacyDownDelta = true; }
            else if (k == "upDelta") { legacyUpDelta = f(); sawLegacyUpDelta = true; }
            else if (k == "downSeconds") { g_cfg.downSeconds = f(); }
            else if (k == "downRepeatSeconds") { g_cfg.downRepeatSeconds = f(); }
            else if (k == "upSeconds") { g_cfg.upSeconds = f(); }
            else if (k == "upProbeSeconds") { g_cfg.upProbeSeconds = f(); }
            else if (k == "upProbeBackoffSeconds") { g_cfg.upProbeBackoffSeconds = f(); }
            else if (k == "cooldownSeconds") { g_cfg.cooldownSeconds = f(); }
            else if (k == "emergencySeconds") { g_cfg.emergencySeconds = f(); }
            else if (k == "nexusLoadingGuardEnabled") { g_cfg.nexusLoadingGuardEnabled = i() != 0; }
            // v82: legacy loadingGuardMode/loadingFpsCenter/loadingFpsWindow/loadingDetectSeconds keys are ignored.
            else if (k == "postLoadingGraceSeconds") { g_cfg.postLoadingGraceSeconds = f(); }
            else if (k == "emaAlpha") { g_cfg.emaAlpha = f(); }
            else if (k == "minTier") { g_cfg.minTier = i(); }
            else if (k == "maxTier") { g_cfg.maxTier = i(); }
            else if (k == "startTier") { g_cfg.startTier = i(); }
            else if (k == "autoChangeLogEnabled") { g_cfg.autoChangeLogEnabled = i() != 0; }
            else if (k == "startupProfileApplyEnabled") { g_cfg.startupProfileApplyEnabled = i() != 0; }
            else if (k == "startupProfileApplyDelaySeconds") { g_cfg.startupProfileApplyDelaySeconds = f(); }
            else if (k == "governorPreset") { g_cfg.governorPreset = i(); }
            else if (k == "frameTimeGovernor") { g_cfg.frameTimeGovernor = i() != 0; }
            else if (k == "frameFastAlpha") { g_cfg.frameFastAlpha = f(); }
            else if (k == "frameSlowAlpha") { g_cfg.frameSlowAlpha = f(); }
            else if (k == "probeWarmupSeconds") { g_cfg.probeWarmupSeconds = f(); }
            else if (k == "probeRollbackSeconds") { g_cfg.probeRollbackSeconds = f(); }
            else if (k == "maxProbeBackoffSeconds") { g_cfg.maxProbeBackoffSeconds = f(); }
            else if (k.rfind("tierLimit", 0) == 0)
            {
                int idx = std::stoi(k.substr(9));
                if (idx >= 0 && idx < 5) { g_cfg.tierLimitValues[static_cast<size_t>(idx)] = i(); }
            }
            else if (k.rfind("tierQuality", 0) == 0)
            {
                int idx = std::stoi(k.substr(11));
                if (idx >= 0 && idx < 5) { g_cfg.tierQualityValues[static_cast<size_t>(idx)] = i(); }
            }
            else if (k == "fullGearMatrixEnabled") { g_cfg.fullGearMatrixEnabled = i() != 0; }
            else if (k == "extraGearLiveApplyEnabled") { g_cfg.extraGearLiveApplyEnabled = i() != 0; }
            else if (k == "extraGearApplySpacingMs") { g_cfg.extraGearApplySpacingMs = i(); }
            else if (k == "extraGearSkipUnchanged") { g_cfg.extraGearSkipUnchanged = i() != 0; }
            else if (k == "allGraphicsGovernorV41") { sawAllGraphicsGovernorV41 = i() != 0; }
            else if (k == "allGraphicsGovernorV42") { sawAllGraphicsGovernorV42 = i() != 0; }
            else if (k == "allGraphicsGovernorV43") { sawAllGraphicsGovernorV43 = i() != 0; }
            else if (k == "allGraphicsGovernorV44") { sawAllGraphicsGovernorV44 = i() != 0; }
            else if (k == "allGraphicsGovernorV45") { sawAllGraphicsGovernorV45 = i() != 0; }
            else if (k == "allGraphicsGovernorV46") { /* v46 marker */ }
            else if (k == "allGraphicsGovernorV47") { sawAllGraphicsGovernorV47 = i() != 0; }
            else if (k == "allGraphicsGovernorV48") { sawAllGraphicsGovernorV48 = i() != 0; }
            else if (k == "allGraphicsGovernorV50") { sawAllGraphicsGovernorV50 = i() != 0; }
            else if (k == "allGraphicsGovernorV51") { sawAllGraphicsGovernorV51 = i() != 0; }
            else if (k == "allGraphicsGovernorV52") { sawAllGraphicsGovernorV52 = i() != 0; }
            else if (k == "allGraphicsGovernorV56") { sawAllGraphicsGovernorV56 = i() != 0; }
            else if (k == "allGraphicsGovernorV57") { sawAllGraphicsGovernorV57 = i() != 0; }
            else if (k == "allGraphicsGovernorV58") { sawAllGraphicsGovernorV58 = i() != 0; }
            else if (k == "allGraphicsGovernorV59") { sawAllGraphicsGovernorV59 = i() != 0; }
            else if (k == "allGraphicsGovernorV60") { sawAllGraphicsGovernorV60 = i() != 0; }
            else if (k == "allGraphicsGovernorV63") { sawAllGraphicsGovernorV63 = i() != 0; }
            else if (k == "allGraphicsGovernorV66") { sawAllGraphicsGovernorV66 = i() != 0; }
            else if (k == "allGraphicsGovernorV40") { sawAllGraphicsGovernorV40 = i() != 0; }
            else if (k == "allGraphicsGovernorV39") { sawAllGraphicsGovernorV39 = i() != 0; }
            else if (k == "allGraphicsGovernorV37") { sawAllGraphicsGovernorV37 = i() != 0; }
            else if (k == "allGraphicsGovernorV36") { sawAllGraphicsGovernorV36 = i() != 0; }
            else if (k == "allGraphicsGovernorV35") { sawAllGraphicsGovernorV35 = i() != 0; }
            else if (k == "allGraphicsGovernorV34") { /* v34 marker is intentionally not enough for v35 crash-fix migration. */ }
            else if (k.rfind("gear.", 0) == 0)
            {
                for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
                {
                    std::string prefix = std::string("gear.") + kExtraGearDefs[n].key + ".";
                    if (k.rfind(prefix, 0) != 0) { continue; }
                    std::string leaf = k.substr(prefix.size());
                    if (leaf == "enabled") { g_cfg.extraGears[n].enabled = i() != 0; }
                    else if (leaf.rfind("tier", 0) == 0)
                    {
                        int idx = std::stoi(leaf.substr(4));
                        if (idx >= 0 && idx < 5) { g_cfg.extraGears[n].value[static_cast<size_t>(idx)] = i(); }
                    }
                    break;
                }
            }
        }
    }
    catch (...) {}

    if (configVersion >= 67)
    {
        sawAllGraphicsGovernorV35 = true;
        sawAllGraphicsGovernorV36 = true;
        sawAllGraphicsGovernorV37 = true;
        sawAllGraphicsGovernorV39 = true;
        sawAllGraphicsGovernorV40 = true;
        sawAllGraphicsGovernorV41 = true;
        sawAllGraphicsGovernorV42 = true;
        sawAllGraphicsGovernorV43 = true;
        sawAllGraphicsGovernorV44 = true;
        sawAllGraphicsGovernorV45 = true;
        sawAllGraphicsGovernorV47 = true;
        sawAllGraphicsGovernorV48 = true;
        sawAllGraphicsGovernorV50 = true;
        sawAllGraphicsGovernorV51 = true;
        sawAllGraphicsGovernorV52 = true;
        sawAllGraphicsGovernorV56 = true;
        sawAllGraphicsGovernorV57 = true;
        sawAllGraphicsGovernorV58 = true;
        sawAllGraphicsGovernorV59 = true;
        sawAllGraphicsGovernorV60 = true;
        sawAllGraphicsGovernorV63 = true;
        sawAllGraphicsGovernorV66 = true;
    }

    // v35 migration: prior experimental builds saved every extra row disabled and/or live apply off.
    // The user-facing behavior now is "all graphics rows are governed" unless disabled after v35.
    if (!sawAllGraphicsGovernorV35 && !sawAllGraphicsGovernorV36 && !sawAllGraphicsGovernorV37 && !sawAllGraphicsGovernorV39 && !sawAllGraphicsGovernorV40 && !sawAllGraphicsGovernorV41 && !sawAllGraphicsGovernorV42 && !sawAllGraphicsGovernorV43 && !sawAllGraphicsGovernorV44)
    {
        g_cfg.fullGearMatrixEnabled = true;
        g_cfg.extraGearLiveApplyEnabled = true;
        for (auto& row : g_cfg.extraGears) { row.enabled = true; }
    }
    // v37 migration: same-value setter calls are intentional. Some GW2 graphics rows,
    // especially shaders, can have the settings field at the desired value while the
    // renderer/UI side was never refreshed after a previous failed/blocked apply.
    if (!sawAllGraphicsGovernorV37)
    {
        g_cfg.extraGearSkipUnchanged = false;
    }
    // v40 safety migration: unchanged rows should not be force-dirtied on every
    // automatic tier sync. Shadow Custom still gets a targeted repack bundle below.
    if (!sawAllGraphicsGovernorV40)
    {
        g_cfg.extraGearSkipUnchanged = true;
        if (g_cfg.extraGearApplySpacingMs < 250) { g_cfg.extraGearApplySpacingMs = 350; }
    }
    // v41 migration: rows saved by v35-v40 had the unsupported/candidate Hide Ally
    // Visual Effects row enabled. Disable it once for existing configs; users can
    // explicitly re-enable it after v41 if they want to test that row.
    if (!sawAllGraphicsGovernorV41)
    {
        if (kExtraGearDefs.size() > 16) { g_cfg.extraGears[16].enabled = false; }
    }

    // v43 migration: configs written by v35-v42 used Shadows=custom as the
    // highest-tier default. The current crash log points at GW2's packed graphics
    // context after that exact startup apply, so migrate the generated default
    // shadow ladder to native presets. Deliberately different user ladders are left
    // alone.
    if (!sawAllGraphicsGovernorV43)
    {
        auto& shadows = g_cfg.extraGears[7];
        if (shadows.value[0] == 5 && shadows.value[1] == 4 && shadows.value[2] == 3 &&
            shadows.value[3] == 2 && shadows.value[4] == 0)
        {
            shadows.value = { 4, 4, 3, 2, 0 };
        }
    }

    // v44 migration: there are only four visible automatic governor tiers.
    // Experimental v34-v43 configs saved a fifth T4 column and very aggressive
    // default graphics reductions; migrate those test defaults to the stable
    // four-tier layout so auto no longer falls through to "everything lowest".
    if (!sawAllGraphicsGovernorV44)
    {
        ApplyV44FourTierDefaults();
    }

    // v46 migration: GW2's Environment setter only accepts 0/1.  Older
    // configs used the generic low/medium/high row, so clamp the old "high"
    // value 2 to native high 1 instead of repeatedly attempting an invalid
    // write that can poison GsContext packing.
    if (!sawAllGraphicsGovernorV45)
    {
        auto& env = g_cfg.extraGears[2];
        for (int t = 0; t < kGovernorTierCount; ++t)
        {
            int& v = env.value[static_cast<size_t>(t)];
            if (v > 1) { v = 1; }
            if (v < 0) { v = 0; }
        }
    }

    // v47 migration: old configs waited ten seconds before even arming startup
    // profile sync. In practice this looked like "nothing applies" because GW2
    // could serialize the old Low/Lowest profile first, then the addon only changed
    // it much later after login/loading guards. Keep the loading guard, but shorten
    // the saved delay for existing v46-and-older configs.
    if (!sawAllGraphicsGovernorV47)
    {
        if (g_cfg.startupProfileApplyDelaySeconds > 2.0f) { g_cfg.startupProfileApplyDelaySeconds = 2.0f; }
        g_cfg.extraGearSkipUnchanged = false;
    }

    // v48 migration: v44-v47 test builds could save T0 as low values after the
    // graphics matrix/dropdown bugs.  Repair the known-bad tier-zero cases once
    // so "Tier 0" again means best visuals, then keep the user's normal tier
    // range/settings behavior.
    if (!sawAllGraphicsGovernorV48)
    {
        const bool badTier0 =
            g_cfg.tierLimitValues[0] < 4 ||
            g_cfg.tierQualityValues[0] < 4 ||
            g_cfg.extraGears[6].value[0] < 2 ||   // Shaders
            g_cfg.extraGears[10].value[0] < 2 ||  // Textures
            g_cfg.extraGears[11].value[0] < 3;    // Postprocessing
        if (badTier0)
        {
            ApplyV44FourTierDefaults();
        }
        g_cfg.extraGearSkipUnchanged = false;
    }

    // v50 migration: keep direct writes disabled, but enable a setter-only bounce for
    // same-value applies. The recent logs show fields already at T0, so dirty-bit
    // refresh can report success while the native menu/render state still appears stale.
    // Bouncing through a neighboring legal value forces GW2's normal changed-value path
    // and then restores the target.
    if (!sawAllGraphicsGovernorV50)
    {
        g_cfg.sameValueBounceEnabled = true;
        g_cfg.extraGearSkipUnchanged = false;
    }
    // Backend-specific migrations are owned by the private backend now.
    // v57: keep the useful context replay, but retire the repeated alt/target/settle
    // shadow pulse by default. The latest run shows each shadow UI change produced
    // four context replays plus three runtime shadow writes; that can make GW2's
    // graphics context packer unstable even when every individual call reports ok.
    if (!sawAllGraphicsGovernorV57)
    {
    }
    if (!sawAllGraphicsGovernorV58)
    {
    }
    if (!sawAllGraphicsGovernorV59)
    {
    }
    if (!sawAllGraphicsGovernorV60)
    {
    }

    // v39 safety migration: the old direct-write-then-notify model path pre-wrote
    // charModelLimit/charModelQuality before entering GW2's setter. That bypassed
    // GW2's own graphics-context dirty/pack refresh and can leave GsContext with a
    // stale packed representation; opening the native Graphics Options then asserts
    // with "Packing mismatch". Keep all governor changes on GW2's setter paths.
    if (!sawAllGraphicsGovernorV39 || !sawAllGraphicsGovernorV40)
    {
    }


    // v66: production defaults. Diagnostics are opt-in from the debug window.
    if (!sawAllGraphicsGovernorV66)
    {
        g_cfg.tracerEnabled = false;
        g_cfg.autoChangeLogEnabled = false;
    }

    // v26 migration: older configs stored the margins as absolute FPS deltas.
    // Convert them to percentages unless the new percentage keys are already present.
    if (!sawDownMarginPercent && sawLegacyDownDelta && g_cfg.targetFps > 1.0f)
    {
        g_cfg.downMarginPercent = 100.0f * legacyDownDelta / g_cfg.targetFps;
    }
    if (!sawUpMarginPercent && sawLegacyUpDelta && g_cfg.targetFps > 1.0f)
    {
        g_cfg.upMarginPercent = 100.0f * legacyUpDelta / g_cfg.targetFps;
    }
    g_cfg.minTier = std::clamp(g_cfg.minTier, 0, kMaxGovernorTier);
    g_cfg.maxTier = std::clamp(g_cfg.maxTier, 0, kMaxGovernorTier);
    g_cfg.downRepeatSeconds = std::clamp(g_cfg.downRepeatSeconds, 0.1f, 10.0f);
    g_cfg.upProbeSeconds = std::clamp(g_cfg.upProbeSeconds, 0.5f, 30.0f);
    g_cfg.upProbeBackoffSeconds = std::clamp(g_cfg.upProbeBackoffSeconds, 0.0f, 300.0f);
    g_cfg.emergencySeconds = std::clamp(g_cfg.emergencySeconds, 0.1f, 10.0f);
    g_cfg.postLoadingGraceSeconds = std::clamp(g_cfg.postLoadingGraceSeconds, 0.0f, 10.0f);
    g_cfg.extraGearApplySpacingMs = std::clamp(g_cfg.extraGearApplySpacingMs, 50, 5000);
    g_cfg.manualLimitValue = std::clamp(g_cfg.manualLimitValue, 0, 4);
    g_cfg.manualQualityValue = std::clamp(g_cfg.manualQualityValue, 0, 4);
    g_cfg.startTier = std::clamp(g_cfg.startTier, 0, kMaxGovernorTier);
    g_cfg.startupProfileApplyDelaySeconds = std::clamp(g_cfg.startupProfileApplyDelaySeconds, 0.0f, 60.0f);
    g_cfg.governorPreset = std::clamp(g_cfg.governorPreset, 0, 2);
    g_cfg.downMarginPercent = std::clamp(g_cfg.downMarginPercent, 0.0f, 80.0f);
    g_cfg.upMarginPercent = std::clamp(g_cfg.upMarginPercent, 0.0f, 80.0f);
    g_cfg.frameFastAlpha = std::clamp(g_cfg.frameFastAlpha, 0.02f, 0.90f);
    g_cfg.frameSlowAlpha = std::clamp(g_cfg.frameSlowAlpha, 0.005f, 0.40f);
    g_cfg.probeWarmupSeconds = std::clamp(g_cfg.probeWarmupSeconds, 0.0f, 5.0f);
    g_cfg.probeRollbackSeconds = std::clamp(g_cfg.probeRollbackSeconds, 0.05f, 5.0f);
    g_cfg.maxProbeBackoffSeconds = std::clamp(g_cfg.maxProbeBackoffSeconds, 10.0f, 600.0f);
    g_cfg.tierOverlayPosX = std::clamp(g_cfg.tierOverlayPosX, -10000.0f, 10000.0f);
    g_cfg.tierOverlayPosY = std::clamp(g_cfg.tierOverlayPosY, -10000.0f, 10000.0f);
    if (g_cfg.minTier > g_cfg.maxTier) { std::swap(g_cfg.minTier, g_cfg.maxTier); }
    for (int t = 0; t < 5; ++t)
    {
        g_cfg.tierLimitValues[static_cast<size_t>(t)] = std::clamp(g_cfg.tierLimitValues[static_cast<size_t>(t)], 0, 4);
        g_cfg.tierQualityValues[static_cast<size_t>(t)] = std::clamp(g_cfg.tierQualityValues[static_cast<size_t>(t)], 0, 4);
    }
    for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
    {
        const auto& d = kExtraGearDefs[n];
        for (int t = 0; t < 5; ++t)
        {
            g_cfg.extraGears[n].value[static_cast<size_t>(t)] = std::clamp(g_cfg.extraGears[n].value[static_cast<size_t>(t)], d.minValue, d.maxValue);
        }
    }
}

