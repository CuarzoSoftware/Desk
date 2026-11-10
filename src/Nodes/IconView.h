#ifndef ICONVIEW_H
#define ICONVIEW_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <memory>

/**
 * @brief An icon (or thumbnail) with a soft drop shadow following its shape, so it stands out on
 *        any background picture.
 *
 * The icon and its shadow are pre-rendered together into one image (Assets::shadowedIcon(), cached
 * per icon) shown by a plain image frame: no live shadow effect to recompute when repainting. The
 * frame extends past the node by the shadow's margin (a percentage of the node, so it follows any
 * size the node is given).
 */
class IconView final : public AKContainer
{
public:
    IconView(AKNode *parent = nullptr) noexcept;

    void setImage(std::shared_ptr<RImage> image) noexcept;

    /// The icon itself (without the shadow).
    std::shared_ptr<RImage> image() const noexcept { return m_image; }

    void setOpacity(SkScalar opacity) noexcept { frame.setOpacity(opacity); }

    AKImageFrame frame { this };

private:
    std::shared_ptr<RImage> m_image;
};

#endif // ICONVIEW_H
