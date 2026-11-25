#include <Nodes/FolderListRow.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <CZ/Core/Events/CZWindowStateEvent.h>
#include <CZ/Ream/RImage.h>

FolderListRow::FolderListRow(AKNode *parent) noexcept :
    AKRoundSolidColor(0, CZBorderRadius::Make(5), parent)
{
    // Clicks are handled by the window (see FolderWindow::pointerButtonEvent())
    const SkRegion empty;

    layout().setFlexDirection(YGFlexDirectionRow);
    layout().setAlignItems(YGAlignCenter);
    layout().setPadding(YGEdgeHorizontal, DESK_LIST_PADDING);
    layout().setGap(YGGutterColumn, DESK_LIST_GAP);

    icon.setInputRegion(&empty);
    icon.layout().setWidth(DESK_LIST_ICON_SIZE);
    icon.layout().setHeight(DESK_LIST_ICON_SIZE);
    icon.layout().setFlexShrink(0.f);

    // Fixed widths (the header's, see setColumns()), the rest of the row empty; each one clips its text
    for (AKContainer *box : { &nameBox, &dateBox, &sizeBox, &kindBox })
    {
        box->setInputRegion(&empty);
        box->enableChildrenClipping(true);
        box->layout().setAlignItems(YGAlignCenter);
        box->layout().setHeightPercent(100.f);
        box->layout().setFlexShrink(0.f);
    }

    setColumns(DeskListColumns {});

    sizeBox.layout().setJustifyContent(YGJustifyFlexEnd); // Sizes line up on the right

    // Like the header's titles: their text after the divider at their left edge (see FolderListHeader)
    dateBox.layout().setPadding(YGEdgeLeft, DESK_LIST_TEXT_INSET);
    kindBox.layout().setPadding(YGEdgeLeft, DESK_LIST_TEXT_INSET);
    sizeBox.layout().setPadding(YGEdgeRight, 16.f);

    for (AKText *text : { &name, &date, &size, &kind })
    {
        text->setInputRegion(&empty);
        text->setFontSize(DESK_LIST_FONT_SIZE);
        text->layout().setFlexShrink(0.f);
    }

    // Long names end with "…" (as wide as their column)
    name.setTruncates(true);
    name.layout().setFlexShrink(1.f);

    updateColors();
}

void FolderListRow::setColumns(const DeskListColumns &columns) noexcept
{
    // Unchanged values don't lay the row out again. The name column includes the icon.
    nameBox.layout().setWidth(std::max(1, columns.name - DESK_LIST_ICON_SIZE - DESK_LIST_GAP));
    dateBox.layout().setWidth(columns.date);
    sizeBox.layout().setWidth(columns.size);
    kindBox.layout().setWidth(columns.kind);
}

void FolderListRow::setEntry(const DeskEntry &entry) noexcept
{
    const bool imageChanged { entry.path != m_entry.path || entry.mimeType != m_entry.mimeType || entry.launcher != m_entry.launcher };
    m_entry = entry;

    name.setText(entry.name);
    date.setText(Desk::FormatDate(entry.modified));
    const bool folder { entry.kind == DeskEntry::Kind::Volume || entry.mimeType == "inode/directory" };
    size.setText(folder ? "--" : Desk::FormatBytes(entry.size));
    // Capitalized, like Finder's (descriptions may start in lowercase, e.g. in Spanish)
    std::string kindName { entry.kindName.empty() ? (folder ? "Folder" : "Document") : entry.kindName };

    if (!kindName.empty() && kindName.front() >= 'a' && kindName.front() <= 'z')
        kindName.front() = char(kindName.front() - 'a' + 'A');

    kind.setText(kindName);

    if (!imageChanged)
        return;

    // The themed icon at the exact size it's drawn at (through the AssetBroker, see Assets)
    if (auto image { GetDesk()->assets.icon(entry) })
        icon.setImageProvider([image](Int32 pixels, Int32 scale){ return GetDesk()->assets.sized(image, pixels, scale); });
    else
        icon.setImage(nullptr);
}

bool FolderListRow::nameContains(const SkPoint &pos) const noexcept
{
    // The name's text, as far as its column shows it
    SkIRect text { name.worldRect() };

    if (!text.intersect(nameBox.worldRect()))
        text.setEmpty();

    return SkRect::Make(icon.worldRect()).contains(pos.x(), pos.y()) || SkRect::Make(text).contains(pos.x(), pos.y());
}

void FolderListRow::setSelected(bool selected) noexcept
{
    if (m_selected == selected)
        return;

    m_selected = selected;
    updateColors();
}

void FolderListRow::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKRoundSolidColor::colorSchemeEvent(e);
    updateColors();
}

void FolderListRow::windowStateEvent(const CZWindowStateEvent &e)
{
    AKRoundSolidColor::windowStateEvent(e);
    updateColors();
}

void FolderListRow::setDimmed(bool dimmed) noexcept
{
    const Float32 opacity { dimmed ? 0.5f : 1.f };

    icon.setOpacity(opacity);

    for (AKText *text : { &name, &date, &size, &kind })
        text->setOpacity(opacity);
}

void FolderListRow::setDropTarget(bool target) noexcept
{
    if (m_dropTarget == target)
        return;

    m_dropTarget = target;
    updateColors();
}

void FolderListRow::updateColors() noexcept
{
    const CZColorScheme scheme { colorScheme() };
    const bool highlighted { m_selected || m_dropTarget };
    const bool emphasized { highlighted && activated() };

    // Like Finder: the accent color while the window is active, gray otherwise
    if (!highlighted)
        setBackgroundColor(SK_ColorTRANSPARENT);
    else if (emphasized)
        setBackgroundColor(AKTheme::SelectedContentBackground.resolve(scheme));
    else
        setBackgroundColor(AKTheme::UnemphasizedSelectedContentBackground.resolve(scheme));

    name.setTextColor(emphasized ? AKTheme::White : AKTheme::Label);

    for (AKText *text : { &date, &size, &kind })
        text->setTextColor(emphasized ? AKTheme::White : AKTheme::SecondaryLabel);
}
