#include <Window/FolderListHeader.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKScene.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZPointerMoveEvent.h>
#include <linux/input-event-codes.h>
#include <algorithm>

// A divider's draggable area: as wide, reaching this far into the gap before its title (the line
// is in its middle, at the title's left edge)
static constexpr SkScalar DividerHitWidth { 9.f };
static constexpr SkScalar DividerOutside { 4.f };

// The column before @p column (its title's divider resizes it)
static FolderListHeader::Column Previous(FolderListHeader::Column column) noexcept
{
    using Column = FolderListHeader::Column;

    switch (column)
    {
    case Column::Date: return Column::Name;
    case Column::Size: return Column::Date;
    case Column::Kind: return Column::Size;
    default: return Column::Name;
    }
}

FolderListHeader::Title::Title(const char *text, FolderListHeader &header, Column column) noexcept :
    box(YGFlexDirectionRow, false, &header),
    divider(header, Previous(column), &box),
    label(text, &box)
{
    const SkRegion empty;
    label.setInputRegion(&empty);
    arrow.setInputRegion(&empty);
    box.layout().setAlignItems(YGAlignCenter);
    box.layout().setHeightPercent(100.f);
    box.layout().setFlexShrink(0.f);
    label.setFontSize(12.f);
    label.setFontStyle(SkFontStyle(SkFontStyle::kSemiBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    label.setTextColor(AKTheme::SecondaryLabel);
    label.setTruncates(true); // "Date Modi…" in a narrow column (titles don't clip)
    label.layout().setFlexShrink(1.f);
    arrow.layout().setFlexShrink(0.f);
    arrow.layout().setMargin(YGEdgeLeft, 2.f);
    arrow.setVisible(false);

    // The name has no divider (its column isn't resized): its text at the column's edge, the
    // others' after their divider, like the rows' (see FolderListRow)
    if (column == Column::Name)
        divider.setVisible(false);
    else
        label.layout().setMargin(YGEdgeLeft, DESK_LIST_TEXT_INSET - (DividerHitWidth - DividerOutside));
}

FolderListHeader::Divider::Divider(FolderListHeader &header, Column column, AKNode *parent) noexcept :
    AKContainer(YGFlexDirectionRow, false, parent),
    header(header),
    column(column)
{
    layout().setWidth(DividerHitWidth);
    layout().setHeightPercent(100.f);
    layout().setFlexShrink(0.f);
    layout().setMargin(YGEdgeLeft, -DividerOutside);
    layout().setJustifyContent(YGJustifyCenter);
    layout().setAlignItems(YGAlignCenter);
    setCursor(CZCursorShape::ResizeColumn);

    const SkRegion empty;
    line.setInputRegion(&empty);
    line.layout().setWidth(1.f);
    line.layout().setHeight(14.f);
}

void FolderListHeader::Divider::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    AKContainer::pointerButtonEvent(e);

    if (e.button != BTN_LEFT)
        return;

    if (e.pressed)
    {
        // Followed while the button is held, even outside it
        m_dragging = true;
        m_startX = AKApp::Get()->pointer().pos().x();
        DeskListColumns columns { header.m_columns };
        m_startWidth = header.width(columns, column);

        if (scene())
            scene()->setPointerGrab(this);
    }
    else
        endDrag();
}

FolderListHeader::Divider::~Divider() noexcept
{
    endDrag(false);
}

void FolderListHeader::Divider::endDrag(bool done) noexcept
{
    if (!m_dragging)
        return;

    // Released (the node under the pointer gets its cursor back right away, see AKScene::setPointerGrab())
    m_dragging = false;

    if (scene() && scene()->pointerGrab() == this)
        scene()->setPointerGrab(nullptr);

    if (!done)
        return;

    DeskListColumns columns { header.m_columns };
    header.resize(column, header.width(columns, column), true);
}

void FolderListHeader::endResize() noexcept
{
    for (Title *t : { &date, &size, &kind })
        t->divider.endDrag();
}

void FolderListHeader::Divider::pointerMoveEvent(const CZPointerMoveEvent &e)
{
    AKContainer::pointerMoveEvent(e);

    // Its column is at its left: dragging it right widens it
    if (m_dragging)
        header.resize(column, m_startWidth + Int32(std::round(AKApp::Get()->pointer().pos().x() - m_startX)), false);
}

FolderListHeader::FolderListHeader(SkScalar inset, AKNode *parent) noexcept :
    AKSolidColor(AKTheme::WindowBackground, parent),
    bottomLine(AKTheme::Separator, this),
    m_padding(inset + DESK_LIST_PADDING)
{
    layout().setFlexDirection(YGFlexDirectionRow);
    layout().setAlignItems(YGAlignCenter);
    layout().setHeight(DESK_LIST_HEADER_HEIGHT);
    layout().setFlexShrink(0.f);

    // Like a row (see FolderListRow): the name over the icon and the name, then the fixed columns
    layout().setPadding(YGEdgeHorizontal, m_padding);
    layout().setGap(YGGutterColumn, DESK_LIST_GAP);

    // Every column as wide as chosen (the header too, see setColumns())
    layout().setWidthPercent(100.f);
    // Sizes line up on the right (like the rows'), its divider still at the column's left edge
    size.label.layout().setMarginAuto(YGEdgeLeft);
    size.box.layout().setPadding(YGEdgeRight, 16.f);

    const SkRegion empty;
    bottomLine.setInputRegion(&empty);
    bottomLine.layout().setPositionType(YGPositionTypeAbsolute);
    bottomLine.layout().setPosition(YGEdgeLeft, 0.f);
    bottomLine.layout().setPosition(YGEdgeBottom, 0.f);
    bottomLine.layout().setWidthPercent(100.f);
    bottomLine.layout().setHeight(1.f);

    m_columns.date = -1; // Laid out for sure
    setColumns(DeskListColumns {});
}

SkScalar FolderListHeader::ContentWidth(const DeskListColumns &columns, SkScalar inset) noexcept
{
    // The padding, the columns and the gaps between them
    return 2.f * (inset + DESK_LIST_PADDING) + SkScalar(columns.name + columns.date + columns.size + columns.kind)
           + 3.f * DESK_LIST_GAP;
}

Int32 &FolderListHeader::width(DeskListColumns &columns, Column column) const noexcept
{
    switch (column)
    {
    case Column::Name: return columns.name;
    case Column::Date: return columns.date;
    case Column::Size: return columns.size;
    default: return columns.kind;
    }
}

void FolderListHeader::setColumns(const DeskListColumns &columns) noexcept
{
    if (columns == m_columns)
        return;

    m_columns = columns;
    name.box.layout().setWidth(columns.name);
    date.box.layout().setWidth(columns.date);
    size.box.layout().setWidth(columns.size);
    kind.box.layout().setWidth(columns.kind);

    // As wide as the window, or wider (scrolled with the list)
    layout().setMinWidth(ContentWidth(columns, m_padding - DESK_LIST_PADDING));
}

void FolderListHeader::resize(Column column, Int32 newWidth, bool done) noexcept
{
    // The columns at its right move along (the list scrolls horizontally if they don't fit)
    DeskListColumns columns { m_columns };
    Int32 &w { width(columns, column) };
    w = std::max(newWidth, column == Column::Name ? DESK_LIST_NAME_MIN : DESK_LIST_COLUMN_MIN);

    if (columns == m_columns && !done)
        return;

    setColumns(columns);
    onColumnsResized.notify(m_columns, done);
}

void FolderListHeader::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKSolidColor::colorSchemeEvent(e);

    for (Title *t : { &name, &date, &size, &kind })
        t->arrow.setColor(AKTheme::Label.resolve(colorScheme()));
}

FolderListHeader::Title *FolderListHeader::title(Column column) noexcept
{
    switch (column)
    {
    case Column::Name: return &name;
    case Column::Date: return &date;
    case Column::Size: return &size;
    case Column::Kind: return &kind;
    }

    return &name;
}

void FolderListHeader::setSort(Column column, bool ascending) noexcept
{
    for (Title *t : { &name, &date, &size, &kind })
    {
        const bool sorted { t == title(column) };
        t->arrow.setVisible(sorted);
        t->label.setTextColor(sorted ? AKTheme::Label : AKTheme::SecondaryLabel);

        if (sorted)
        {
            t->arrow.setIconFromName(ascending ? "expand_less" : "expand_more");
            t->arrow.setColor(AKTheme::Label.resolve(colorScheme()));
        }
    }
}

std::optional<FolderListHeader::Column> FolderListHeader::columnAt(const SkPoint &pos) const noexcept
{
    if (!visible() || !SkRect::Make(worldRect()).contains(pos.x(), pos.y()))
        return std::nullopt;

    // Dragged to resize, not clicked to sort
    for (const Divider *d : { &date.divider, &size.divider, &kind.divider })
        if (SkRect::Make(d->worldRect()).contains(pos.x(), pos.y()))
            return std::nullopt;

    const std::pair<const Title*, Column> titles[] {
        { &name, Column::Name }, { &date, Column::Date }, { &size, Column::Size }, { &kind, Column::Kind } };

    // Each title up to the next one (the gaps belong to the one on their left)
    for (const auto &[t, column] : titles)
        if (pos.x() < SkScalar(t->box.worldRect().right()) + DESK_LIST_GAP)
            return column;

    return Column::Kind;
}
