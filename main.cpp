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

    // Per-frame hook so the drag preview can follow the cursor. The static keeps
    // the subscription alive for the plugin's lifetime; its destructor (run when
    // dlclose tears the .so down on plugin unload) unsubscribes cleanly.
    static auto TICK_LISTENER = Event::bus()->m_events.tick.listen([] {
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
            if (auto* orient = dynamic_cast<COrientationTileAlgorithm*>(tiled.get()))
                orient->tick();
        }
    });

    HyprlandAPI::reloadConfig();

    return {"orientation-tile", "Orientation-aware tiling: rows on landscape monitors, columns on portrait monitors.", "Hannes", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    HyprlandAPI::removeAlgo(PHANDLE, "orientationtile");
}
