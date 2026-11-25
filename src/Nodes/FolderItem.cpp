#include <Nodes/FolderItem.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <CZ/Ream/RImage.h>
#include <cmath>

SkSize FolderItem::CellSize() noexcept
{
    // The icon's box, the gap and two lines of text (see ItemLabel)
    const Int32 box { DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING };
    const Int32 lineHeight { Int32(std::ceil(DESK_WINDOW_LABEL_FONT_SIZE * 1.3f)) };
    return SkSize::Make(DESK_WINDOW_CELL_WIDTH, box + DESK_LABEL_GAP + DESK_LABEL_LINES * lineHeight + 2);
}

FolderItem::FolderItem(const DeskEntry &entry, AKNode *parent) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    m_entry(entry)
{
    // Clicks are handled by the window (see FolderWindow::pointerButtonEvent())

    layout().setAlignItems(YGAlignCenter);
    layout().setWidth(DESK_WINDOW_CELL_WIDTH);
    layout().setFlexShrink(0.f);

    const Int32 box { DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING };
    iconBox.layout().setWidth(box);
    iconBox.layout().setHeight(box);
    iconBox.layout().setAlignItems(YGAlignCenter);
    iconBox.layout().setJustifyContent(YGJustifyCenter);
    iconBox.layout().setMargin(YGEdgeBottom, DESK_LABEL_GAP);
    icon.layout().setWidth(DESK_WINDOW_ICON_SIZE);
    icon.layout().setHeight(DESK_WINDOW_ICON_SIZE);

    label.setWidth(DESK_WINDOW_CELL_WIDTH);
    label.setText(entry.name);

    // A cell created empty shows nothing until it's given a file (see FolderWindow)
    if (!entry.path.empty())
        updateImage();
}

void FolderItem::setEntry(const DeskEntry &entry) noexcept
{
    // Another file (a cell reused while scrolling) or another type: the image changes
    const bool imageChanged { entry.path != m_entry.path || entry.mimeType != m_entry.mimeType || entry.launcher != m_entry.launcher };
    m_entry = entry;
    label.setText(entry.name);

    if (imageChanged)
        updateImage();
}

void FolderItem::setSelected(bool selected) noexcept
{
    if (m_selected == selected)
        return;

    m_selected = selected;
    label.setSelected(selected);
    updateSelectionColor();
}

bool FolderItem::isFolder() const noexcept
{
    return m_entry.kind == DeskEntry::Kind::Volume || (!m_entry.launcher && m_entry.mimeType == "inode/directory");
}

void FolderItem::updateSelectionColor() noexcept
{
    iconBox.setBackgroundColor(m_selected ? DESK_WINDOW_ICON_SELECTION.resolve(colorScheme()) : 0x00000000);
}

void FolderItem::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKContainer::colorSchemeEvent(e);
    updateSelectionColor();
}

void FolderItem::updateImage() noexcept
{
    auto &assets { GetDesk()->assets };

    if (m_entry.kind == DeskEntry::Kind::File && !m_entry.launcher && Assets::HasThumbnail(m_entry.mimeType))
    {
        // Loaded at 2x so they stay sharp on HiDPI screens (decoded in the background).
        if (auto thumb { assets.thumbnail(m_entry.path, DESK_WINDOW_ICON_SIZE * 2) })
        {
            icon.setImage(thumb);
            return;
        }
    }

    // The themed icon, drawn with its shadow (through the AssetBroker) at the exact size it's drawn at.
    icon.setImage(assets.icon(m_entry));
}

void FolderItem::thumbnailReady(const std::string &path) noexcept
{
    if (m_entry.path.string() == path)
        updateImage();
}

bool FolderItem::contains(const SkPoint &pos) const noexcept
{
    if (SkRect::Make(iconBox.worldRect()).contains(pos.x(), pos.y()))
        return true;

    SkRect text { label.textBounds() };
    text.offset(label.worldRect().x(), label.worldRect().y());
    return text.contains(pos.x(), pos.y());
}
