#include <Nodes/IconView.h>
#include <Core/Desk.h>

IconView::IconView(AKNode *parent, const ABShadow &shadow) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    m_shadow(shadow)
{
    SkRegion empty;
    setInputRegion(&empty);
    frame.setInputRegion(&empty);

    // The pre-rendered image covers the icon plus the shadow margin around it (as a fraction of the
    // icon size, see Assets::shadowedIcon()).
    const float margin { 100.f * Assets::ShadowMargin(m_shadow) };
    frame.setSizeMode(AKImageFrame::SizeMode::Fill);
    frame.layout().setPositionType(YGPositionTypeAbsolute);
    frame.layout().positionPercent(YGEdgeLeft, -margin);
    frame.layout().positionPercent(YGEdgeTop, -margin);
    frame.layout().setWidthPercent(100.f + 2.f * margin);
    frame.layout().setHeightPercent(100.f + 2.f * margin);
}

void IconView::setImage(std::shared_ptr<RImage> image) noexcept
{
    if (m_image == image)
        return;

    m_image = image;

    // Rendered for the size it's drawn at on each screen (the frame includes the shadow margin).
    if (image)
        frame.setImageProvider([this, image](Int32 pixels, Int32 scale){ return GetDesk()->assets.shadowedIcon(image, pixels, scale, m_shadow); });
    else
        frame.setImage(nullptr);
}
