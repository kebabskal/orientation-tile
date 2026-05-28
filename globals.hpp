#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

// Handle Hyprland hands us in pluginInit; needed for most HyprlandAPI calls.
inline HANDLE PHANDLE = nullptr;

// plugin:orientationtile:orientation
//   "auto"   -> follow the monitor: row on landscape, column on portrait (default)
//   "row"    -> always lay windows out in a horizontal row
//   "column" -> always stack windows in a vertical column
inline SP<Config::Values::CStringValue> g_orientationMode;
