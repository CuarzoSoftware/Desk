#include <Nodes/ItemLabel.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/AK/Events/AKBakeEvent.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkRRect.h>
#include <CZ/skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <cmath>

using namespace skia::textlayout;

ItemLabel::ItemLabel(AKNode *parent) noexcept : AKBakeable(parent)
{
    SkRegion empty;
    setInputRegion(&empty);
    update();
}

void ItemLabel::setText(const std::string &text) noexcept
{
    if (m_text == text)
        return;

    m_text = text;
    update();
}

void ItemLabel::setWidth(Int32 width) noexcept
{
    if (m_width == width)
        return;

    m_width = width;
    update();
}

void ItemLabel::setSelected(bool selected) noexcept
{
    if (m_selected == selected)
        return;

    m_selected = selected;
    update();
}

void ItemLabel::update() noexcept
{
    ParagraphStyle paragraphStyle;
    paragraphStyle.setTextAlign(TextAlign::kCenter);
    paragraphStyle.setTextDirection(TextDirection::kLtr);
    paragraphStyle.setMaxLines(DESK_LABEL_LINES);
    paragraphStyle.setEllipsis(u"…");

    TextStyle style { theme()->DefaultTextStyle };
    style.setFontSize(DESK_LABEL_FONT_SIZE);
    style.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    // The theme's style has a foreground paint, which overrides setColor().
    SkPaint paint;
    paint.setColor(DESK_LABEL_COLOR);
    paint.setAntiAlias(true);
    style.setColor(DESK_LABEL_COLOR);
    style.setForegroundPaint(paint);
    style.resetShadows();

    // Readable over any picture: a tight dark shadow plus a soft halo (none when highlighted).
    if (!m_selected)
    {
        style.addShadow(TextShadow(DESK_LABEL_SHADOW, SkPoint::Make(0.f, 1.f), 1.2));
        style.addShadow(TextShadow(0x80000000, SkPoint::Make(0.f, 0.f), 2.5));
    }

    auto builder { ParagraphBuilder::make(paragraphStyle, AKApp::Get()->fontCollection()) };
    builder->pushStyle(style);
    builder->addText(m_text.c_str());
    builder->pop();
    m_paragraph = builder->Build();
    m_paragraph->layout(std::max(1, m_width - 2 * DESK_LABEL_PADDING_X));

    layout().setWidth(m_width);
    layout().setHeight(std::ceil(m_paragraph->getHeight()) + 2.f);
    addDamage(AK_IRECT_INF);
}

SkRect ItemLabel::textBounds() const noexcept
{
    SkRect bounds { SkRect::MakeEmpty() };

    if (!m_paragraph)
        return bounds;

    std::vector<LineMetrics> lines;
    m_paragraph->getLineMetrics(lines);

    for (const auto &line : lines)
    {
        const SkRect rect { SkRect::MakeXYWH(
            DESK_LABEL_PADDING_X + line.fLeft - DESK_LABEL_PADDING_X,
            1.f + line.fBaseline - line.fAscent - 1.f,
            line.fWidth + 2 * DESK_LABEL_PADDING_X,
            line.fHeight + 2.f) };
        bounds.join(rect);
    }

    return bounds;
}

void ItemLabel::bakeEvent(const AKBakeEvent &e)
{
    if (e.damage.isEmpty())
        return;

    auto pass { e.surface->beginPass(RPassCap_SkCanvas) };
    SkCanvas *c { pass->getCanvas() };
    c->clear(SK_ColorTRANSPARENT);

    if (!m_paragraph)
        return;

    if (m_selected)
    {
        // One rounded highlight per line, like Finder.
        std::vector<LineMetrics> lines;
        m_paragraph->getLineMetrics(lines);

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(DESK_LABEL_SELECTION);

        for (const auto &line : lines)
        {
            const SkRect rect { SkRect::MakeXYWH(
                line.fLeft,
                1.f + line.fBaseline - line.fAscent - 1.f,
                line.fWidth + 2 * DESK_LABEL_PADDING_X,
                line.fHeight + 2.f) };
            c->drawRRect(SkRRect::MakeRectXY(rect, 4.f, 4.f), paint);
        }
    }

    m_paragraph->paint(c, DESK_LABEL_PADDING_X, 1.f);
}
