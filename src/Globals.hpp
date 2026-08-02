#pragma once

#include "GlassLayerSurface.hpp"
#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class CGlassDecoration;

// Layer namespaces are matched exactly by default. A pattern ending in '*' is
// treated as a prefix match, so e.g. "noctalia-background-*" matches
// "noctalia-background-DP-3" on any output without hardcoding the output name.
inline bool namespaceMatches(const std::string& pattern, const std::string& ns) {
    if (pattern.empty())
        return false;
    if (pattern.back() == '*')
        return ns.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return pattern == ns;
}

inline bool matchesAnyPattern(const std::unordered_set<std::string>& patterns, const std::string& ns) {
    if (patterns.contains(ns)) // fast path: exact match, no scan
        return true;
    for (const auto& pattern : patterns) {
        if (pattern.find('*') != std::string::npos && namespaceMatches(pattern, ns))
            return true;
    }
    return false;
}

template <typename T>
inline const T* findByPattern(const std::unordered_map<std::string, T>& map, const std::string& ns) {
    auto it = map.find(ns); // fast path: exact match, no scan
    if (it != map.end())
        return &it->second;
    for (const auto& [pattern, value] : map) {
        if (pattern.find('*') != std::string::npos && namespaceMatches(pattern, ns))
            return &value;
    }
    return nullptr;
}

struct SGlobalState {
    std::vector<WP<CGlassDecoration>> decorations;
    CShaderManager                    shaderManager;
    SPluginConfig                     config;

    // User-defined presets (populated from config keyword, swapped in on configReloaded)
    std::unordered_map<std::string, SCustomPreset> customPresets;

    // Shared blur temp framebuffer (reused across all decorations since they render sequentially)
    SP<Render::IFramebuffer> blurTempFramebuffer;

    // Layer surface glass state (one per tracked layer, keyed by raw pointer).
    // shared_ptr so CGlassLayerPassElement can hold a copy that survives map erasure mid-frame.
    std::unordered_map<Desktop::View::CLayerSurface*, std::shared_ptr<CGlassLayerSurface>> layerSurfaces;

    // Parsed namespace whitelist (empty = match all when layers enabled)
    std::unordered_set<std::string> layerNamespaceFilter;
    // Parsed namespace blacklist (always excluded, takes priority over whitelist)
    std::unordered_set<std::string> layerNamespaceExclude;
    // Per-namespace preset overrides (namespace → preset name)
    std::unordered_map<std::string, std::string> layerNamespacePresets;
    // Per-namespace mask alpha threshold (namespace → threshold, default 0.001)
    std::unordered_map<std::string, float> layerNamespaceMaskThresholds;
    // Namespaces that must always re-sample + re-blur every frame, bypassing the
    // sceneGeneration cache. Use for layers sitting above an animated layer
    // surface (video wallpaper, live background) that never bumps sceneGeneration
    // on its own, which otherwise leaves the glass showing a stale blurred frame.
    std::unordered_set<std::string> layerNamespaceForceLive;

    // Per-monitor generation counter, incremented when the scene behind layers
    // changes on that monitor. Layer surfaces compare to their cached value to
    // skip redundant blur work. Per-monitor avoids cross-monitor feedback loops
    // where re-sampling on an idle monitor captures its own stale glass output.
    // Keyed by MONITORID rather than CMonitor* so the code builds on both
    // Hyprland <= 0.55.x (global CMonitor) and git (Monitor::CMonitor), and a
    // stale entry can never alias a reallocated monitor object.
    std::unordered_map<MONITORID, uint64_t> sceneGeneration;

    uint64_t getSceneGeneration(const PHLMONITOR& mon) const {
        if (!mon)
            return 0;
        auto it = sceneGeneration.find(mon->m_id);
        return it != sceneGeneration.end() ? it->second : 0;
    }
    void bumpSceneGeneration(const PHLMONITOR& mon) {
        if (mon)
            sceneGeneration[mon->m_id]++;
    }

    // renderLayer hook
    CFunctionHook* renderLayerHook = nullptr;
};

using Render::GL::g_pHyprOpenGL;

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
