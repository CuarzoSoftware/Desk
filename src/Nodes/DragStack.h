#ifndef DRAGSTACK_H
#define DRAGSTACK_H

#include <Core/Types.h>
#include <Nodes/IconView.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKText.h>
#include <memory>
#include <vector>

/**
 * @brief What's shown while dragging several items: their icons stacked (the grabbed one in front,
 *        the others peeking out up-right behind it) and a red badge counting them, like Finder.
 *
 * Its size depends on how many icons are stacked (see nodeSize() and frontOrigin()).
 */
class DragStack final : public AKContainer
{
public:
    DragStack(Int32 iconSize, AKNode *parent = nullptr) noexcept;

    /// Stacks @p images (back to front, at most DESK_DRAG_STACK_MAX are shown) and counts @p count
    /// items in the badge (hidden for a single item).
    void setItems(const std::vector<std::shared_ptr<RImage>> &images, UInt32 count) noexcept;

    /// Offset between consecutive layers (each one further back moves right and up by it).
    static Int32 LayerStep(Int32 iconSize) noexcept;

    /// Depth (0 = front) of item @p index of @p count (back to front), deeper ones sharing the last
    /// visible layer.
    static Int32 LayerDepth(size_t index, size_t count) noexcept;

    /// Top-left corner of the front icon within the node.
    SkIPoint frontOrigin() const noexcept { return m_frontOrigin; }
    SkISize nodeSize() const noexcept { return m_size; }

    AKRoundSolidColor badge { DESK_DRAG_BADGE_COLOR, CZBorderRadius::Make(0), this };
    AKText count { "", &badge };

private:
    // Each icon in its own positioned box.
    struct Layer
    {
        Layer(AKNode *parent) noexcept : box(YGFlexDirectionColumn, false, parent) {}
        AKContainer box;
        IconView icon { &box };
    };
    std::vector<std::unique_ptr<Layer>> m_layers;
    Int32 m_iconSize, m_badgeHeight;
    SkIPoint m_frontOrigin {};
    SkISize m_size {};
};

#endif // DRAGSTACK_H
