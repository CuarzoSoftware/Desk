#include <Nodes/SelectionBox.h>

SelectionBox::SelectionBox(AKNode *parent) noexcept : AKContainer(YGFlexDirectionColumn, false, parent)
{
    SkRegion empty;
    setInputRegion(&empty);
    layout().setPositionType(YGPositionTypeAbsolute);
    setVisible(false);

    // The fill inside the border, then the four 1 pt edges.
    for (AKSolidColor *node : { &m_fill, &m_top, &m_bottom, &m_left, &m_right })
    {
        node->setInputRegion(&empty);
        node->layout().setPositionType(YGPositionTypeAbsolute);
    }

    m_fill.layout().setPosition(YGEdgeAll, 1.f);

    m_top.layout().setPosition(YGEdgeLeft, 0.f);
    m_top.layout().setPosition(YGEdgeTop, 0.f);
    m_top.layout().setWidthPercent(100.f);
    m_top.layout().setHeight(1.f);

    m_bottom.layout().setPosition(YGEdgeLeft, 0.f);
    m_bottom.layout().setPosition(YGEdgeBottom, 0.f);
    m_bottom.layout().setWidthPercent(100.f);
    m_bottom.layout().setHeight(1.f);

    m_left.layout().setPosition(YGEdgeLeft, 0.f);
    m_left.layout().setPosition(YGEdgeTop, 1.f);
    m_left.layout().setPosition(YGEdgeBottom, 1.f);
    m_left.layout().setWidth(1.f);

    m_right.layout().setPosition(YGEdgeRight, 0.f);
    m_right.layout().setPosition(YGEdgeTop, 1.f);
    m_right.layout().setPosition(YGEdgeBottom, 1.f);
    m_right.layout().setWidth(1.f);
}

void SelectionBox::setRect(const SkIRect &rect) noexcept
{
    setVisible(!rect.isEmpty());

    if (rect.isEmpty())
        return;

    layout().setPosition(YGEdgeLeft, rect.x());
    layout().setPosition(YGEdgeTop, rect.y());
    layout().setWidth(rect.width());
    layout().setHeight(rect.height());
}
