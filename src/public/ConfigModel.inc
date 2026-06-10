// Public Visual Governor implementation section. Contains UI, config, and governor logic.
// Included by Addon.cpp to keep this refactor source-compatible.

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
    bool tierOverlayVisible = false;        // tiny movable current-tier overlay
    bool controllerEnabled = false;
    bool tracerEnabled = false;
    bool liveStructHookEnabled = true;
    bool setterHookEnabled = true;
    bool useCapturedSetterArgs = true;
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
    bool nexusLoadingGuardEnabled = true;     // v22: Comfy-style loading detection via NexusLinkData::IsGameplay
    int loadingGuardMode = 0;                 // 0=Auto, 1=Off, 2=On for the legacy 60 FPS fallback
    float loadingFpsCenter = 60.0f;
    float loadingFpsWindow = 2.5f;
    float loadingDetectSeconds = 0.5f;
    float postLoadingGraceSeconds = 2.5f;
    int minTier = 0;
    int maxTier = kMaxGovernorTier;
    int startTier = 0;
    bool autoChangeLogEnabled = false;        // off by default; enable from Debug when diagnosing
    bool startupProfileApplyEnabled = true;   // v30: one-time sync after startup so GW2 and governor begin from the same tier
    float startupProfileApplyDelaySeconds = 2.0f;
    int governorPreset = 1;                    // 0=Swift, 1=Balanced, 2=Calm
    bool frameTimeGovernor = true;             // v24: DRS-style frame-time gearbox instead of raw FPS switching
    float frameFastAlpha = 0.28f;              // fast frame-time EMA
    float frameSlowAlpha = 0.055f;             // slow frame-time EMA
    float probeWarmupSeconds = 1.1f;           // ignore immediate apply/rebuild turbulence during an upshift probe
    float probeRollbackSeconds = 1.25f;        // sustained bad time required before rolling back a probe
    float maxProbeBackoffSeconds = 160.0f;     // exponential failed-probe backoff cap
    std::array<int, kStoredTierSlots> tierLimitValues{ 4, 3, 2, 1, 0 };
    std::array<int, kStoredTierSlots> tierQualityValues{ 4, 3, 2, 1, 0 };
    bool fullGearMatrixEnabled = true;
    bool extraGearLiveApplyEnabled = true;    // v35: full graphics matrix live-apply is enabled; individual rows can still be disabled.
    int extraGearApplySpacingMs = 350;        // v36: spread renderer-affecting graphics setters across frames; do not hammer GW2's GsContext packer.
    bool extraGearSkipUnchanged = false;      // v44: manual/UI applies force rows; default off avoids stale renderer state when raw fields already match.
    // v51: the per-option GW2 setters mutate the Graphics Options backing object,
    // but several non-character rows can still leave the packed renderer context stale.
    // Commit the full packed graphics context after each extra row so the rendered state
    // is rebuilt from the same object the native Options UI displays.
    // v52: the v51/v51a packed-context call is now hard-disabled because the
    // traced build faults inside GW2's context packer before any renderer rebuild.
    bool modelRendererFallbackEnabled = true; // if a model setter writes the field but SEH-aborts before the renderer call, run the renderer-side call directly.
    bool extraGearRendererFallbackEnabled = true; // same fallback for non-character graphics rows.
    bool shadowContextHookEnabled = true;      // v56: capture GW2's native shadow render-context object when map/context rebuilds run.
    bool shadowContextRefreshEnabled = true;   // v57: one safe replay after shadow preset changes; no multi-pulse replay storm.
    bool shadowContextManualRowOnly = false;   // v60: diagnostic escape hatch only. Auto shadow preset replay is enabled; context lifetime is handled by ctor/dtor hooks, not loading toggles.
    std::array<ExtraGearConfig, kExtraGearSettingCount> extraGears{};
} g_cfg;

// Native GW2 addresses and settings-object field positions are resolved at runtime.
// This source intentionally does not keep build-specific function RVAs.  If a
// signature cannot be found in the loaded client image, the matching feature is
// disabled instead of calling a stale address.
constexpr int ModelSettingLimitNotifyBit = 0x04;
constexpr int ModelSettingQualityNotifyBit = 0x08;

struct Gw2RvaTable
{
    uint64_t settingsSerialize = 0;
    uint64_t setCharModelLimit = 0;
    uint64_t setCharModelQuality = 0;

    uint64_t setAnimation = 0;
    uint64_t setAntiAliasing = 0;
    uint64_t setHideAllyEffectsCandidate = 0;
    uint64_t setEnvironment = 0;
    uint64_t setBoolFlag = 0;
    uint64_t setLodDistance = 0;
    uint64_t setPostProcessing = 0;
    uint64_t setReflections = 0;
    uint64_t setRenderSampling = 0;
    uint64_t setShaders = 0;
    uint64_t setShadowsPreset = 0;
    uint64_t setShadowResolution = 0;
    uint64_t setShadowCascadeCount = 0;
    uint64_t setTextureDetail = 0;

    uint64_t modelRendererSingleton = 0;
    uint64_t runtimeOptionBool = 0;
    uint64_t runtimeOptionInt3 = 0;
    uint64_t runtimeShaderMode = 0;
    uint64_t runtimeGraphicsInt = 0;
    uint64_t runtimeShadowPreset = 0;
    uint64_t runtimeEnvironmentScale = 0;
    uint64_t runtimeAntiAliasing = 0;
    uint64_t runtimePostProcessing = 0;
    uint64_t runtimeRenderSampling = 0;

    uint64_t shadowContextCtor = 0;
    uint64_t shadowContextDestructor = 0;
    uint64_t shadowContextApplyGlobals = 0;
    uint64_t shadowContextVtable0 = 0;
    uint64_t shadowContextVtable1 = 0;
    bool resolved = false;
};

Gw2RvaTable g_rva{};

struct Gw2FieldTable
{
    uint32_t charModelLimit = 0;
    uint32_t charModelQuality = 0;
    uint32_t modelSettingFlags = 0;
    uint32_t boolSettingFlags = 0;

    uint32_t animation = 0;
    uint32_t antiAliasing = 0;
    uint32_t hideAllyEffectsCandidate = 0;
    uint32_t environment = 0;
    uint32_t flags = 0;
    uint32_t lodDistance = 0;
    uint32_t postProcessing = 0;
    uint32_t reflections = 0;
    uint32_t renderSampling = 0;
    uint32_t shaders = 0;
    uint32_t shadowsPreset = 0;
    uint32_t shadowResolution = 0;
    uint32_t shadowCascadeCount = 0;
    uint32_t textureDetail = 0;

    uint32_t modelRendererLimitMethod = 0;
    uint32_t modelRendererQualityMethod = 0;
    bool resolved = false;
};

Gw2FieldTable g_field{};

enum class ExtraGearKind { EnumSetting, BoolFlag };
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
    bool experimental;
};


typedef int (__fastcall* SettingsSerialize_t)(void* settingsStruct);
typedef void (__fastcall* ModelSettingSetter2_t)(void* settingsStruct, int value);
typedef void (__fastcall* ModelSettingSetter4_t)(void* settingsStruct, int value, uintptr_t a3, uintptr_t a4);
typedef void (__fastcall* ExtraEnumSetter_t)(void* settingsStruct, int value);
typedef void (__fastcall* ExtraBoolSetter_t)(void* settingsStruct, int flagBit, int enabled);
typedef void* (__fastcall* NoArgPtrFn_t)();
typedef void (__fastcall* RuntimeVoidInt_t)(int a);
typedef void (__fastcall* RuntimeVoidInt2_t)(int a, int b);
typedef void (__fastcall* RuntimeVoidInt3_t)(int a, int b, int c);
typedef void (__fastcall* RuntimeVoidFloat_t)(float a);
typedef void (__fastcall* RendererModelMethod_t)(void* renderer, int value);
typedef void* (__fastcall* ShadowContextCtor_t)(void* shadowContext, void* ownerOrMap, int skipNativeApply);
typedef void* (__fastcall* ShadowContextDestructor_t)(void* shadowContext);
typedef void (__fastcall* ShadowContextApplyGlobals_t)(void* shadowContext);

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
    { "hideAllyEffects", "Hide Ally Visual Effects candidate", ExtraGearKind::EnumSetting, 0, 4, -1, kHideAllyCandidateNames, 5, true },
} };


uint64_t ResolvedExtraGearSetterOffset(const ExtraGearDef& d)
{
    if (std::strcmp(d.key, "animation") == 0) { return g_rva.setAnimation; }
    if (std::strcmp(d.key, "antiAliasing") == 0) { return g_rva.setAntiAliasing; }
    if (std::strcmp(d.key, "environment") == 0) { return g_rva.setEnvironment; }
    if (std::strcmp(d.key, "lodDistance") == 0) { return g_rva.setLodDistance; }
    if (std::strcmp(d.key, "reflections") == 0) { return g_rva.setReflections; }
    if (std::strcmp(d.key, "renderSampling") == 0) { return g_rva.setRenderSampling; }
    if (std::strcmp(d.key, "shaders") == 0) { return g_rva.setShaders; }
    if (std::strcmp(d.key, "shadows") == 0) { return g_rva.setShadowsPreset; }
    if (std::strcmp(d.key, "shadowResolution") == 0) { return g_rva.setShadowResolution; }
    if (std::strcmp(d.key, "shadowCascades") == 0) { return g_rva.setShadowCascadeCount; }
    if (std::strcmp(d.key, "textures") == 0) { return g_rva.setTextureDetail; }
    if (std::strcmp(d.key, "postProcessing") == 0) { return g_rva.setPostProcessing; }
    if (std::strcmp(d.key, "hideAllyEffects") == 0) { return g_rva.setHideAllyEffectsCandidate; }
    if (d.kind == ExtraGearKind::BoolFlag) { return g_rva.setBoolFlag; }
    return 0;
}

uint32_t ResolvedExtraGearFieldOffset(const ExtraGearDef& d)
{
    if (std::strcmp(d.key, "animation") == 0) { return g_field.animation; }
    if (std::strcmp(d.key, "antiAliasing") == 0) { return g_field.antiAliasing; }
    if (std::strcmp(d.key, "environment") == 0) { return g_field.environment; }
    if (std::strcmp(d.key, "lodDistance") == 0) { return g_field.lodDistance; }
    if (std::strcmp(d.key, "reflections") == 0) { return g_field.reflections; }
    if (std::strcmp(d.key, "renderSampling") == 0) { return g_field.renderSampling; }
    if (std::strcmp(d.key, "shaders") == 0) { return g_field.shaders; }
    if (std::strcmp(d.key, "shadows") == 0) { return g_field.shadowsPreset; }
    if (std::strcmp(d.key, "shadowResolution") == 0) { return g_field.shadowResolution; }
    if (std::strcmp(d.key, "shadowCascades") == 0) { return g_field.shadowCascadeCount; }
    if (std::strcmp(d.key, "textures") == 0) { return g_field.textureDetail; }
    if (std::strcmp(d.key, "postProcessing") == 0) { return g_field.postProcessing; }
    if (std::strcmp(d.key, "hideAllyEffects") == 0) { return g_field.hideAllyEffectsCandidate; }
    if (d.kind == ExtraGearKind::BoolFlag) { return g_field.flags; }
    return 0;
}

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
        c.value[4] = c.value[3]; // legacy hidden slot; not shown/applied in v44.
        c.enabled = true;
    }
    // v35: all official graphics rows are part of the automatic governor by default.
    // v41: keep the unsupported/candidate Hide Ally Visual Effects row opt-in. It is
    // a real setter entry, but it is not part of the normal visible graphics-options
    // pack in all clients and is not needed for "all official graphics settings".
    // Users can still enable it manually from the table when testing that row.

    // v44: four visible tiers only: T0 best visuals, T3 lowest automatic tier.
    // Keep texture quality high by default; texture detail is mostly VRAM/streaming
    // pressure, so dropping it makes the game look broken without helping frame time much.
    g_cfg.extraGears[0].value  = { 2, 1, 1, 0, 0 }; // Animation: high, medium, medium, low.
    g_cfg.extraGears[1].value  = { 3, 2, 1, 1, 1 }; // Antialiasing: SMAA high, SMAA low, FXAA, FXAA.
    g_cfg.extraGears[2].value  = { 1, 1, 1, 0, 0 }; // Environment: GW2 native setter is 0/1 only (low/high).
    g_cfg.extraGears[3].value  = { 3, 2, 1, 1, 1 }; // LOD distance.
    g_cfg.extraGears[4].value  = { 2, 1, 1, 0, 0 }; // Reflections.
    g_cfg.extraGears[5].value  = { 2, 1, 1, 1, 1 }; // Render sampling: do not default to subsample.
    g_cfg.extraGears[6].value  = { 2, 2, 1, 0, 0 }; // Shaders.
    g_cfg.extraGears[7].value  = { 4, 3, 2, 1, 1 }; // Shadows: ultra, high, medium, low.
    g_cfg.extraGears[8].value  = { 3, 2, 1, 1, 1 }; // Shadow resolution, only used with custom.
    g_cfg.extraGears[9].value  = { 3, 2, 1, 1, 1 }; // Shadow cascades, only used with custom.
    g_cfg.extraGears[10].value = { 2, 2, 2, 2, 2 }; // Textures stay high by default.
    g_cfg.extraGears[11].value = { 3, 2, 1, 1, 1 }; // Postprocessing.
    g_cfg.extraGears[12].value = { 1, 1, 1, 1, 1 }; // Best Texture Filtering stays on.
    g_cfg.extraGears[13].value = { 1, 1, 1, 1, 1 }; // Effect LOD stays on by default.
    g_cfg.extraGears[14].value = { 1, 1, 1, 0, 0 }; // High-res characters.
    g_cfg.extraGears[15].value = { 1, 1, 1, 1, 1 }; // Vertical Sync row retained, but not governed by default.
    g_cfg.extraGears[16].value = { 1, 1, 0, 0, 0 }; // Screen-space shadows.
    g_cfg.extraGears[15].enabled = false;            // Do not let the governor fight the user's presentation/cap setting.
    g_cfg.extraGears[17].enabled = false;            // Hide Ally Effects candidate: opt-in only.
}

void ApplyV44FourTierDefaults()
{
    g_cfg.minTier = 0;
    g_cfg.maxTier = kMaxGovernorTier;
    g_cfg.startTier = std::clamp(g_cfg.startTier, 0, kMaxGovernorTier);
    g_cfg.tierLimitValues = { 4, 3, 2, 1, 0 };
    g_cfg.tierQualityValues = { 4, 3, 2, 1, 0 };
    InitExtraGearDefaults();
    g_cfg.extraGearSkipUnchanged = false;
    if (g_cfg.extraGearApplySpacingMs < 350) { g_cfg.extraGearApplySpacingMs = 350; }
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
    float loadingTimer = 0.0f;
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
    bool hooksInstalled = false;
    bool settingsStructHookInstalled = false;
    bool setterHooksInstalled = false;
    bool shadowContextHookInstalled = false;
    bool shadowContextCtorHookInstalled = false;
    bool shadowContextDtorHookInstalled = false;
    std::atomic<uintptr_t> shadowContextPtr{0};
    std::atomic<uint64_t> shadowContextCaptures{0};
    std::atomic<uint64_t> shadowContextRefreshCalls{0};
    std::atomic<uint64_t> limitSetterCalls{0};
    std::atomic<uint64_t> qualitySetterCalls{0};
    std::atomic<uint64_t> ownSetterCalls{0};
    std::atomic<uintptr_t> lastLimitA3{0};
    std::atomic<uintptr_t> lastLimitA4{0};
    std::atomic<uintptr_t> lastQualityA3{0};
    std::atomic<uintptr_t> lastQualityA4{0};
    std::atomic<int> lastLimitSetterValue{-1};
    std::atomic<int> lastQualitySetterValue{-1};
    std::atomic<uintptr_t> settingsStructPtr{0};
    std::atomic<int> settingsStructConfidence{0}; // 0=none, 1=memory scan, 2=serialize hook, 3=setter hook
    std::atomic<int> observedLimit{-1};
    std::atomic<int> observedQuality{-1};
    uint64_t settingsStructHits = 0;
    std::atomic<bool> guardArmed{false};
    std::atomic<uint64_t> guardHits{0};
    std::atomic<uint64_t> guardPageHits{0};
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
        out << "configVersion=69\n";
        out << "windowVisible=" << (g_cfg.windowVisible ? 1 : 0) << "\n";
        out << "tierOverlayVisible=" << (g_cfg.tierOverlayVisible ? 1 : 0) << "\n";
        out << "controllerEnabled=" << (g_cfg.controllerEnabled ? 1 : 0) << "\n";
        out << "tracerEnabled=" << (g_cfg.tracerEnabled ? 1 : 0) << "\n";
        out << "liveStructHookEnabled=" << (g_cfg.liveStructHookEnabled ? 1 : 0) << "\n";
        out << "setterHookEnabled=" << (g_cfg.setterHookEnabled ? 1 : 0) << "\n";
        out << "useCapturedSetterArgs=" << (g_cfg.useCapturedSetterArgs ? 1 : 0) << "\n";
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
        out << "loadingGuardMode=" << g_cfg.loadingGuardMode << "\n";
        out << "loadingFpsCenter=" << g_cfg.loadingFpsCenter << "\n";
        out << "loadingFpsWindow=" << g_cfg.loadingFpsWindow << "\n";
        out << "loadingDetectSeconds=" << g_cfg.loadingDetectSeconds << "\n";
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
        out << "modelRendererFallbackEnabled=" << (g_cfg.modelRendererFallbackEnabled ? 1 : 0) << "\n";
        out << "extraGearRendererFallbackEnabled=" << (g_cfg.extraGearRendererFallbackEnabled ? 1 : 0) << "\n";
        out << "shadowContextHookEnabled=" << (g_cfg.shadowContextHookEnabled ? 1 : 0) << "\n";
        out << "shadowContextRefreshEnabled=" << (g_cfg.shadowContextRefreshEnabled ? 1 : 0) << "\n";
        out << "shadowContextManualRowOnly=" << (g_cfg.shadowContextManualRowOnly ? 1 : 0) << "\n";
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
            else if (k == "controllerEnabled") { g_cfg.controllerEnabled = i() != 0; }
            else if (k == "tracerEnabled") { g_cfg.tracerEnabled = i() != 0; }
            else if (k == "liveStructHookEnabled") { g_cfg.liveStructHookEnabled = i() != 0; }
            else if (k == "setterHookEnabled") { g_cfg.setterHookEnabled = i() != 0; }
            else if (k == "useCapturedSetterArgs") { g_cfg.useCapturedSetterArgs = i() != 0; }
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
            else if (k == "loadingGuardMode") { g_cfg.loadingGuardMode = i(); }
            else if (k == "loadingFpsCenter") { g_cfg.loadingFpsCenter = f(); }
            else if (k == "loadingFpsWindow") { g_cfg.loadingFpsWindow = f(); }
            else if (k == "loadingDetectSeconds") { g_cfg.loadingDetectSeconds = f(); }
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
            else if (k == "modelRendererFallbackEnabled") { g_cfg.modelRendererFallbackEnabled = i() != 0; }
            else if (k == "extraGearRendererFallbackEnabled") { g_cfg.extraGearRendererFallbackEnabled = i() != 0; }
            else if (k == "shadowContextHookEnabled") { g_cfg.shadowContextHookEnabled = i() != 0; }
            else if (k == "shadowContextRefreshEnabled") { g_cfg.shadowContextRefreshEnabled = i() != 0; }
            else if (k == "shadowContextManualRowOnly") { g_cfg.shadowContextManualRowOnly = i() != 0; }
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
        g_cfg.extraGears[17].enabled = false;
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

    // v51 attempted a full packed GsContext commit. v52 disables that path and
    // replaces it with renderer-tail fallbacks because the traced packed call
    // faults before committing any rendered state. Run this once even for users
    // who already saved a v51/v51a config.
    if (!sawAllGraphicsGovernorV52)
    {
        g_cfg.modelRendererFallbackEnabled = true;
        g_cfg.extraGearRendererFallbackEnabled = true;
    }

    // v56: shadows are special. GW2's runtime shadow-preset setter only updates
    // global shadow knobs; the active shadow context does not consume them again
    // until a map/context rebuild. Capture that native context rebuild and replay it
    // after live shadow changes.
    if (!sawAllGraphicsGovernorV56)
    {
        g_cfg.shadowContextHookEnabled = true;
        g_cfg.shadowContextRefreshEnabled = true;
    }
    // v57: keep the useful context replay, but retire the repeated alt/target/settle
    // shadow pulse by default. The latest run shows each shadow UI change produced
    // four context replays plus three runtime shadow writes; that can make GW2's
    // graphics context packer unstable even when every individual call reports ok.
    if (!sawAllGraphicsGovernorV57)
    {
    }

    // v58: quarantined replay to manual-only while we proved map-load safety.
    // v59: EXE inspection found the missing real-time case: when a map is built
    // with shadows disabled, GW2 constructs the shadow context with the 0x80 flag
    // and skips ApplyGlobals ApplyGlobals, so the v56/v57 hook never captured a
    // context. v59 hooks the constructor too, keeps safe replay gates, and enables auto shadow preset replay again.
    if (!sawAllGraphicsGovernorV58)
    {
    }
    if (!sawAllGraphicsGovernorV59)
    {
        g_cfg.shadowContextHookEnabled = true;
        g_cfg.shadowContextRefreshEnabled = true;
        g_cfg.shadowContextManualRowOnly = false;
    }

    // v60: do not clear a good shadow context on every Nexus loading toggle.
    // The latest trace had a second loading-enter with no follow-up shadow ctor/apply
    // capture, so v59 threw away the valid context and auto shadows stopped replaying.
    // v60 keeps the pointer across loading toggles and invalidates it only when GW2's
    // actual shadow-context destructor runs.
    if (!sawAllGraphicsGovernorV60)
    {
        g_cfg.shadowContextHookEnabled = true;
        g_cfg.shadowContextRefreshEnabled = true;
        g_cfg.shadowContextManualRowOnly = false;
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
    g_cfg.loadingGuardMode = std::clamp(g_cfg.loadingGuardMode, 0, 2);
    g_cfg.downRepeatSeconds = std::clamp(g_cfg.downRepeatSeconds, 0.1f, 10.0f);
    g_cfg.upProbeSeconds = std::clamp(g_cfg.upProbeSeconds, 0.5f, 30.0f);
    g_cfg.upProbeBackoffSeconds = std::clamp(g_cfg.upProbeBackoffSeconds, 0.0f, 300.0f);
    g_cfg.emergencySeconds = std::clamp(g_cfg.emergencySeconds, 0.1f, 10.0f);
    g_cfg.loadingDetectSeconds = std::clamp(g_cfg.loadingDetectSeconds, 0.1f, 5.0f);
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

