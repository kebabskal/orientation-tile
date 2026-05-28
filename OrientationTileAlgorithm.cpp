#include "OrientationTileAlgorithm.hpp"
#include "globals.hpp"

#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>

#include <hyprutils/string/VarList2.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <algorithm>
#include <cmath>
#include <format>

using namespace Layout;
using namespace Hyprutils::String;

// ---- helpers ---------------------------------------------------------------

SP<CSpace> COrientationTileAlgorithm::space() const {
    const auto PARENT = m_parent.lock();
    return PARENT ? PARENT->space() : nullptr;
}

bool COrientationTileAlgorithm::isColumn() const {
    // explicit override wins
    if (g_orientationMode) {
        const auto MODE = g_orientationMode->value();
        if (MODE == "row")
            return false;
        if (MODE == "column")
            return true;
    }

    const auto SPACE = space();
    if (!SPACE)
        return false;

    // prefer the monitor's true (transform-adjusted) orientation
    if (const auto WS = SPACE->workspace(); WS) {
        if (const auto MON = WS->m_monitor.lock(); MON && MON->m_size.x > 0 && MON->m_size.y > 0)
            return MON->m_size.y > MON->m_size.x;
    }

    // fallback: aspect of the usable work area
    const auto& WA = SPACE->workArea();
    return WA.h > WA.w;
}

SP<COrientationTileAlgorithm::SNode> COrientationTileAlgorithm::nodeFor(SP<ITarget> t) const {
    for (const auto& n : m_nodes) {
        if (n->target.lock() == t)
            return n;
    }
    return nullptr;
}

int COrientationTileAlgorithm::indexOf(SP<ITarget> t) const {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i]->target.lock() == t)
            return static_cast<int>(i);
    }
    return -1;
}

void COrientationTileAlgorithm::renormalize() {
    double sum = 0.0;
    for (const auto& n : m_nodes)
        sum += n->weight;

    if (m_nodes.empty())
        return;

    if (sum <= 0.0) {
        const double eq = 1.0 / m_nodes.size();
        for (auto& n : m_nodes)
            n->weight = eq;
        return;
    }

    for (auto& n : m_nodes)
        n->weight /= sum;
}

void COrientationTileAlgorithm::insertAt(SP<ITarget> target, int index) {
    const int N = static_cast<int>(m_nodes.size());
    index       = std::clamp(index, 0, N);

    auto node    = makeShared<SNode>();
    node->target = target;
    // give the newcomer an equal slice; renormalize() rescales everyone to sum 1
    node->weight = (N == 0) ? 1.0 : (1.0 / N);

    m_nodes.insert(m_nodes.begin() + index, node);
    renormalize();
}

int COrientationTileAlgorithm::dropIndexFor(std::optional<Vector2D> focalPoint) const {
    // 1. caller-supplied focal point wins (typically a cross-monitor move)
    std::optional<Vector2D> coord = focalPoint;

    // 2. if a mouse-move drag is in progress / just released, honour the cursor
    if (!coord.has_value() && g_layoutManager) {
        const auto& DRAG = g_layoutManager->dragController();
        if (DRAG && (DRAG->wasDraggingWindow() || DRAG->mode() == MBIND_MOVE))
            coord = g_pInputManager->getMouseCoordsInternal();
    }

    if (!coord.has_value() || m_nodes.empty())
        return static_cast<int>(m_nodes.size()); // 3. otherwise append

    // walk the stack along the layout axis: insert before the first node whose
    // midpoint is past the cursor (windows are already in axis-sorted order).
    const bool   COL = isColumn();
    const double p   = COL ? coord->y : coord->x;

    int index = 0;
    for (const auto& n : m_nodes) {
        const auto T = n->target.lock();
        if (!T) {
            ++index;
            continue;
        }
        const auto   BOX = T->position();
        const double mid = COL ? BOX.middle().y : BOX.middle().x;
        if (p > mid)
            ++index;
        else
            break;
    }
    return index;
}

// ---- IModeAlgorithm / ITiledAlgorithm -------------------------------------

void COrientationTileAlgorithm::newTarget(SP<ITarget> target) {
    // Default: append. But if a mouse-move drag is active (e.g. user is mid-drag
    // when this fires), dropIndexFor honours the cursor so the window lands
    // where they released it.
    insertAt(target, dropIndexFor(std::nullopt));
    recalculate();
}

void COrientationTileAlgorithm::movedTarget(SP<ITarget> target, std::optional<Vector2D> focalPoint) {
    insertAt(target, dropIndexFor(focalPoint));
    recalculate();
}

void COrientationTileAlgorithm::removeTarget(SP<ITarget> target) {
    std::erase_if(m_nodes, [&](const auto& n) { return n->target.lock() == target; });
    renormalize();
    recalculate();
}

void COrientationTileAlgorithm::resizeTarget(const Vector2D& delta, SP<ITarget> target, eRectCorner corner) {
    const int idx = indexOf(target);
    if (idx < 0 || m_nodes.size() < 2)
        return;

    const auto SPACE = space();
    if (!SPACE)
        return;

    const bool   COL   = isColumn();
    const auto&  WA    = SPACE->workArea();
    const double total = COL ? WA.h : WA.w;
    if (total <= 0.0)
        return;

    // how much the dragged window should grow along the layout axis
    double     grow = COL ? delta.y : delta.x;

    // dragging the start-side edge (top in a column, left in a row) grows the
    // window toward the previous neighbour, and the edge motion is inverted.
    const bool startEdge = COL ? (corner == CORNER_TOPLEFT || corner == CORNER_TOPRIGHT) : (corner == CORNER_TOPLEFT || corner == CORNER_BOTTOMLEFT);

    int neighbour = idx + 1;
    if (startEdge) {
        grow      = -grow;
        neighbour = idx - 1;
    }

    // at the ends of the stack, borrow from whichever neighbour exists
    if (neighbour < 0 || neighbour >= static_cast<int>(m_nodes.size())) {
        neighbour = (idx + 1 < static_cast<int>(m_nodes.size())) ? idx + 1 : idx - 1;
        if (neighbour < 0 || neighbour >= static_cast<int>(m_nodes.size()))
            return;
    }

    constexpr double MIN_FRAC = 0.05;
    double           df       = grow / total;

    double& a = m_nodes[idx]->weight;
    double& b = m_nodes[neighbour]->weight;

    // clamp so neither the window nor its neighbour collapses below MIN_FRAC
    if (a + df < MIN_FRAC)
        df = MIN_FRAC - a;
    if (b - df < MIN_FRAC)
        df = b - MIN_FRAC;

    a += df;
    b -= df;

    m_forceWarps = true;
    recalculate();
    m_forceWarps = false;
}

void COrientationTileAlgorithm::recalculate(eRecalculateReason reason) {
    const auto SPACE = space();
    if (!SPACE)
        return;

    // drop any expired targets defensively
    std::erase_if(m_nodes, [](const auto& n) { return !n->target.lock(); });

    if (m_nodes.empty())
        return;

    renormalize();

    const auto WA  = SPACE->workArea();
    const bool COL = isColumn();

    Hyprutils::Utils::CScopeGuard guard([this, SPACE] {
        if (const auto WS = SPACE->workspace(); WS) {
            if (const auto MON = WS->m_monitor.lock(); MON)
                g_pHyprRenderer->damageMonitor(MON);
        }

        if (!m_forceWarps)
            return;

        for (const auto& n : m_nodes) {
            if (const auto T = n->target.lock(); T)
                T->warpPositionSize();
        }
    });

    const double total = COL ? WA.h : WA.w;
    const double cross = COL ? WA.w : WA.h;
    const int    N     = static_cast<int>(m_nodes.size());

    double accFrac = 0.0;
    for (int i = 0; i < N; ++i) {
        const auto&  NODE    = m_nodes[i];
        const double startPx = std::round(accFrac * total);
        accFrac += NODE->weight;
        const double endPx = (i == N - 1) ? total : std::round(accFrac * total);
        const double lenPx = std::max(1.0, endPx - startPx);

        const auto T = NODE->target.lock();
        if (!T)
            continue;

        Vector2D pos, size;
        if (COL) {
            pos  = {WA.x, WA.y + startPx};
            size = {cross, lenPx};
        } else {
            pos  = {WA.x + startPx, WA.y};
            size = {lenPx, cross};
        }

        T->setPositionGlobal(CBox{pos, size});
    }
}

SP<ITarget> COrientationTileAlgorithm::getNextCandidate(SP<ITarget> old) {
    if (m_nodes.empty())
        return nullptr;

    const int idx = indexOf(old);
    if (idx < 0)
        return m_nodes.front()->target.lock();

    if (idx + 1 < static_cast<int>(m_nodes.size()))
        return m_nodes[idx + 1]->target.lock();
    if (idx - 1 >= 0)
        return m_nodes[idx - 1]->target.lock();

    return nullptr;
}

Config::ErrorResult COrientationTileAlgorithm::layoutMsg(const std::string_view& sv) {
    CVarList2 vars(std::string{sv}, 0, 's');

    if (vars.size() < 1 || vars[0].empty())
        return Config::configError("orientationtile: layoutmsg without params", Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);

    const auto COMMAND = vars[0];
    const auto FW      = Desktop::focusState()->window();

    const auto moveFocused = [&](bool next) -> Config::ErrorResult {
        if (!FW)
            return Config::configError("orientationtile: no focused window", Config::eConfigErrorLevel::WARNING, Config::eConfigErrorCode::NO_TARGET);

        const int idx = indexOf(FW->layoutTarget());
        if (idx < 0)
            return Config::configError("orientationtile: focused window isn't tiled here", Config::eConfigErrorLevel::WARNING, Config::eConfigErrorCode::INVALID_STATE);

        const int other = next ? idx + 1 : idx - 1;
        if (other < 0 || other >= static_cast<int>(m_nodes.size()))
            return {};

        std::swap(m_nodes[idx], m_nodes[other]);
        recalculate();
        return {};
    };

    if (COMMAND == "swapnext")
        return moveFocused(true);
    if (COMMAND == "swapprev")
        return moveFocused(false);

    return Config::configError(std::format("orientationtile: unknown layoutmsg '{}'", COMMAND), Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);
}

std::optional<Vector2D> COrientationTileAlgorithm::predictSizeForNewTarget() {
    const auto SPACE = space();
    if (!SPACE)
        return std::nullopt;

    const auto& WA  = SPACE->workArea();
    const bool  COL = isColumn();
    const int   N   = static_cast<int>(m_nodes.size());

    if (COL)
        return Vector2D{WA.w, WA.h / (N + 1)};
    return Vector2D{WA.w / (N + 1), WA.h};
}

void COrientationTileAlgorithm::swapTargets(SP<ITarget> a, SP<ITarget> b) {
    auto na = nodeFor(a);
    auto nb = nodeFor(b);

    if (na)
        na->target = b;
    if (nb)
        nb->target = a;

    recalculate();
}

void COrientationTileAlgorithm::moveTargetInDirection(SP<ITarget> t, Math::eDirection dir, bool silent) {
    if (!t || !t->window())
        return;

    const bool COL       = isColumn();
    const bool alongAxis = COL ? (dir == Math::DIRECTION_UP || dir == Math::DIRECTION_DOWN) : (dir == Math::DIRECTION_LEFT || dir == Math::DIRECTION_RIGHT);
    const bool toPrev    = (dir == Math::DIRECTION_UP || dir == Math::DIRECTION_LEFT);

    const int idx = indexOf(t);

    // reorder within this workspace's stack when moving along the layout axis
    if (alongAxis && idx >= 0) {
        const int other = toPrev ? idx - 1 : idx + 1;
        if (other >= 0 && other < static_cast<int>(m_nodes.size())) {
            std::swap(m_nodes[idx], m_nodes[other]);
            t->window()->setAnimationsToMove();
            recalculate();
            return;
        }
    }

    // otherwise hand the window off to the monitor in that direction
    static auto PMONITORFALLBACK = CConfigValue<Config::INTEGER>("binds:window_direction_monitor_fallback");
    if (!*PMONITORFALLBACK)
        return;

    const auto SPACE = space();
    if (!SPACE || !SPACE->workspace())
        return;

    const auto MON      = SPACE->workspace()->m_monitor.lock();
    const auto MONINDIR = g_pCompositor->getMonitorInDirection(MON, dir);
    if (MONINDIR && MONINDIR != MON) {
        if (const auto WS = MONINDIR->m_activeWorkspace; WS) {
            t->window()->setAnimationsToMove();
            t->assignToSpace(WS->m_space, focalPointForDir(t, dir));
        }
    }
}

std::optional<std::string> COrientationTileAlgorithm::layoutName() const {
    return "orientationtile";
}
