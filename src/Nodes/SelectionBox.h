#ifndef SELECTIONBOX_H
#define SELECTIONBOX_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKSolidColor.h>

/**
 * @brief The rubber band selection rectangle (translucent fill with a hairline border).
 *
 * Plain solid color nodes (no baked image): resizing it on every pointer motion only repaints the
 * strips that change, not everything under it.
 */
class SelectionBox final : public AKContainer
{
public:
    SelectionBox(AKNode *parent = nullptr) noexcept;

    /// Shows the box over @p rect (surface coordinates), or hides it with an empty rect.
    void setRect(const SkIRect &rect) noexcept;

private:
    AKSolidColor m_fill { CZAdaptiveColor(DESK_RUBBER_FILL), this };
    AKSolidColor m_top { CZAdaptiveColor(DESK_RUBBER_BORDER), this };
    AKSolidColor m_bottom { CZAdaptiveColor(DESK_RUBBER_BORDER), this };
    AKSolidColor m_left { CZAdaptiveColor(DESK_RUBBER_BORDER), this };
    AKSolidColor m_right { CZAdaptiveColor(DESK_RUBBER_BORDER), this };
};

#endif // SELECTIONBOX_H
