#ifndef FOLDERLISTROW_H
#define FOLDERLISTROW_H

#include <Core/Types.h>
#include <Core/Desk.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/AK/Nodes/AKText.h>

/**
 * @brief A file in the list view of a folder window (see FolderWindow), like a row of Finder's list
 *        view: its icon and name, then its modification date, size and kind.
 *
 * The columns line up with the window's list header (DESK_LIST_* widths). Selected, the whole row
 * is highlighted with the accent color (gray while the window isn't active) and its text is white.
 * A cell of an AKListView: rebound to other files while scrolling (setEntry()).
 */
class FolderListRow final : public AKRoundSolidColor
{
public:
    FolderListRow(AKNode *parent = nullptr) noexcept;

    /// Shows another file, or the same one updated.
    void setEntry(const DeskEntry &entry) noexcept;
    const DeskEntry &entry() const noexcept { return m_entry; }

    void setSelected(bool selected) noexcept;

    /// Faded while it's dragged.
    void setDimmed(bool dimmed) noexcept;

    /// Highlighted (like selected) while files dragged over it would drop into it.
    void setDropTarget(bool target) noexcept;

    /// Whether @p pos (surface coordinates) is over its icon or the text of its name.
    bool nameContains(const SkPoint &pos) const noexcept;
    bool selected() const noexcept { return m_selected; }

    /// Widths of the columns, like the header's (the rest of the row is empty).
    void setColumns(const DeskListColumns &columns) noexcept;

    AKImageFrame icon { this };
    AKContainer nameBox { YGFlexDirectionRow, false, this }; // Clips the name
    AKText name { "", &nameBox };
    AKContainer dateBox { YGFlexDirectionRow, false, this };
    AKText date { "", &dateBox };
    AKContainer sizeBox { YGFlexDirectionRow, false, this };
    AKText size { "", &sizeBox };
    AKContainer kindBox { YGFlexDirectionRow, false, this };
    AKText kind { "", &kindBox };

protected:
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;
    void windowStateEvent(const CZWindowStateEvent &e) override;

private:
    void updateColors() noexcept;
    DeskEntry m_entry;
    bool m_selected { false }, m_dropTarget { false };
};

#endif // FOLDERLISTROW_H
