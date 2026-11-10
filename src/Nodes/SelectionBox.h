#ifndef SELECTIONBOX_H
#define SELECTIONBOX_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKBakeable.h>

/// The rubber band selection rectangle (translucent fill with a hairline border).
class SelectionBox final : public AKBakeable
{
public:
    SelectionBox(AKNode *parent = nullptr) noexcept;

    /// Shows the box over @p rect (surface coordinates), or hides it with an empty rect.
    void setRect(const SkIRect &rect) noexcept;

protected:
    void bakeEvent(const AKBakeEvent &event) override;
};

#endif // SELECTIONBOX_H
