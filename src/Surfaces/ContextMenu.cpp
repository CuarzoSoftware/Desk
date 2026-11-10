#include <Surfaces/ContextMenu.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>

ContextMenu::ContextMenu() noexcept
{
    slot()->layout().setPadding(YGEdgeVertical, 5.f);
    slot()->layout().setPadding(YGEdgeHorizontal, 5.f);
    layout().setMinWidth(220);
}

void ContextMenu::open(MSurface &parent, const SkIPoint &pos, const CZPointerButtonEvent &event, const Builder &build) noexcept
{
    setMapped(false);

    m_items.clear();
    m_view = std::make_unique<MMenuView>(this);
    m_view->setCheckGutter(true);
    m_view->onItemActivated.subscribe(m_view.get(), [this](MMenuItem *){ close(); });
    build(*this);

    if (m_items.empty())
        return;

    // Its top-left corner at the pointer, flipped/slid by the compositor near the screen edges.
    setParent(&parent);
    setAnchorRect(SkIRect::MakeXYWH(pos.x(), pos.y(), 1, 1));
    setAnchor(Anchor::TopLeft);
    setGravity(Gravity::BottomRight);
    setGrab(&event);
    setMapped(true);
}

MMenuAction &ContextMenu::addAction(const std::string &text, std::function<void()> onClicked, bool enabled) noexcept
{
    auto action { std::make_unique<MMenuAction>(text, m_view.get()) };
    action->onClicked = std::move(onClicked);
    action->setEnabled(enabled);
    MMenuAction &ref { *action };
    m_items.emplace_back(std::move(action));
    return ref;
}

void ContextMenu::addDivider() noexcept
{
    // No leading/trailing or doubled dividers.
    if (m_items.empty() || dynamic_cast<MMenuDivider*>(m_items.back().get()))
        return;

    m_items.emplace_back(std::make_unique<MMenuDivider>(m_view.get()));
}
