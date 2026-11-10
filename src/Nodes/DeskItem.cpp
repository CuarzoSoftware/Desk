#include <Nodes/DeskItem.h>
#include <Core/Desk.h>
#include <CZ/Ream/RImage.h>
#include <cmath>

DeskItem::DeskItem(const DeskEntry &entry, Int32 iconSize, AKNode *parent) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    m_entry(entry)
{
    // Only the icon and the label's text take input (see updateLabelInput()).
    SkRegion empty;
    setInputRegion(&empty);

    layout().setPositionType(YGPositionTypeAbsolute);
    layout().setAlignItems(YGAlignCenter);

    iconBox.layout().setAlignItems(YGAlignCenter);
    iconBox.layout().setJustifyContent(YGJustifyCenter);
    iconBox.layout().setMargin(YGEdgeBottom, DESK_LABEL_GAP);

    label.setText(entry.name);
    setIconSize(iconSize);
    updateImage();
}

SkISize DeskItem::CellSize(Int32 iconSize) noexcept
{
    const Int32 box { iconSize + 2 * DESK_ICON_PADDING };
    const Int32 lineHeight { Int32(std::ceil(DESK_LABEL_FONT_SIZE * 1.3f)) };
    const Int32 labelHeight { DESK_LABEL_LINES * lineHeight + 2 };
    return { std::max(box + 32, 76), box + DESK_LABEL_GAP + labelHeight };
}

void DeskItem::setEntry(const DeskEntry &entry) noexcept
{
    const bool imageChanged { entry.mimeType != m_entry.mimeType || entry.launcher != m_entry.launcher };
    m_entry = entry;
    label.setText(entry.name);
    updateLabelInput();

    if (imageChanged)
        updateImage();
}

void DeskItem::setIconSize(Int32 iconSize) noexcept
{
    m_iconSize = iconSize;
    const SkISize cell { CellSize(iconSize) };
    const Int32 box { iconSize + 2 * DESK_ICON_PADDING };

    layout().setWidth(cell.width());
    layout().setHeight(cell.height());
    iconBox.layout().setWidth(box);
    iconBox.layout().setHeight(box);
    icon.layout().setWidth(iconSize);
    icon.layout().setHeight(iconSize);
    label.setWidth(cell.width());
    updateLabelInput();
    updateImage();
}

void DeskItem::setSelected(bool selected) noexcept
{
    if (m_selected == selected)
        return;

    m_selected = selected;
    GetDesk()->selectionChanged();
    iconBox.setBackgroundColor(selected ? DESK_ICON_SELECTION : 0x00000000);
    label.setSelected(selected);
    updateLabelInput();
}

void DeskItem::setDimmed(bool dimmed) noexcept
{
    icon.setOpacity(dimmed ? 0.5f : 1.f);
    iconBox.setOpacity(dimmed ? 0.5f : 1.f);
    label.setOpacity(dimmed ? 0.5f : 1.f);
}

void DeskItem::updateImage() noexcept
{
    auto &assets { GetDesk()->assets };

    if (m_entry.kind == DeskEntry::Kind::File && !m_entry.launcher && Assets::HasThumbnail(m_entry.mimeType))
    {
        // Thumbnails are loaded at 2x so they stay sharp on HiDPI screens.
        if (auto thumb { assets.thumbnail(m_entry.path, m_iconSize * 2) })
        {
            icon.setImage(thumb);
            return;
        }
    }

    icon.setImage(assets.icon(m_entry));
}

void DeskItem::thumbnailReady(const std::string &path) noexcept
{
    if (m_entry.path.string() == path)
        updateImage();
}

void DeskItem::updateLabelInput() noexcept
{
    const SkRect text { label.textBounds() };
    const SkRegion region { text.roundOut() };
    label.setInputRegion(&region);
}

SkRect DeskItem::labelRect() const noexcept
{
    SkRect text { label.textBounds() };
    text.offset(label.worldRect().x(), label.worldRect().y());
    return text;
}
