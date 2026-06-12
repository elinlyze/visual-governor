// Public Visual Governor implementation section. Contains UI, config, and governor logic.
// Included by Addon.cpp to keep this refactor source-compatible.

const char* GovernorTierName(int tier)
{
    static const char* kNames[] = { "Ultra", "High", "Medium", "Low" };
    tier = std::clamp(tier, 0, kMaxGovernorTier);
    return kNames[tier];
}

bool GovernorTierCombo(const char* label, int* value)
{
    int current = std::clamp(*value, 0, kMaxGovernorTier);
    bool changed = false;
    if (ImGui::BeginCombo(label, GovernorTierName(current)))
    {
        for (int t = 0; t < kGovernorTierCount; ++t)
        {
            const bool selected = (t == current);
            if (ImGui::Selectable(GovernorTierName(t), selected))
            {
                *value = t;
                changed = true;
            }
            if (selected) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void DrawTierPolicy()
{
    for (int i = 0; i < kGovernorTierCount; ++i)
    {
        int lv = GovernorTierToLimitValue(i);
        int qv = GovernorTierToQualityValue(i);
        ImGui::BulletText("%s%s: Limit %s, Quality %s", GovernorTierName(i), i == g_state.currentTier ? " (current)" : "", ModelValueName(lv), ModelValueName(qv));
    }
}

bool IsVsyncGearRow(const ExtraGearDef& d)
{
    return std::strcmp(d.key, "verticalSync") == 0;
}

void DrawSectionLabel(const char* text)
{
    ImGui::TextUnformatted(text);
    ImGui::Separator();
}

void DrawGearCellLiveApply(size_t row, int tier)
{
    int applyTier = ClampTierToConfiguredRange(g_state.currentTier);
    if (tier == applyTier && g_cfg.controllerEnabled && g_cfg.fullGearMatrixEnabled && g_cfg.extraGearLiveApplyEnabled)
    {
        QueueExtraGearRow(row, applyTier, "manual-ui-row-change", true);
    }
}

void DrawGraphicsTierMatrix()
{
    static const char* modelNames[] = { "Lowest", "Low", "Medium", "High", "Highest" };

    ImGui::Checkbox("Enable graphics tier matrix", &g_cfg.fullGearMatrixEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Live apply edited rows", &g_cfg.extraGearLiveApplyEnabled);
    ImGui::Checkbox("Force same-value applies through neighbor value", &g_cfg.sameValueBounceEnabled);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Recommended. Bounces a row through a nearby legal value so GW2 runs the real changed-value path even when the stored field already equals the target."); }
    ImGui::Checkbox("Skip unchanged rows during automatic applies", &g_cfg.extraGearSkipUnchanged);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Usually leave off while validating renderer refresh. Some rows need same-value applies to refresh the live renderer."); }
    ImGui::DragInt("Graphics row spacing ms", &g_cfg.extraGearApplySpacingMs, 10.0f, 50, 5000);

    if (ImGui::Button("Apply full current tier now")) { ForceApplyCurrentTier("manual-full-tier-apply", true); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel graphics queue"))
    {
        g_state.extraGearQueueActive = false;
        g_state.extraGearPendingActive = false;
        g_state.status = "Graphics queue canceled.";
        TraceLine("ExtraGear queue canceled by user.");
    }

    if (g_state.extraGearQueueActive)
    {
        ImGui::Text("Graphics queue: %s row %u/%u", GovernorTierName(g_state.extraGearQueueTier), static_cast<unsigned>(g_state.extraGearQueueIndex), static_cast<unsigned>(kExtraGearDefs.size()));
        if (!g_state.extraGearQueueWaitReason.empty())
        {
            ImGui::SameLine();
            ImGui::Text("waiting: %s %.1fs", g_state.extraGearQueueWaitReason.c_str(), g_state.extraGearQueueBlockedSeconds);
        }
    }

    ImGui::TextWrapped("Ultra is highest visuals; Low is the lowest automatic profile. Vertical Sync is intentionally hidden from this matrix.");

    if (ImGui::BeginTable("graphicsTierMatrix", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch);
        for (int t = 0; t < kGovernorTierCount; ++t)
        {
            char label[24];
            std::snprintf(label, sizeof(label), "%s%s", GovernorTierName(t), t == g_state.currentTier ? "*" : "");
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, 112.0f);
        }
        ImGui::TableHeadersRow();

        auto drawModelRow = [&](const char* label, std::array<int, kStoredTierSlots>& values, bool& enabled)
        {
            ImGui::PushID(label);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##use", &enabled);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Let the automatic governor control this model setting."); }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(label);
            for (int t = 0; t < kGovernorTierCount; ++t)
            {
                ImGui::TableSetColumnIndex(t + 2);
                ImGui::SetNextItemWidth(104.0f);
                char comboId[64];
                std::snprintf(comboId, sizeof(comboId), "##%s_tier%d", label, t);
                if (ImGui::Combo(comboId, &values[static_cast<size_t>(t)], modelNames, IM_ARRAYSIZE(modelNames)))
                {
                    int applyTier = ClampTierToConfiguredRange(g_state.currentTier);
                    if (t == applyTier && g_cfg.controllerEnabled)
                    {
                        ApplyLiveModelValues(GovernorTierToLimitValue(applyTier), GovernorTierToQualityValue(applyTier), "manual-ui-tier-mapping-change");
                    }
                }
            }
            ImGui::PopID();
        };

        drawModelRow("Character Model Limit", g_cfg.tierLimitValues, g_cfg.controlLimitLive);
        drawModelRow("Character Model Quality", g_cfg.tierQualityValues, g_cfg.controlQualityLive);

        for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
        {
            const auto& d = kExtraGearDefs[n];
            if (IsVsyncGearRow(d)) { continue; }
            auto& c = g_cfg.extraGears[n];
            ImGui::PushID(static_cast<int>(n));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##use", &c.enabled);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(d.label);
            if (d.experimental)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Experimental/candidate row. Test this row by itself before enabling it for automatic tier changes.");
                }
            }
            for (int t = 0; t < kGovernorTierCount; ++t)
            {
                ImGui::TableSetColumnIndex(t + 2);
                ImGui::SetNextItemWidth(104.0f);
                char comboId[64];
                std::snprintf(comboId, sizeof(comboId), "##gear_row%u_tier%d", static_cast<unsigned>(n), t);
                if (StableEnumCombo(comboId, &c.value[static_cast<size_t>(t)], d.names, d.nameCount))
                {
                    DrawGearCellLiveApply(n, t);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Enable all visible graphics rows"))
    {
        for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
        {
            if (!IsVsyncGearRow(kExtraGearDefs[n])) { g_cfg.extraGears[n].enabled = true; }
        }
        g_cfg.fullGearMatrixEnabled = true;
        g_cfg.extraGearLiveApplyEnabled = true;
        g_state.status = "All visible graphics gear rows enabled.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable all visible graphics rows"))
    {
        for (size_t n = 0; n < kExtraGearDefs.size(); ++n)
        {
            if (!IsVsyncGearRow(kExtraGearDefs[n])) { g_cfg.extraGears[n].enabled = false; }
        }
        g_state.status = "All visible graphics gear rows disabled.";
    }
}

void DrawTierRangeControls()
{
    int oldMinTier = g_cfg.minTier;
    int oldMaxTier = g_cfg.maxTier;
    bool minChanged = GovernorTierCombo("Best automatic profile", &g_cfg.minTier);
    bool maxChanged = GovernorTierCombo("Lowest automatic profile", &g_cfg.maxTier);
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Ultra = best visuals. Low = most performance saving."); }
    if (g_cfg.minTier > g_cfg.maxTier) { std::swap(g_cfg.minTier, g_cfg.maxTier); }
    if (g_cfg.controllerEnabled && (minChanged || maxChanged || oldMinTier != g_cfg.minTier || oldMaxTier != g_cfg.maxTier))
    {
        int clampedCurrent = std::clamp(g_state.currentTier, g_cfg.minTier, g_cfg.maxTier);
        g_state.currentTier = clampedCurrent;
        ResetAutoDecisionTimers(false);
        ForceApplyCurrentTier("manual-tier-range-change", true);
    }
    ImGui::Text("Active range: %s .. %s", GovernorTierName(g_cfg.minTier), GovernorTierName(g_cfg.maxTier));
    DrawTierPolicy();
}

void DrawGovernorTuningControls()
{
    bool wasControllerEnabled = g_cfg.controllerEnabled;
    if (ImGui::Checkbox("Enable automatic governor", &g_cfg.controllerEnabled))
    {
        if (g_cfg.controllerEnabled && !wasControllerEnabled)
        {
            ResetAutoDecisionTimers(false);
            g_state.startupProfileApplyDone = false;
            g_state.startupProfileApplyWaiting = false;
            g_state.startupProfileApplyTimer = g_cfg.startupProfileApplyDelaySeconds;
            ForceApplyCurrentTier("manual-auto-enabled-sync", true);
        }
    }

    ImGui::Text("Preset: %s", GovernorPresetName(g_cfg.governorPreset));
    if (ImGui::Button("Swift preset")) { ApplyGovernorPreset(0); }
    ImGui::SameLine();
    if (ImGui::Button("Balanced preset")) { ApplyGovernorPreset(1); }
    ImGui::SameLine();
    if (ImGui::Button("Calm preset")) { ApplyGovernorPreset(2); }

    ImGui::DragFloat("Target FPS", &g_cfg.targetFps, 1.0f, 30.0f, 500.0f, "%.0f");
    ImGui::DragFloat("Emergency minimum FPS", &g_cfg.minFps, 1.0f, 10.0f, 500.0f, "%.0f");
    if (EffectiveMinimumFps() < g_cfg.minFps)
    {
        ImGui::TextDisabled("Effective emergency minimum is %.0f FPS because it must be below target.", EffectiveMinimumFps());
    }
    ImGui::DragFloat("Downshift margin (% of target FPS)", &g_cfg.downMarginPercent, 0.1f, 0.0f, 80.0f, "%.1f%%");
    ImGui::DragFloat("Upshift probe margin (% of target FPS)", &g_cfg.upMarginPercent, 0.1f, 0.0f, 80.0f, "%.1f%%");
    ImGui::Text("Budgets: down at ~%.1f FPS, probe-stable at ~%.1f FPS", g_cfg.targetFps - DownMarginFps(), g_cfg.targetFps - UpMarginFps());

    ImGui::DragFloat("First downshift hold seconds", &g_cfg.downSeconds, 0.1f, 0.1f, 30.0f, "%.1f");
    ImGui::DragFloat("Repeat downshift hold seconds", &g_cfg.downRepeatSeconds, 0.1f, 0.1f, 10.0f, "%.1f");
    ImGui::DragFloat("Stable time before upshift probe", &g_cfg.upSeconds, 0.5f, 1.0f, 120.0f, "%.1f");
    ImGui::DragFloat("Upshift probe duration", &g_cfg.upProbeSeconds, 0.5f, 0.5f, 30.0f, "%.1f");
    ImGui::DragFloat("Probe backoff after failed probe", &g_cfg.upProbeBackoffSeconds, 1.0f, 0.0f, 300.0f, "%.0f");
    ImGui::DragFloat("Max probe backoff seconds", &g_cfg.maxProbeBackoffSeconds, 5.0f, 10.0f, 600.0f, "%.0f");
    ImGui::DragFloat("Probe warmup seconds", &g_cfg.probeWarmupSeconds, 0.1f, 0.0f, 5.0f, "%.1f");
    ImGui::DragFloat("Probe rollback bad seconds", &g_cfg.probeRollbackSeconds, 0.1f, 0.05f, 5.0f, "%.2f");
    ImGui::DragFloat("Apply spacing / cooldown seconds", &g_cfg.cooldownSeconds, 0.1f, 0.0f, 10.0f, "%.1f");
    ImGui::DragFloat("Emergency hold seconds", &g_cfg.emergencySeconds, 0.1f, 0.1f, 10.0f, "%.1f");
    float dropReactionPct = g_cfg.frameFastAlpha * 100.0f;
    if (ImGui::DragFloat("FPS Drop Reaction Smoothing", &dropReactionPct, 0.5f, 2.0f, 90.0f, "%.1f%%"))
    {
        g_cfg.frameFastAlpha = std::clamp(dropReactionPct / 100.0f, 0.02f, 0.90f);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Higher = reacts faster to FPS drops. Lower = smoother, ignores more tiny spikes."); }
    float recoveryPct = g_cfg.frameSlowAlpha * 100.0f;
    if (ImGui::DragFloat("FPS Recovery Smoothing", &recoveryPct, 0.1f, 0.5f, 40.0f, "%.1f%%"))
    {
        g_cfg.frameSlowAlpha = std::clamp(recoveryPct / 100.0f, 0.005f, 0.40f);
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Higher = trusts recovery sooner. Lower = waits for longer stable FPS before recovering visuals."); }

    ImGui::Checkbox("Nexus gameplay loading guard", &g_cfg.nexusLoadingGuardEnabled);
    ImGui::TextDisabled("Loading detection is Nexus IsGameplay only. Legacy 60 FPS fallback removed in v82.");
    ImGui::DragFloat("Post-loading grace seconds", &g_cfg.postLoadingGraceSeconds, 0.1f, 0.0f, 10.0f, "%.1f");

    ImGui::Checkbox("Startup sync current profile after delay", &g_cfg.startupProfileApplyEnabled);
    ImGui::DragFloat("Startup sync delay seconds", &g_cfg.startupProfileApplyDelaySeconds, 0.5f, 0.0f, 60.0f, "%.1f");

    if (ImGui::Button("Reset auto timers"))
    {
        ResetAutoDecisionTimers(false);
        g_state.postLoadingGrace = 0.0f;
        g_state.inLoadingGuard = false;
        g_state.autoState = "reset";
        g_state.status = "Auto governor timers reset.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset frame samples"))
    {
        ResetFrameStats(g_state.frameMs > 0.1f ? g_state.frameMs : TargetFrameMs());
        g_state.status = "Frame-time samples reset.";
    }
}

void DrawDebugStatsContent()
{
    VisualBackendStatus backend = GetVisualBackendStatus();
    int observedLimit = backend.observedLimit;
    int observedQuality = backend.observedQuality;

    DrawSectionLabel("Runtime");
    ImGui::Text("Profile %s  -> Limit %s / Quality %s", GovernorTierName(g_state.currentTier), ModelValueName(GovernorTierToLimitValue(g_state.currentTier)), ModelValueName(GovernorTierToQualityValue(g_state.currentTier)));
    ImGui::Text("FPS %.1f  EMA %.1f  frame ms now %.2f fast %.2f slow %.2f p95 %.2f", g_state.uiFps, g_state.emaFps, g_state.frameMs, g_state.fastFrameMs, g_state.slowFrameMs, g_state.p95FrameMs);
    ImGui::Text("Observed: Limit %s  Quality %s", ModelValueName(observedLimit), ModelValueName(observedQuality));
    ImGui::TextWrapped("Status: %s", g_state.status.c_str());

    DrawSectionLabel("Governor state");
    ImGui::Text("Auto state: %s", g_state.autoState.c_str());
    ImGui::Text("Nexus gameplay: %s | Nexus loading: %s", g_state.nexusTimingInitialized ? (g_state.nexusWasGameplay ? "yes" : "no") : "unknown", g_state.nexusInLoading ? "yes" : "no");
    ImGui::Text("Down timer %.2fs / %.2fs%s", g_state.lowTimer, g_state.hadDownshiftSeries ? g_cfg.downRepeatSeconds : g_cfg.downSeconds, g_state.hadDownshiftSeries ? " repeat" : " first");
    ImGui::Text("Stable credit %.2fs / %.2fs (gain %.2fx, drain %.2fx)", g_state.highTimer, g_cfg.upSeconds, g_state.stableCreditRate, g_state.stableCreditDrainRate);
    ImGui::Text("Probe %.2fs / %.2fs  bad %.2fs / %.2fs  failures %d", g_state.probeTimer, g_cfg.upProbeSeconds, g_state.probeBadTimer, g_cfg.probeRollbackSeconds, g_state.failedProbeCount);
    ImGui::Text("Cooldown %.2fs  probe backoff %.2fs  post-load grace %.2fs", g_state.cooldown, g_state.probeBackoff, g_state.postLoadingGrace);
    ImGui::Text("Checks: overBudget=%s stableBase=%s strong=%s soft=%s candidate=%s emergency=%s",
        g_state.dbgOverBudget ? "yes" : "no",
        g_state.dbgStableBase ? "yes" : "no",
        g_state.dbgStableStrong ? "yes" : "no",
        g_state.dbgStableSoft ? "yes" : "no",
        g_state.dbgStableCandidate ? "yes" : "no",
        g_state.dbgEmergencyBad ? "yes" : "no");
    ImGui::Text("Budgets ms: target %.2f | down %.2f | stable %.2f | emergency %.2f", g_state.dbgTargetMs, g_state.dbgDownMs, g_state.dbgStableMs, g_state.dbgEmergencyMs);

    DrawSectionLabel("Apply queues");
    ImGui::Text("Model apply queue: %s step=%d reason=%s", g_state.stagedActive ? "active" : "idle", g_state.stagedStep, g_state.stagedReason.c_str());
    ImGui::Text("Graphics queue: %s profile=%s row=%u reason=%s", g_state.extraGearQueueActive ? "active" : "idle", GovernorTierName(g_state.extraGearQueueTier), static_cast<unsigned>(g_state.extraGearQueueIndex), g_state.extraGearQueueReason.c_str());
    if (!g_state.extraGearQueueWaitReason.empty())
    {
        ImGui::Text("Graphics wait: %s %.1fs", g_state.extraGearQueueWaitReason.c_str(), g_state.extraGearQueueBlockedSeconds);
    }

    DrawSectionLabel("Backend");
    ImGui::TextWrapped("Backend: %s", backend.summary.c_str());
    ImGui::Text("Ready: %s  Settings captured: %s  Hooks: %s", backend.available ? "yes" : "no", backend.settingsCaptured ? "yes" : "no", backend.hooksInstalled ? "yes" : "no");
    ImGui::Text("Model: limit %s / quality %s  Graphics matrix: %s  Shadow refresh: %s",
        backend.characterLimitAvailable ? "ok" : "off",
        backend.characterQualityAvailable ? "ok" : "off",
        backend.graphicsMatrixAvailable ? "ok" : "off",
        backend.shadowRefreshAvailable ? "ok" : "off");
    ImGui::Text("Captured apply context: Limit %s  Quality %s", backend.capturedLimitContext ? "yes" : "zero-arg", backend.capturedQualityContext ? "yes" : "zero-arg");
    ImGui::Text("Shadow context captured: %s  captures=%llu  refreshes=%llu",
        backend.shadowContextCaptured ? "yes" : "no",
        static_cast<unsigned long long>(backend.shadowCaptures),
        static_cast<unsigned long long>(backend.shadowRefreshes));
    ImGui::Text("Threads: render=%lu gw2=%lu current=%lu nativeCalls=%llu mismatch=%llu",
        static_cast<unsigned long>(backend.renderThreadId),
        static_cast<unsigned long>(backend.gw2ThreadId),
        static_cast<unsigned long>(backend.currentThreadId),
        static_cast<unsigned long long>(backend.nativeApplyCalls),
        static_cast<unsigned long long>(backend.nativeApplyThreadMismatches));

    DrawSectionLabel("Manual/debug tools");
    ImGui::SliderInt("Manual Character Model Limit", &g_cfg.manualLimitValue, 0, 4, "%d");
    ImGui::SliderInt("Manual Character Model Quality", &g_cfg.manualQualityValue, 0, 4, "%d");
    if (ImGui::Button("Apply selected model values")) { ApplyLiveModelValues(g_cfg.manualLimitValue, g_cfg.manualQualityValue, "manual-apply"); }
    ImGui::SameLine();
    if (ImGui::Button("Apply current automatic tier now")) { ForceApplyCurrentTier("manual-test-current-auto-tier", true); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel queued apply"))
    {
        g_state.stagedActive = false;
        g_state.stagedQualityHardRefresh = false;
        g_state.extraGearQueueActive = false;
        g_state.extraGearPendingActive = false;
        g_state.status = "Queued apply canceled.";
        TraceLine("Staged/graphics apply canceled by user.");
    }

    if (ImGui::Button("Both Lowest")) { g_cfg.manualLimitValue = 0; g_cfg.manualQualityValue = 0; ApplyLiveModelValues(0, 0, "button-lowest"); }
    ImGui::SameLine();
    if (ImGui::Button("Both Medium")) { g_cfg.manualLimitValue = 2; g_cfg.manualQualityValue = 2; ApplyLiveModelValues(2, 2, "button-medium"); }
    ImGui::SameLine();
    if (ImGui::Button("Both Highest")) { g_cfg.manualLimitValue = 4; g_cfg.manualQualityValue = 4; ApplyLiveModelValues(4, 4, "button-highest"); }

    DrawSectionLabel("Logs");
    ImGui::Checkbox("Trace enabled", &g_cfg.tracerEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-change log", &g_cfg.autoChangeLogEnabled);
    ImGui::TextWrapped("Trace: %s", WideToUtf8(g_tracePath).c_str());
    ImGui::TextWrapped("Auto-change log: %s", WideToUtf8(g_autoLogPath).c_str());
    if (ImGui::Button("Clear auto-change log")) { std::ofstream(g_autoLogPath, std::ios::binary | std::ios::trunc); }
    ImGui::SameLine();
    if (ImGui::Button("Clear trace/log/snapshots"))
    {
        std::ofstream(g_tracePath, std::ios::binary | std::ios::trunc);
        std::ofstream(g_xrefPath, std::ios::binary | std::ios::trunc);
        std::ofstream(g_probePath, std::ios::binary | std::ios::trunc);
    }
    ImGui::SameLine();
    if (ImGui::Button("Write marker")) { TraceLine("USER_MARKER from debug UI"); }

    DrawSectionLabel("Advanced apply engine");
    ImGui::Checkbox("Allow live apply through backend", &g_cfg.liveWritesEnabled);
    ImGui::Checkbox("Control Character Model Limit", &g_cfg.controlLimitLive);
    ImGui::Checkbox("Control Character Model Quality", &g_cfg.controlQualityLive);
    ImGui::Checkbox("Use staged apply queue", &g_cfg.stagedApplyEnabled);
    ImGui::DragInt("Apply step delay ms", &g_cfg.stagedApplyDelayMs, 5, 0, 1000);
    if (ImGui::Button("Refresh observed")) { VisualBackendRefreshObserved(); TraceLine("Manual refresh observed values."); }

    DrawSectionLabel("Options bool probe");
    ImGui::TextWrapped("For Standard Enemy/Friendly Models: press Baseline, toggle one GW2 Options checkbox, then press Diff. Keep only one native option change between captures.");
    if (ImGui::Button("Probe baseline before native toggle"))
    {
        g_cfg.tracerEnabled = true;
        VisualBackendCaptureOptionsProbeBaseline("manual-baseline");
        VisualBackendLogOptionsProbeKnownFields("manual-baseline");
        g_state.status = "Options probe baseline captured.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Probe diff after native toggle"))
    {
        g_cfg.tracerEnabled = true;
        VisualBackendDiffOptionsProbe("manual-after-toggle");
        VisualBackendLogOptionsProbeKnownFields("manual-after-toggle");
        g_state.status = "Options probe diff written to settings trace.";
    }
    if (ImGui::Button("Marker: before Standard Enemy Models toggle"))
    {
        g_cfg.tracerEnabled = true;
        TraceLine("USER_MARKER before Standard Enemy Models toggle");
    }
    ImGui::SameLine();
    if (ImGui::Button("Marker: after Standard Enemy Models toggle"))
    {
        g_cfg.tracerEnabled = true;
        TraceLine("USER_MARKER after Standard Enemy Models toggle");
    }
    if (ImGui::Button("Marker: before Standard Friendly Models toggle"))
    {
        g_cfg.tracerEnabled = true;
        TraceLine("USER_MARKER before Standard Friendly Models toggle");
    }
    ImGui::SameLine();
    if (ImGui::Button("Marker: after Standard Friendly Models toggle"))
    {
        g_cfg.tracerEnabled = true;
        TraceLine("USER_MARKER after Standard Friendly Models toggle");
    }
}

void DrawTierOverlay()
{
    if (!g_cfg.tierOverlayVisible) { return; }

    static bool initialPosApplied = false;
    if (g_cfg.tierOverlayPosValid && !initialPosApplied)
    {
        ImGui::SetNextWindowPos(ImVec2(g_cfg.tierOverlayPosX, g_cfg.tierOverlayPosY), ImGuiCond_Always);
        initialPosApplied = true;
    }
    else
    {
        ImGui::SetNextWindowPos(ImVec2(18.0f, 120.0f), ImGuiCond_FirstUseEver);
    }

    ImGui::SetNextWindowSize(ImVec2(92, 30), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin(TierOverlayWindowName, nullptr, flags))
    {
        ImGui::TextUnformatted(GovernorTierName(g_state.currentTier));
        ImVec2 p = ImGui::GetWindowPos();
        if (!g_cfg.tierOverlayPosValid || std::fabs(g_cfg.tierOverlayPosX - p.x) > 0.5f || std::fabs(g_cfg.tierOverlayPosY - p.y) > 0.5f)
        {
            g_cfg.tierOverlayPosValid = true;
            g_cfg.tierOverlayPosX = p.x;
            g_cfg.tierOverlayPosY = p.y;
        }
    }
    ImGui::End();
}


void DrawWindow()
{
    VisualBackendMarkRenderThread();
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    g_state.uiFpsRefreshTimer -= dt;
    if (g_state.uiFpsRefreshTimer <= 0.0f)
    {
        g_state.uiFps = g_state.fps;
        g_state.uiFpsRefreshTimer = 0.25f;
    }

    UpdateGovernor(dt);
    UpdateStartupProfileApply(dt);
    UpdateStagedApply(dt);
    UpdateExtraGearQueue(dt);
    VisualBackendRefreshObserved();

    DrawTierOverlay();

    if (!g_cfg.windowVisible) { return; }
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_FirstUseEver);
    bool open = g_cfg.windowVisible;
    if (!ImGui::Begin(WindowName, &open))
    {
        g_cfg.windowVisible = open;
        ImGui::End();
        return;
    }
    g_cfg.windowVisible = open;
    DrawDebugStatsContent();
    if (ImGui::Button("Save config")) { SaveConfig(); g_state.status = "Config saved."; }
    ImGui::End();
}

void DrawOptions()
{
    if (ImGui::CollapsingHeader("Graphics tiers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawGraphicsTierMatrix();
    }

    if (ImGui::CollapsingHeader("Automatic tier range", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTierRangeControls();
    }

    if (ImGui::CollapsingHeader("Governor tuning"))
    {
        DrawGovernorTuningControls();
    }

    if (ImGui::CollapsingHeader("Debug windows"))
    {
        if (ImGui::Button(g_cfg.windowVisible ? "Hide debug window" : "Open debug window"))
        {
            g_cfg.windowVisible = !g_cfg.windowVisible;
            SaveConfig();
        }
        bool overlay = g_cfg.tierOverlayVisible;
        if (ImGui::Checkbox("Show tiny movable current-tier window", &overlay))
        {
            g_cfg.tierOverlayVisible = overlay;
            SaveConfig();
        }
        ImGui::Text("FPS %.1f", g_state.uiFps);
    }

    if (ImGui::Button("Save config")) { SaveConfig(); g_state.status = "Config saved."; }
}

void OnToggle(const char*, bool isRelease)
{
    if (isRelease) { return; }
    g_cfg.tierOverlayVisible = !g_cfg.tierOverlayVisible;
    SaveConfig();
}

void InitPaths()
{
    if (g_api && g_api->Paths_GetAddonDirectory)
    {
        const char* addon = g_api->Paths_GetAddonDirectory("visual_governor");
        if (addon && *addon) { g_addonDir = Utf8ToWide(addon); }
    }
    if (g_addonDir.empty()) { g_addonDir = (std::filesystem::path(ModuleDirectory()) / L"visual_governor").wstring(); }
    g_addonDir = AddSlash(g_addonDir);
    try { std::filesystem::create_directories(std::filesystem::path(g_addonDir)); } catch (...) {}
    g_configPath = (std::filesystem::path(g_addonDir) / L"config.ini").wstring();
    try
    {
        std::filesystem::path oldDir = std::filesystem::path(ModuleDirectory()) / L"gw2_visual_governor";
        std::filesystem::path oldConfig = oldDir / L"config.ini";
        if (!std::filesystem::exists(std::filesystem::path(g_configPath)) && std::filesystem::exists(oldConfig))
        {
            std::filesystem::copy_file(oldConfig, std::filesystem::path(g_configPath), std::filesystem::copy_options::skip_existing);
        }
    } catch (...) {}
    g_tracePath = (std::filesystem::path(g_addonDir) / L"settings-write-trace.log").wstring();
    g_autoLogPath = (std::filesystem::path(g_addonDir) / L"auto-governor.log").wstring();
    g_xrefPath = (std::filesystem::path(g_addonDir) / L"string-xrefs.log").wstring();
    g_probePath = (std::filesystem::path(g_addonDir) / L"gw2-code-probes.log").wstring();
}

void AddonLoad(AddonAPI_t* api)
{
    g_api = api;
    if (g_api && g_api->ImguiContext)
    {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_api->ImguiContext));
        ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(g_api->ImguiMalloc), reinterpret_cast<void (*)(void*, void*)>(g_api->ImguiFree));
    }
    InitPaths();
    InitExtraGearDefaults();
    LoadConfig();
    if (g_cfg.tracerEnabled)
    {
        std::ofstream(g_tracePath, std::ios::binary | std::ios::app) << "\n=== addon load " << NowString() << " ===\n";
    }
    VisualBackendInitialize();
    g_state.currentTier = ClampTierToConfiguredRange(g_cfg.startTier);
    VisualBackendInstallHooks();

    if (g_api && g_api->GUI_Register)
    {
        g_api->GUI_Register(RT_Render, DrawWindow);
        g_api->GUI_Register(RT_OptionsRender, DrawOptions);
    }
    if (g_api && g_api->GUI_RegisterCloseOnEscape) { g_api->GUI_RegisterCloseOnEscape(WindowName, &g_cfg.windowVisible); }
    if (g_api && g_api->InputBinds_RegisterWithString) { g_api->InputBinds_RegisterWithString(KeybindToggle, OnToggle, "ALT+SHIFT+V"); }
    Log(LOGL_INFO, "Loaded.");
}

void AddonUnload()
{
    SaveConfig();
    VisualBackendShutdown();
    if (g_api && g_api->GUI_Deregister)
    {
        g_api->GUI_Deregister(DrawWindow);
        g_api->GUI_Deregister(DrawOptions);
    }
    if (g_api && g_api->GUI_DeregisterCloseOnEscape) { g_api->GUI_DeregisterCloseOnEscape(WindowName); }
    if (g_api && g_api->InputBinds_Deregister) { g_api->InputBinds_Deregister(KeybindToggle); }
    Log(LOGL_INFO, "Unloaded.");
    g_api = nullptr;
}

