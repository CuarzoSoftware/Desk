#include <Nodes/ItemLabel.h>
#include <Core/Desk.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Ream/RImage.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkRRect.h>
#include <CZ/skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <algorithm>
#include <cmath>

using namespace skia::textlayout;

ItemLabel::ItemLabel(AKNode *parent) noexcept : AKContainer(YGFlexDirectionColumn, false, parent)
{
    SkRegion empty;
    setInputRegion(&empty);
    m_frame.setInputRegion(&empty);
    m_frame.setSizeMode(AKImageFrame::SizeMode::Fill);
    m_frame.layout().setWidthPercent(100.f);
    m_frame.layout().setHeightPercent(100.f);
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
    showLook();
}

std::unique_ptr<Paragraph> ItemLabel::buildParagraph(bool selected) const noexcept
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
    if (!selected)
    {
        style.addShadow(TextShadow(DESK_LABEL_SHADOW, SkPoint::Make(0.f, 1.f), 1.2));
        style.addShadow(TextShadow(0x80000000, SkPoint::Make(0.f, 0.f), 2.5));
    }

    auto builder { ParagraphBuilder::make(paragraphStyle, AKApp::Get()->fontCollection()) };
    builder->pushStyle(style);
    builder->addText(m_text.c_str());
    builder->pop();
    auto paragraph { builder->Build() };
    paragraph->layout(std::max(1, m_width - 2 * DESK_LABEL_PADDING_X));
    return paragraph;
}

void ItemLabel::update() noexcept
{
    // The layout is the same for both looks (shadows don't change it).
    m_paragraph = buildParagraph(true);
    m_size = { m_width, Int32(std::ceil(m_paragraph->getHeight())) + 2 };

    // Skia's line metrics leave the ellipsis out of the line width: measure it to highlight it too.
    m_ellipsisWidth = 0.f;

    if (m_paragraph->didExceedMaxLines())
    {
        ParagraphStyle paragraphStyle;
        TextStyle style { theme()->DefaultTextStyle };
        style.setFontSize(DESK_LABEL_FONT_SIZE);
        style.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
        auto ellipsis { ParagraphBuilder::make(paragraphStyle, AKApp::Get()->fontCollection()) };
        ellipsis->pushStyle(style);
        ellipsis->addText(u"…");
        ellipsis->pop();
        auto measured { ellipsis->Build() };
        measured->layout(1000.f);
        m_ellipsisWidth = measured->getMaxIntrinsicWidth();
    }

    layout().setWidth(m_size.width());
    layout().setHeight(m_size.height());

    m_looks[0].reset();
    m_looks[1].reset();
    showLook();
}

void ItemLabel::showLook() noexcept
{
    // Rendered at the largest screen scale (sharp everywhere).
    Int32 scale { 1 };
    for (MScreen *s : GetDesk()->marco->screens())
        scale = std::max(scale, s->props().scale);

    if (scale != m_looksScale)
    {
        m_looks[0].reset();
        m_looks[1].reset();
        m_looksScale = scale;
    }

    auto &look { m_looks[m_selected] };

    if (!look)
        look = renderLook(m_selected);

    m_frame.setImage(look);
}

std::shared_ptr<RImage> ItemLabel::renderLook(bool selected) noexcept
{
    if (m_text.empty() || m_size.isEmpty())
        return {};

    auto surface { RSurface::Make(m_size, m_looksScale, true) };

    if (!surface)
        return {};

    {
        auto pass { surface->beginPass(RPassCap_SkCanvas) };
        SkCanvas *c { pass->getCanvas() };
        c->clear(SK_ColorTRANSPARENT);

        if (selected)
        {
            // One rounded highlight per line, like Finder.
            SkPaint paint;
            paint.setAntiAlias(true);
            paint.setColor(DESK_LABEL_SELECTION);

            for (const SkRect &rect : lineRects())
                c->drawRRect(SkRRect::MakeRectXY(rect, 4.f, 4.f), paint);

            m_paragraph->paint(c, DESK_LABEL_PADDING_X, 1.f);
        }
        else
            buildParagraph(false)->paint(c, DESK_LABEL_PADDING_X, 1.f);
    }

    return surface->image();
}

std::vector<SkRect> ItemLabel::lineRects() const noexcept
{
    std::vector<SkRect> rects;

    if (!m_paragraph)
        return rects;

    std::vector<LineMetrics> lines;
    m_paragraph->getLineMetrics(lines);

    for (size_t i = 0; i < lines.size(); i++)
    {
        const auto &line { lines[i] };

        // The truncated (last) line ends with the ellipsis, drawn after fWidth.
        const SkScalar width { line.fWidth + (i + 1 == lines.size() ? m_ellipsisWidth : 0.f) };

        rects.push_back(SkRect::MakeXYWH(
            line.fLeft,
            line.fBaseline - line.fAscent,
            width + 2 * DESK_LABEL_PADDING_X,
            line.fHeight + 2.f));
    }

    return rects;
}

SkRect ItemLabel::textBounds() const noexcept
{
    SkRect bounds { SkRect::MakeEmpty() };

    for (const SkRect &rect : lineRects())
        bounds.join(rect);

    return bounds;
}
