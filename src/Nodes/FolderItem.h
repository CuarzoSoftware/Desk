#ifndef FOLDERITEM_H
#define FOLDERITEM_H

#include <Core/Types.h>
#include <Core/Desk.h>
#include <Nodes/ItemLabel.h>
#include <Nodes/IconView.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <string>

/**
 * @brief A file of a folder window (see FolderWindow): its icon (or picture thumbnail) over its
 *        name, in a cell of the window's icon grid, like Finder's icon view.
 *
 * Icons are themed icons drawn by the AssetBroker with a faint drop shadow (Assets::WindowShadow, see
 * IconView), at the exact size they take on each screen. Only the icon and the label's text select it.
 */
class FolderItem final : public AKContainer
{
public:
    FolderItem(const DeskEntry &entry = {}, AKNode *parent = nullptr) noexcept;
    ~FolderItem() noexcept;

    /// Size of its cell in the window's grid (every item has it).
    static SkSize CellSize() noexcept;

    const std::string &key() const noexcept { return m_entry.key; }
    const DeskEntry &entry() const noexcept { return m_entry; }

    /// Shows another file, or the same one updated (e.g. its type changed).
    void setEntry(const DeskEntry &entry) noexcept;

    void setSelected(bool selected) noexcept;
    bool selected() const noexcept { return m_selected; }

    /// Faded while it's dragged.
    void setDimmed(bool dimmed) noexcept;

    /// Highlighted (like selected) while files dragged over it would drop into it.
    void setDropTarget(bool target) noexcept;

    /// Whether files can be dropped into it (a folder or a disk).
    bool acceptsDrops() const noexcept { return isFolder(); }

    /// Whether it's a folder (or a disk): opened in the window itself.
    bool isFolder() const noexcept;

    /// Reloads the thumbnail if it's for @p path.
    void thumbnailReady(const std::string &path) noexcept;

    /// Whether @p pos (surface coordinates) is over its icon or the text of its name.
    bool contains(const SkPoint &pos) const noexcept;

    /// Whether @p pos (surface coordinates) is over the text of its name.
    bool nameContains(const SkPoint &pos) const noexcept;

    AKRoundSolidColor iconBox { 0x00000000, CZBorderRadius::Make(6), this };
    IconView icon { &iconBox, Assets::WindowShadow };
    ItemLabel label { this, ItemLabel::Style::Window };

protected:
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;

private:
    void updateImage() noexcept;
    void updateSelectionColor() noexcept;
    void cancelThumbnail() noexcept;
    DeskEntry m_entry;
    std::filesystem::path m_waitingThumbnail; // Requested, not ready yet (see Assets::thumbnail())
    bool m_selected { false }, m_dropTarget { false };
};

#endif // FOLDERITEM_H
