#pragma once

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Orientation-aware tiled layout.
//
// One instance exists per workspace (CSpace). Windows are arranged along a
// single axis chosen from the workspace's monitor:
//   * landscape monitor (width >= height) -> horizontal row
//   * portrait  monitor (height > width)  -> vertical column
//
// Windows share the axis equally by default. Each window carries a `weight`
// (a fraction of the axis; weights across the workspace sum to 1) so it can be
// resized with the mouse or `resizeactive`, redistributing space with a
// neighbour.
class COrientationTileAlgorithm : public Layout::ITiledAlgorithm {
  public:
    COrientationTileAlgorithm()           = default;
    ~COrientationTileAlgorithm() override = default;

    void                       newTarget(SP<Layout::ITarget> target) override;
    void                       movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                       removeTarget(SP<Layout::ITarget> target) override;

    void                       resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                       recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN) override;

    SP<Layout::ITarget>        getNextCandidate(SP<Layout::ITarget> old) override;

    Config::ErrorResult        layoutMsg(const std::string_view& sv) override;
    std::optional<Vector2D>    predictSizeForNewTarget() override;

    void                       swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                       moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) override;

    std::optional<std::string> layoutName() const override;

    // Per-frame entry point used by the plugin's global tick subscription to
    // refresh the drag preview as the cursor moves. Idempotent and cheap when
    // no drag is in progress or the preview index hasn't changed.
    void                       tick();

  private:
    struct SNode {
        WP<Layout::ITarget> target;
        double              weight = 0.0; // share of the axis; weights across nodes sum to 1
    };

    std::vector<SP<SNode>> m_nodes;
    bool                   m_forceWarps = false; // snap (don't animate) during interactive resize

    // Live drag preview: while a tile-mode drag is in progress and the cursor is
    // over this workspace, m_previewIndex holds the predicted drop index and
    // recalculate() lays out N+1 slots so the other windows make room. -1 = no
    // preview, lay out the N real windows normally.
    int                    m_previewIndex = -1;

    SP<Layout::CSpace>     space() const;
    bool                   isColumn() const; // true => stack vertically (portrait)

    SP<SNode>              nodeFor(SP<Layout::ITarget> t) const;
    int                    indexOf(SP<Layout::ITarget> t) const;
    void                   insertAt(SP<Layout::ITarget> target, int index);
    void                   renormalize();

    // Pick where a (re)inserted window should land along the axis:
    //   * if a focalPoint is provided (e.g. cross-monitor move), use it
    //   * else if the user just released a mouse-move drag, use the cursor
    //   * else append at the end (predictable for plain new windows / layout switches)
    int                    dropIndexFor(std::optional<Vector2D> focalPoint) const;

    // Read the DragController + cursor and update m_previewIndex. Called from
    // recalculate() on each render frame; cheap when nothing changed.
    void                   updateDragPreview();
};
