#include <Nodes/SelectionBox.h>
#include <CZ/AK/Events/AKBakeEvent.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <CZ/skia/core/SkCanvas.h>

SelectionBox::SelectionBox(AKNode *parent) noexcept : AKBakeable(parent)
{
    SkRegion empty;
    setInputRegion(&empty);
    layout().setPositionType(YGPositionTypeAbsolute);
    setVisible(false);
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
    addDamage(AK_IRECT_INF);
}

void SelectionBox::bakeEvent(const AKBakeEvent &e)
{
    if (e.damage.isEmpty())
        return;

    auto pass { e.surface->beginPass(RPassCap_SkCanvas) };
    SkCanvas *c { pass->getCanvas() };
    c->clear(SK_ColorTRANSPARENT);

    const SkRect rect { SkRect::MakeWH(worldRect().width(), worldRect().height()) };

    SkPaint paint;
    paint.setColor(DESK_RUBBER_FILL);
    c->drawRect(rect, paint);

    paint.setStroke(true);
    paint.setStrokeWidth(1.f);
    paint.setColor(DESK_RUBBER_BORDER);
    c->drawRect(rect.makeInset(0.5f, 0.5f), paint);
}
