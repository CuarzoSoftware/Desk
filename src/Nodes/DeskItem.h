#ifndef DESKITEM_H
#define DESKITEM_H

#include <Core/Types.h>
#include <Nodes/ItemLabel.h>
#include <Nodes/IconView.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <Core/Desk.h>
#include <string>

/**
 * @brief A desktop icon: the icon (or picture thumbnail) over its name, in a grid cell.
 *
 * Only the icon and the label's text take the pointer, so clicks on the rest of the cell go to the
 * desktop (e.g. starting a rubber band selection).
 */
class DeskItem final : public AKContainer
{
public:
    DeskItem(const DeskEntry &entry, Int32 iconSize, AKNode *parent = nullptr) noexcept;

    /// Grid cell size for an icon size (shared by every item).
    static SkISize CellSize(Int32 iconSize) noexcept;

    const std::string &key() const noexcept { return m_entry.key; }
    const DeskEntry &entry() const noexcept { return m_entry; }

    /// Updates the name/icon (same key, e.g. a launcher whose name changed).
    void setEntry(const DeskEntry &entry) noexcept;

    void setIconSize(Int32 iconSize) noexcept;
    Int32 iconSize() const noexcept { return m_iconSize; }

    void setSelected(bool selected) noexcept;
    bool selected() const noexcept { return m_selected; }

    /// Dims the item (while it's being dragged).
    void setDimmed(bool dimmed) noexcept;

    /// Whether files can be dropped on it: folders and disks (see Background::dragOver()).
    bool acceptsDrops() const noexcept;

    /// Something droppable is over it: highlighted, and folders show the open folder icon.
    void setDropTarget(bool target) noexcept;
    bool dropTarget() const noexcept { return m_dropTarget; }

    /// Reloads the thumbnail if it's for @p path.
    void thumbnailReady(const std::string &path) noexcept;

    /// The grid cell it's placed at.
    SkIPoint cell {};

    /// Areas that select it (icon and label text), in surface coordinates.
    SkIRect iconRect() const noexcept { return iconBox.worldRect(); }
    SkRect labelRect() const noexcept;

    AKRoundSolidColor iconBox { 0x00000000, CZBorderRadius::Make(6), this };
    IconView icon { &iconBox };
    ItemLabel label { this };

private:
    void updateImage() noexcept;
    void updateLabelInput() noexcept;
    DeskEntry m_entry;
    Int32 m_iconSize { DESK_ICON_SIZE_DEFAULT };
    bool m_selected { false };
    bool m_dropTarget { false };
};

#endif // DESKITEM_H
