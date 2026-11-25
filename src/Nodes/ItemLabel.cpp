#include <Nodes/ItemLabel.h>
#include <Core/Desk.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Ream/RImage.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <CZ/Core/Events/CZWindowStateEvent.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkRRect.h>
#include <CZ/skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <algorithm>
#include <cmath>

using namespace skia::textlayout;

// Looks (see m_looks)
static constexpr int LookNormal { 0 }, LookSelected { 1 }, LookSelectedInactive { 2 };

ItemLabel::ItemLabel(AKNode *parent, Style style) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    m_style(style)
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

SkScalar ItemLabel::fontSize() const noexcept
{
    return m_style == Style::Desktop ? DESK_LABEL_FONT_SIZE : DESK_WINDOW_LABEL_FONT_SIZE;
}

SkFontStyle ItemLabel::fontStyle() const noexcept
{
    return SkFontStyle(m_style == Style::Desktop ? SkFontStyle::kMedium_Weight : SkFontStyle::kNormal_Weight,
                       SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant);
}

int ItemLabel::lookIndex() const noexcept
{
    if (!m_selected)
        return LookNormal;

    // Like Finder: a gray highlight while the window isn't the active one
    return m_style == Style::Window && !activated() ? LookSelectedInactive : LookSelected;
}

std::unique_ptr<Paragraph> ItemLabel::buildParagraph(int look) const noexcept
{
    ParagraphStyle paragraphStyle;
    paragraphStyle.setTextAlign(TextAlign::kCenter);
    paragraphStyle.setTextDirection(TextDirection::kLtr);
    paragraphStyle.setMaxLines(DESK_LABEL_LINES);
    paragraphStyle.setEllipsis(u"…");

    TextStyle style { theme()->DefaultTextStyle };
    style.setFontSize(fontSize());
    style.setFontStyle(fontStyle());

    SkColor color { DESK_LABEL_COLOR };

    if (m_style == Style::Window && look != LookSelected)
        color = AKTheme::Label.resolve(colorScheme());

    // The theme's style has a foreground paint, which overrides setColor().
    SkPaint paint;
    paint.setColor(color);
    paint.setAntiAlias(true);
    style.setColor(color);
    style.setForegroundPaint(paint);
    style.resetShadows();

    // Readable over any picture: a tight dark shadow plus a soft halo (none when highlighted).
    if (m_style == Style::Desktop && look == LookNormal)
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
    // The layout is the same for every look (colors and shadows don't change it).
    m_paragraph = buildParagraph(LookSelected);
    m_size = { m_width, Int32(std::ceil(m_paragraph->getHeight())) + 2 };

    // Skia's line metrics leave the ellipsis out of the line width: measure it to highlight it too.
    m_ellipsisWidth = 0.f;

    if (m_paragraph->didExceedMaxLines())
    {
        ParagraphStyle paragraphStyle;
        TextStyle style { theme()->DefaultTextStyle };
        style.setFontSize(fontSize());
        style.setFontStyle(fontStyle());
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
    dropLooks();
}

void ItemLabel::dropLooks() noexcept
{
    for (auto &look : m_looks)
        look.reset();

    showLook();
}

void ItemLabel::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKContainer::colorSchemeEvent(e);

    if (m_style == Style::Window)
        dropLooks();
}

void ItemLabel::windowStateEvent(const CZWindowStateEvent &e)
{
    AKContainer::windowStateEvent(e);

    if (m_style == Style::Window && m_selected)
        showLook();
}

void ItemLabel::LookFrame::onSceneBegin()
{
    label.checkScale(AKNode::scale());
    AKImageFrame::onSceneBegin();
}

void ItemLabel::checkScale(Int32 scale) noexcept
{
    if (scale <= 0 || scale == m_looksScale)
        return;

    m_looksScale = scale;
    dropLooks();
}

void ItemLabel::showLook() noexcept
{
    // At the scale of its screen (see checkScale()). Not known before it's first shown: the largest
    // screen scale until then.
    if (m_looksScale <= 0)
        for (MScreen *s : GetDesk()->marco->screens())
            m_looksScale = std::max(m_looksScale, s->props().scale);

    m_looksScale = std::max(m_looksScale, 1);

    const int index { lookIndex() };
    auto &look { m_looks[index] };

    if (!look)
        look = renderLook(index);

    m_frame.setImage(look);
}

std::shared_ptr<RImage> ItemLabel::renderLook(int look) noexcept
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

        if (look != LookNormal)
        {
            // One rounded highlight per line, like Finder.
            SkPaint paint;
            paint.setAntiAlias(true);

            if (m_style == Style::Desktop)
                paint.setColor(DESK_LABEL_SELECTION);
            else if (look == LookSelected)
                paint.setColor(AKTheme::SelectedContentBackground.resolve(colorScheme()));
            else
                paint.setColor(AKTheme::UnemphasizedSelectedContentBackground.resolve(colorScheme()));

            for (const SkRect &rect : lineRects())
                c->drawRRect(SkRRect::MakeRectXY(rect, 4.f, 4.f), paint);
        }

        if (look == LookSelected)
            m_paragraph->paint(c, DESK_LABEL_PADDING_X, 1.f);
        else
            buildParagraph(look)->paint(c, DESK_LABEL_PADDING_X, 1.f);
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
