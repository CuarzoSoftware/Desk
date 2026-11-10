#include <Nodes/DragStack.h>
#include <algorithm>
#include <cmath>
#include <string>

DragStack::DragStack(Int32 iconSize, AKNode *parent) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    m_iconSize(iconSize),
    m_badgeHeight(std::max(18, Int32(std::round(iconSize * 0.34f))))
{
    SkRegion empty;
    setInputRegion(&empty);
    layout().setPositionType(YGPositionTypeAbsolute);
    layout().setPosition(YGEdgeLeft, 0.f);
    layout().setPosition(YGEdgeTop, 0.f);

    badge.setBorderRadius(CZBorderRadius::Make(m_badgeHeight / 2));
    badge.layout().setPositionType(YGPositionTypeAbsolute);
    badge.layout().setPosition(YGEdgeRight, 0.f);
    badge.layout().setPosition(YGEdgeTop, 0.f);
    badge.layout().setHeight(m_badgeHeight);
    badge.layout().setMinWidth(m_badgeHeight);
    badge.layout().setPadding(YGEdgeHorizontal, m_badgeHeight * 0.3f);
    badge.layout().setAlignItems(YGAlignCenter);
    badge.layout().setJustifyContent(YGJustifyCenter);

    count.setFontSize(std::round(m_badgeHeight * 0.62f));
    count.setFontStyle(SkFontStyle::Bold());
    count.setTextColor(CZAdaptiveColor(0xFFFFFFFF));
}

Int32 DragStack::LayerStep(Int32 iconSize) noexcept
{
    return std::max(3, Int32(std::round(iconSize * 0.08f)));
}

Int32 DragStack::LayerDepth(size_t index, size_t count) noexcept
{
    return Int32(std::min<size_t>(count - 1 - index, DESK_DRAG_STACK_MAX - 1));
}

void DragStack::setItems(const std::vector<std::shared_ptr<RImage>> &images, UInt32 n) noexcept
{
    m_layers.clear();

    // The front-most DESK_DRAG_STACK_MAX icons.
    const size_t visible { std::min<size_t>(std::max<size_t>(images.size(), 1), DESK_DRAG_STACK_MAX) };
    const size_t first { images.size() > visible ? images.size() - visible : 0 };
    const Int32 step { LayerStep(m_iconSize) };
    const Int32 spread { Int32(visible - 1) * step };
    const Int32 overflow { n > 1 ? m_badgeHeight / 2 : 0 }; // Room for the badge on the top-right corner

    // Back layers peek out up-right behind the front one (at the bottom-left).
    m_frontOrigin = { 0, overflow + spread };
    m_size = { m_iconSize + spread + overflow, m_iconSize + spread + overflow };

    for (size_t i = 0; i < visible; i++)
    {
        const Int32 depth { Int32(visible - 1 - i) }; // 0 = front
        auto layer { std::make_unique<Layer>(this) };
        layer->icon.setImage(i + first < images.size() ? images[i + first] : nullptr);
        layer->box.layout().setPositionType(YGPositionTypeAbsolute);
        layer->box.layout().setPosition(YGEdgeLeft, m_frontOrigin.x() + depth * step);
        layer->box.layout().setPosition(YGEdgeTop, m_frontOrigin.y() - depth * step);
        layer->box.layout().setWidth(m_iconSize);
        layer->box.layout().setHeight(m_iconSize);
        layer->icon.layout().setWidth(m_iconSize);
        layer->icon.layout().setHeight(m_iconSize);
        m_layers.emplace_back(std::move(layer));
    }

    // Above the icons.
    badge.setParent(nullptr);
    badge.setParent(this);
    badge.setVisible(n > 1);
    count.setText(std::to_string(n));

    layout().setWidth(m_size.width());
    layout().setHeight(m_size.height());
}
