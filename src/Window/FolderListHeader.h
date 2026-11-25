#ifndef FOLDERLISTHEADER_H
#define FOLDERLISTHEADER_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKSolidColor.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKText.h>
#include <CZ/AK/Nodes/AKFontIcon.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/CZSignal.h>
#include <optional>

/**
 * @brief The column titles above a folder window's list (Name, Date Modified, Size, Kind), like
 *        Finder's list view.
 *
 * Its columns line up with the rows' (see FolderListRow). The column the files are sorted by shows
 * an arrow (up: ascending). Clicking a title sorts by it (see FolderWindow).
 *
 * Each title but the name's starts with a small divider (its left edge, part of the title: it
 * moves with it), then its text (DESK_LIST_TEXT_INSET from the column's edge, like the rows').
 * Dragging a divider resizes the column at its left: Name, Date or Size (the ones
 * at its right move along, so it follows the pointer; the pointer is grabbed meanwhile: the
 * cursor stays the resize one). The name column keeps at least DESK_LIST_NAME_MIN, the others
 * DESK_LIST_COLUMN_MIN. The columns may be wider than the window: the header is as wide as them
 * (see ContentWidth()) and is scrolled with the list (see FolderWindow).
 */
class FolderListHeader final : public AKSolidColor
{
public:
    enum class Column { Name, Date, Size, Kind };

    /// @p inset: the list's content padding (the rows start after it).
    FolderListHeader(SkScalar inset, AKNode *parent = nullptr) noexcept;

    /// Shows the arrow on @p column.
    void setSort(Column column, bool ascending) noexcept;

    /// Lays the titles (and dividers) out for @p columns.
    void setColumns(const DeskListColumns &columns) noexcept;
    const DeskListColumns &columns() const noexcept { return m_columns; }

    /// The width of the header, and of the list's content, for @p columns (@p inset: the list's padding).
    static SkScalar ContentWidth(const DeskListColumns &columns, SkScalar inset) noexcept;

    /// Ends a column resize in progress, if any (e.g. the list is hidden).
    void endResize() noexcept;

    /// The title under @p pos (scene coordinates), if any (none over a divider).
    std::optional<Column> columnAt(const SkPoint &pos) const noexcept;

    /// A divider is being dragged: the new widths (already shown). @p done: released.
    CZSignal<const DeskListColumns&, bool> onColumnsResized;

    // The line at the left edge of a title, inside a wider area that can be dragged (reaching into
    // the gap before it)
    class Divider final : public AKContainer
    {
    public:
        Divider(FolderListHeader &header, Column column, AKNode *parent) noexcept;
        FolderListHeader &header;
        const Column column; // The one it resizes (at its left)
        AKSolidColor line { AKTheme::Separator, this };
        ~Divider() noexcept;

        /// Ends a drag (resizing): the pointer grab is released. @p done: the width is final.
        void endDrag(bool done = true) noexcept;
        bool dragging() const noexcept { return m_dragging; }
    protected:
        void pointerButtonEvent(const CZPointerButtonEvent &e) override;
        void pointerMoveEvent(const CZPointerMoveEvent &e) override;
    private:
        bool m_dragging { false };
        SkScalar m_startX { 0.f };
        Int32 m_startWidth { 0 };
    };

    struct Title
    {
        Title(const char *text, FolderListHeader &header, Column column) noexcept;
        AKContainer box;
        Divider divider; // Resizes the column before (hidden for the name, the first one)
        AKText label;
        AKFontIcon arrow { "", 14, &box };
    };

    Title name { "Name", *this, Column::Name };
    Title date { "Date Modified", *this, Column::Date };
    Title size { "Size", *this, Column::Size };
    Title kind { "Kind", *this, Column::Kind };
    AKSolidColor bottomLine; // Separator below the titles

protected:
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;

private:
    Title *title(Column column) noexcept;
    Int32 &width(DeskListColumns &columns, Column column) const noexcept;

    /// Resizes @p column to @p width (within the limits), see Divider.
    void resize(Column column, Int32 width, bool done) noexcept;

    SkScalar m_padding; // Left and right (the list's inset and the rows' padding)
    DeskListColumns m_columns;
};

#endif // FOLDERLISTHEADER_H
