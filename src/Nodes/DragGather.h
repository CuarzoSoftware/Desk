#ifndef DRAGGATHER_H
#define DRAGGATHER_H

#include <Core/Types.h>
#include <Core/Assets.h>
#include <CZ/Marco/Roles/MSubsurface.h>
#include <CZ/Marco/Roles/MDND.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/Core/CZLinearAnimation.h>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief Several items dragged (from the desktop or a folder window): their icons fly to the
 *        pointer and stack up, like Finder.
 *
 * Each icon is its own subsurface of the drag icon (the MDND surface), so nothing clips them (they
 * may start anywhere, the pointer may leave the window right away) and they follow the pointer by
 * themselves (the compositor moves the drag icon with it). Each one flies from where it was to its
 * layer of the stack (see DragStack), stacked like it: back to front, all below the drag icon
 * (whose front icon is the grabbed one). Those past the stack's last layer (DESK_DRAG_STACK_MAX)
 * land on it. Only icons the user sees fly (not those scrolled out of view), however many are
 * dragged. Then they're unmapped and onFinished is called, where the
 * owner turns the drag icon into the whole stack: both land in the same commit of the drag icon.
 */
class DragGather
{
public:
    struct Flight
    {
        std::shared_ptr<RImage> image;
        SkRect from;  // Where the icon is (without its shadow), relative to the drag icon at the start
        Int32 depth;  // Its layer in the stack (0 = front)
    };

    /**
     * @param dnd The drag icon (already dragging).
     * @param front Where the front icon of the stack is in the drag icon (the grabbed one).
     * @param flights Back to front.
     */
    DragGather(MDND &dnd, const SkPoint &front, Int32 iconSize, const ABShadow &shadow,
               std::vector<Flight> flights, std::function<void()> onFinished) noexcept;
    ~DragGather() noexcept;

private:
    // An icon flying (see the class description)
    class Flyer final : public MSubsurface
    {
    public:
        Flyer(MSurface &parent) noexcept;
        AKImageFrame frame { this };
    };

    void step(Float64 progress) noexcept;

    SkPoint m_front;
    Int32 m_iconSize;
    float m_margin; // The shadow's, as a fraction of the icon size
    std::vector<Flight> m_flights;
    std::vector<std::unique_ptr<Flyer>> m_flyers;
    std::function<void()> m_onFinished;
    CZLinearAnimation m_anim;
};

#endif // DRAGGATHER_H
