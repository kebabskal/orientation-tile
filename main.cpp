#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/Compositor.hpp>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "OrientationTileAlgorithm.hpp"
#include "globals.hpp"

#include <stdexcept>
#include <typeinfo>

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[orientation-tile] Version mismatch: this plugin was built against a different Hyprland. Rebuild it (hyprpm update).",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 6000);
        throw std::runtime_error("[orientation-tile] Version mismatch");
    }

    // optional override of the auto orientation detection
    g_orientationMode = makeShared<Config::Values::CStringValue>(
        "plugin:orientationtile:orientation", "auto = follow the monitor (row on landscape, column on portrait); row = always horizontal; column = always vertical", "auto");
    HyprlandAPI::addConfigValueV2(PHANDLE, g_orientationMode);

    const bool REGISTERED = HyprlandAPI::addTiledAlgo(PHANDLE, "orientationtile", &typeid(COrientationTileAlgorithm),
                                                      [] { return makeUnique<COrientationTileAlgorithm>(); });

    if (!REGISTERED) {
        HyprlandAPI::addNotification(PHANDLE, "[orientation-tile] Could not register the layout (is the name already taken?)", CHyprColor{1.0, 0.5, 0.0, 1.0}, 6000);
        throw std::runtime_error("[orientation-tile] addTiledAlgo failed");
    }

    HyprlandAPI::addNotification(PHANDLE, "[orientation-tile] loaded — set general:layout = orientationtile", CHyprColor{0.2, 1.0, 0.2, 1.0}, 4000);

    // mouse.move fires on every cursor sample, independent of the animation
    // manager. Using `tick` was a mistake: tick is gated on shouldTickForNext()
    // and a tile-mode drag uses warpPositionSize() (instant snap, no animation),
    // so tick went silent between cursor wiggles. Mouse-move never goes silent
    // while the user is moving the mouse.
    auto kickAlgos = [] {
        if (!g_layoutManager)
            return;
        const auto& DRAG = g_layoutManager->dragController();
        if (!DRAG || DRAG->mode() != MBIND_MOVE || !DRAG->draggingTiled())
            return;
        if (!g_pCompositor)
            return;
        for (const auto& m : g_pCompositor->m_monitors) {
            if (!m || !m->m_activeWorkspace || !m->m_activeWorkspace->m_space)
                continue;
            const auto algo = m->m_activeWorkspace->m_space->algorithm();
            if (!algo)
                continue;
            const auto& tiled = algo->tiledAlgo();
            if (!tiled)
                continue;
            if (auto* orient = dynamic_cast<COrientationTileAlgorithm*>(tiled.get())) {
                if (g_pHyprRenderer)
                    g_pHyprRenderer->damageMonitor(m);
                m->m_scheduledRecalc = true;
                orient->tick();
            }
        }
    };

    static auto MOUSE_MOVE_LISTENER = Event::bus()->m_events.input.mouse.move.listen([kickAlgos](const Vector2D&, Event::SCallbackInfo&) { kickAlgos(); });

    // Belt-and-braces: tick still fires when other animations are active and is
    // cheap enough to keep around as a fallback so the indicator stays alive
    // for users whose pointer drivers batch sparsely.
    static auto TICK_LISTENER = Event::bus()->m_events.tick.listen([kickAlgos] { kickAlgos(); });

    HyprlandAPI::reloadConfig();

    return {"orientation-tile", "Orientation-aware tiling: rows on landscape monitors, columns on portrait monitors.", "Hannes", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    HyprlandAPI::removeAlgo(PHANDLE, "orientationtile");
}
