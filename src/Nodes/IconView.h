#ifndef ICONVIEW_H
#define ICONVIEW_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKBakeable.h>
#include <CZ/AK/Effects/AKBackgroundImageShadowEffect.h>
#include <memory>

/**
 * @brief An icon (or thumbnail) scaled to fit the node, with a soft drop shadow following its shape
 *        so it stands out on any background picture.
 *
 * Baked (rather than an AKImageFrame) because the shadow effect is generated from the baked image.
 */
class IconView final : public AKBakeable
{
public:
    IconView(AKNode *parent = nullptr) noexcept;

    void setImage(std::shared_ptr<RImage> image) noexcept;
    std::shared_ptr<RImage> image() const noexcept { return m_image; }

    AKBackgroundImageShadowEffect shadow { DESK_ICON_SHADOW_RADIUS, { 0, DESK_ICON_SHADOW_OFFSET_Y }, DESK_ICON_SHADOW_COLOR, this };

protected:
    void bakeEvent(const AKBakeEvent &event) override;

private:
    std::shared_ptr<RImage> m_image;
};

#endif // ICONVIEW_H
