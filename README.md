# orientation-tile

A custom tiling layout for **Hyprland 0.55.2** that picks its split axis from each
monitor's orientation:

- **Landscape monitor** (width ≥ height) → windows tile in a **horizontal row**.
- **Portrait monitor** (height > width) → windows tile in a **vertical column**.

No dwindling / spiraling. Every window gets an equal share of the axis by default,
and you can resize a window (mouse drag or `resizeactive`) to redistribute space
with its neighbour. Because Hyprland creates one layout instance per workspace,
the orientation is decided independently for each workspace based on the monitor
it currently lives on — so a workspace on your portrait DP-2 stacks vertically
while a workspace on your landscape DP-1 tiles horizontally, simultaneously.

Orientation is read from the monitor's **transform-adjusted** logical size, so
rotated panels (`transform 1`/`3`) are detected correctly as portrait.

## Requirements

- Hyprland **0.55.2** (built from commit `39d7e20…`). The plugin checks the ABI
  hash on load and refuses to load against a mismatched Hyprland — rebuild it
  after every Hyprland update (`hyprpm update`).
- A C++23 toolchain (gcc ≥ 13 or clang ≥ 16) and `hyprpm`'s build deps. On Arch:
  `base-devel`, `cmake`, `meson`, plus the Hyprland headers `hyprpm` pulls itself.

## Install (recommended: hyprpm)

`hyprpm` compiles the plugin against your *installed* Hyprland headers, which is
the safe way to get a matching ABI.

```sh
# 1. Make sure hyprpm has the right headers for your Hyprland
hyprpm update

# 2. Add this plugin
hyprpm add https://github.com/kebabskal/orientation-tile

# 3. Enable it
hyprpm enable orientation-tile

# 4. Confirm it loaded
hyprpm list
```

To load it automatically on every Hyprland start, add to your `hyprland.conf`:

```ini
exec-once = hyprpm reload
```

> Iterating on the code locally instead? `hyprpm add /absolute/path/to/checkout`
> works too — point it at a local git repo and it picks up commits on `hyprpm update`.

## Enable the layout

In `hyprland.conf`:

```ini
general {
    layout = orientationtile
}
```

Reload (`hyprctl reload`) or restart Hyprland. Existing windows re-tile immediately.

### Optional: force an orientation

By default the layout follows the monitor (`auto`). You can override it globally:

```ini
plugin {
    orientationtile {
        orientation = auto   # auto (default) | row | column
    }
}
```

You can also pin a layout per workspace with a normal Hyprland workspace rule, e.g.
`workspace = 5, layoutopt:...` — the layout itself is selected with
`general:layout`, and per-workspace `layout = ...` rules work as usual.

## Keybinds

The layout plugs into Hyprland's **built-in** dispatchers — you don't need any
plugin-specific binds. Suggested config:

```ini
# resize the focused window (redistributes with its neighbour along the axis)
binde = SUPER, L, resizeactive, 40 0     # wider  (row layouts)
binde = SUPER, H, resizeactive, -40 0    # narrower
binde = SUPER, K, resizeactive, 0 -40    # shorter (column layouts)
binde = SUPER, J, resizeactive, 0 40     # taller

# mouse resize (drag any edge/corner; space is taken from the neighbour)
bindm = SUPER, mouse:273, resizewindow

# move / reorder the focused window within the stack, or to the next monitor
bind = SUPER SHIFT, left,  movewindow, l
bind = SUPER SHIFT, right, movewindow, r
bind = SUPER SHIFT, up,    movewindow, u
bind = SUPER SHIFT, down,  movewindow, d
```

It also understands two `layoutmsg` commands for swapping the focused window with
its neighbour in the stack:

```ini
bind = SUPER, bracketright, layoutmsg, swapnext
bind = SUPER, bracketleft,  layoutmsg, swapprev
```

## Behaviour notes

- **New windows** are appended to the end of the row/column (predictable, and
  stable when you switch layouts — Hyprland re-adds every window on a switch).
- **Resizing** moves space between the resized window and one neighbour; no window
  shrinks below 5% of the axis.
- **Gaps** (`general:gaps_in` / `gaps_out`) and reserved areas (bars) are applied
  by Hyprland automatically — the layout positions windows edge-to-edge inside the
  usable work area and Hyprland insets the gaps.
- **Floating, fullscreen, pseudo** windows are handled by Hyprland as normal; only
  tiled windows participate in the row/column.
- Dragging a window onto another monitor drops it at the cursor position along that
  monitor's axis.

## Manual build (without hyprpm)

```sh
make
# produces orientation-tile.so
# then load it however you load raw plugins, e.g.:
#   plugin = /absolute/path/to/orientation-tile.so   (in hyprland.conf)
```

Building manually requires the Hyprland headers installed and matching your running
version exactly. **hyprpm is strongly preferred** because it guarantees the headers
(and therefore the ABI hash) match — a plugin built against different headers will
be rejected at load.

## Troubleshooting

- *"Version mismatch" notification on load* → the plugin was built against a
  different Hyprland than the one running. Run `hyprpm update && hyprpm reload`.
- *Layout doesn't change* → confirm `general:layout = orientationtile` and that
  `hyprpm list` shows the plugin enabled; check `hyprctl plugin list`.
- *A square-ish monitor guesses wrong* → set `plugin:orientationtile:orientation`
  to `row` or `column`.

## How it works (for hacking on it)

It implements `Layout::ITiledAlgorithm` (the per-workspace tiled-algorithm
interface introduced in the 0.55 layout refactor) and registers itself with
`HyprlandAPI::addTiledAlgo`. Each workspace owns one instance holding an ordered
list of windows, each with a `weight` (its fractional share of the axis; weights
sum to 1). `recalculate()` reads the workspace's work area and its monitor's
transform-adjusted size, decides row vs column, and lays the windows out along the
chosen axis. `resizeTarget` shifts weight between a window and a neighbour;
`moveTargetInDirection` reorders within the stack or hands off to the monitor in
that direction.

Source: `main.cpp` (registration), `OrientationTileAlgorithm.{hpp,cpp}` (the layout).
