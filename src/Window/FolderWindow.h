#ifndef FOLDERWINDOW_H
#define FOLDERWINDOW_H

#include <Core/Types.h>
#include <Core/Desk.h>
#include <Nodes/FolderItem.h>
#include <Window/FolderSidebar.h>
#include <Surfaces/ContextMenu.h>
#include <CZ/Marco/Roles/MToplevel.h>
#include <CZ/AK/Nodes/AKSolidColor.h>
#include <CZ/AK/Nodes/AKGridView.h>
#include <CZ/AK/Nodes/AKListView.h>
#include <Nodes/ToolbarButton.h>
#include <Nodes/ViewPopUp.h>
#include <Nodes/FolderListRow.h>
#include <Window/FolderListHeader.h>
#include <Nodes/RenameField.h>
#include <Window/WindowMenuBar.h>
#include <Nodes/SelectionBox.h>
#include <Nodes/DragStack.h>
#include <Nodes/DragGather.h>
#include <CZ/Marco/Roles/MDND.h>
#include <CZ/Core/CZLinearAnimation.h>
#include <CZ/AK/Nodes/AKText.h>
#include <CZ/AK/Effects/AKEdgeShadow.h>
#include <CZ/Core/CZTimer.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace CZ { class CZEventSource; }

/**
 * @brief A window showing the files of a folder, like a Finder window in icon view.
 *
 * [sidebar (vibrancy, window buttons, Favorites, Locations)][toolbar (back, forward, name, view) / icons]
 *
 * Double clicking a folder (or Ctrl+Down) opens it in the same window, other files open with their
 * default apps. Back and forward go through the folders shown before, like a browser. The folder is
 * watched: the icons follow its changes. If it goes away (deleted, its disk ejected) the window
 * shows the closest folder that still exists.
 *
 * In the list, Shift+click selects the rows from the last one clicked to it, Ctrl+click adds or
 * removes a row. (In the icon view both add or remove an icon, like the desktop.)
 *
 * Keyboard (⌘ = Ctrl): arrows move the selection, Enter or ⌘↓ opens it, ⌘↑ / Backspace goes to the
 * enclosing folder, ⌘[ ⌘] / Alt+←→ go back and forward, ⌘A selects all, Delete or ⌘⌫ moves to the
 * trash, ⇧⌘N creates a folder, ⌘H shows hidden files, ⌘N opens another window and ⌘W closes it.
 * ⇧⌘H, ⇧⌘D, ⇧⌘O, ⇧⌘L and ⇧⌘C go to Home, Desktop, Documents, Downloads and the system disk.
 *
 * A file is renamed in place (see RenameField, in both views) with F2, the context menu's Rename,
 * or clicking the name of the only file selected (slowly: not a double click).
 *
 * In the icon view, dragging from the empty space between the icons selects the files the
 * rectangle touches, like the desktop (Ctrl/Shift: adding to the selection). The list has no
 * rubber band: its rows are dragged from anywhere. Scrolling meanwhile extends it, and dragging it near the view's top or bottom edge
 * (or past it) scrolls the view, faster the farther it goes.
 *
 * Files are dragged (drag & drop, like the desktop's icons) from their icon or name (a row of the
 * list from anywhere): into folders
 * and disks (of this or another window, or the desktop), to the desktop or to other apps (as
 * text/uri-list). Several fly to the pointer and stack up with a count. Files dropped here (from
 * other windows, the desktop or other apps) go into the folder or disk under the pointer, or into
 * the folder shown: moved, copied to other disks (see Desk::dropInto()).
 *
 * The files are a model (their entries and the names selected), shown as icons (an AKGridView of
 * FolderItem cells) or as a list (an AKListView of FolderListRow rows, with Finder's columns: Name,
 * Date Modified, Size and Kind), switched from the toolbar like in Finder (the last one chosen is
 * remembered). Both only create the cells in view, so folders with thousands of files scroll as
 * smoothly as small ones. Clicking a column title of the list sorts by it (again: reversed).
 */
class FolderWindow final : public MToplevel
{
public:
    FolderWindow(const std::filesystem::path &dir) noexcept;
    ~FolderWindow() noexcept;

    /// The folder shown.
    const std::filesystem::path &folder() const noexcept { return m_dir; }

    /// Shows @p dir (recorded in the history: see goBack()).
    void go(const std::filesystem::path &dir) noexcept;

    /// Selects the file named @p name (nothing if it's not here).
    void select(const std::string &name) noexcept;

    /// How folders are compared: absolute, without symlinks, "..", or trailing separators.
    static std::filesystem::path Normalized(const std::filesystem::path &dir) noexcept;
    void goBack() noexcept;
    void goForward() noexcept;
    bool canGoBack() const noexcept { return !m_backStack.empty(); }
    bool canGoForward() const noexcept { return !m_forwardStack.empty(); }

    /// Shows the enclosing folder, selecting the one shown now.
    void goUp() noexcept;

    /* Selection */
    std::vector<const DeskEntry*> selectedEntries() const noexcept;

    /// Selects only the file at @p index (none: nullopt), scrolling to it.
    void selectOnly(std::optional<size_t> index) noexcept;
    void clearSelection() noexcept;

    /// Selects only the files from the anchor (the last one clicked without Shift) to @p index,
    /// like Finder's lists. No anchor: only @p index.
    void selectRange(size_t index) noexcept;
    void selectAll() noexcept;

    /**
     * @brief Opens the selected items.
     *
     * A single folder is shown in this window (in a new one with @p newWindow). Several folders
     * open in their own windows. Files open with their default apps.
     */
    void openSelection(bool newWindow = false) noexcept;
    void trashSelection() noexcept;

    /// The selected files on the clipboard: copied, or moved if @p cut, where they're pasted (see Desk::copyFiles()).
    void copySelection(bool cut) noexcept;

    /// The clipboard's files into the folder shown (see Desk::pasteInto()).
    void paste() noexcept;

    /// Creates a new folder here and selects it.
    void newFolder() noexcept;

    /// Edits the name of the only file selected (see RenameField).
    void renameSelection() noexcept;

    /// Shows the files as a list (true) or as icons.
    void setListView(bool list) noexcept;
    bool listView() const noexcept { return m_listView; }

    using SortColumn = FolderListHeader::Column;

    /// Sorts the files by @p column (both views).
    void sortBy(SortColumn column, bool ascending) noexcept;
    SortColumn sortColumn() const noexcept { return m_sortColumn; }
    bool sortAscending() const noexcept { return m_sortAscending; }

    /// Shows the hidden files (starting with ".") too.
    void setShowsHidden(bool shows) noexcept;
    bool showsHidden() const noexcept { return m_showsHidden; }

    FolderSidebar sidebar { this };

    // Next to the sidebar: a toolbar the window is dragged by and the icons (scrolled).
    struct Content : public AKSolidColor
    {
        Content(AKNode *parent) noexcept;
        AKContainer toolbar { YGFlexDirectionRow, false, this };
        ToolbarButton back { "chevron_left", &toolbar };
        ToolbarButton forward { "chevron_right", &toolbar };
        AKText title { "", &toolbar };
        ViewPopUp viewSwitch { &toolbar }; // Grid or List
        // List view only: the column titles, scrolled horizontally with the list (in a clipping box)
        AKContainer listHeaderClip { YGFlexDirectionRow, true, this };
        FolderListHeader listHeader { DESK_WINDOW_LIST_INSET, &listHeaderClip };
        AKGridView grid { this }; // The files' icons (FolderItem cells)
        AKListView list { this }; // The files' rows (FolderListRow cells)
        SelectionBox selectionBox { this }; // Rubber band selection, over the files
        AKEdgeShadow separator { CZEdgeLeft, this }; // Over the sidebar's trailing edge

        // Under the toolbar, above the icons: shown while they're scrolled under it (like Finder)
        AKContainer toolbarShadowArea { YGFlexDirectionColumn, false, this };
        AKEdgeShadow toolbarShadow { CZEdgeBottom, &toolbarShadowArea };

        // Over the name being edited (see renameSelection()), one per view
        RenameField gridRenameField { this, RenameField::Style::Grid };
        RenameField listRenameField { this, RenameField::Style::List };
    } content { this };

protected:
    void layoutEvent(const CZLayoutEvent &e) override;
    void windowStateEvent(const CZWindowStateEvent &e) override;
    void pointerButtonEvent(const CZPointerButtonEvent &e) override;
    void pointerMoveEvent(const CZPointerMoveEvent &e) override;
    void keyboardKeyEvent(const CZKeyboardKeyEvent &e) override;

private:
    /// Shows @p dir (without recording it), selecting the item @p select if any.
    void navigate(const std::filesystem::path &dir, const std::string &select = {}) noexcept;

    /// Reads the folder again (in the background), then updates the icons (keeping the selection).
    void rescan() noexcept;

    /// The previous folder's files, still shown, are cleared (see m_stale).
    void clearStale() noexcept;

    /// The folder was read: shown (keeping the selection of the files still there).
    void showEntries(std::vector<DeskEntry> &&entries) noexcept;

    /// Watches m_dir for changes (inotify).
    void watch() noexcept;

    /// The folder went away (deleted, its disk ejected...): shows the closest one that exists.
    bool leaveIfGone() noexcept;

    void updateNavigation() noexcept;

    /// The view shown (grid or list: the list is an AKGridView too).
    AKGridView &view() noexcept { return m_listView ? static_cast<AKGridView&>(content.list) : content.grid; }
    const AKGridView &view() const noexcept { return m_listView ? static_cast<const AKGridView&>(content.list) : content.grid; }

    /// Shows @p selected on a cell of the view shown.
    void setCellSelected(AKNode &cell, bool selected) noexcept;

    /// Sorts m_entries (see sortBy()) and shows them again.
    void sortEntries() noexcept;

    /// The file whose icon or name is under @p pos (scene coordinates).
    std::optional<size_t> indexAt(const SkPoint &pos) const noexcept;

    /// The file named @p key (its file name).
    std::optional<size_t> indexOf(const std::string &key) const noexcept;

    bool isSelected(size_t index) const noexcept { return index < m_entries.size() && m_selected.contains(m_entries[index].key); }
    void setSelected(size_t index, bool selected) noexcept;

    /// Moves the selection to the neighbor of the last selected item (arrow keys).
    void moveSelection(Int32 dx, Int32 dy) noexcept;

    /// @p pos (scene coordinates) in the view's content (scrolled: where the cells are, see AKGridView::cellRect()).
    SkPoint toViewContent(const SkPoint &pos) const noexcept;

    /// Selects the files the rubber band touches (and those of m_bandBase), and shows it.
    void updateBand() noexcept;
    void endBand() noexcept;

    /// The rubber band's scroll speed (points per second, down positive) for where the pointer is.
    SkScalar bandScrollSpeed() const noexcept;

    /// Scrolls the view while the rubber band is near its edges (see bandScrollSpeed()).
    void bandAutoScroll() noexcept;

    /* Drag & drop */

    /// Starts dragging the selection, grabbed by the file at @p grabbed.
    void startDrag(size_t grabbed) noexcept;
    void endDrag() noexcept;

    // Several: the other icons in view fly to the pointer (see DragGather), then the drag icon
    // becomes the whole stack (like the desktop's, see Background)
    void gatherFinished() noexcept;
    void setDragImage(const std::vector<std::shared_ptr<RImage>> &images, UInt32 count) noexcept;

    void dragOver(MDNDOffer &offer, const SkPoint &pos) noexcept;
    void dropped(MDNDOffer &offer, const SkPoint &pos) noexcept;

    /// The folder or disk a drag over @p pos would drop into, if any.
    std::optional<size_t> dropTargetAt(MDNDOffer &offer, const SkPoint &pos) const noexcept;
    void setDropTarget(const std::string &key) noexcept;

    /// Hides what shows where a drag would drop (highlighted files and favorites, the favorites' line).
    void clearDropFeedback() noexcept;

    /// Whether the drag is a sidebar favorite being reordered (see FolderSidebar).
    bool reorderingFavorite(MDNDOffer &offer) const noexcept;

    /// Whether the drag carries this folder's own files only (dropping them here does nothing).
    bool dragsFromHere(MDNDOffer &offer) const noexcept;

    /// Where the icon of @p index is drawn (content coordinates; small in the list).
    SkRect iconRect(size_t index) const noexcept;

    /// The image of @p index's icon (its thumbnail if it's in view with one).
    std::shared_ptr<RImage> iconImage(size_t index) const noexcept;

    /* List columns */

    /// Shows m_columns (header, rows, the list's content width).
    void applyColumns() noexcept;

    /// The name column follows the list's visible width (see Settings::listNameOffset): it grows
    /// and shrinks by as much as the window does. The others keep their widths.
    void layoutNameColumn() noexcept;

    /// The list's visible width (its viewport, not its content: 0 if it isn't known yet).
    SkScalar listViewportWidth() const noexcept;

    /// The rename field of the view shown.
    RenameField &renameField() noexcept { return m_listView ? content.listRenameField : content.gridRenameField; }
    const RenameField &renameField() const noexcept { return m_listView ? content.listRenameField : content.gridRenameField; }

    /// Where the name of @p index is edited (content coordinates, see renameSelection()): the
    /// label's box (as wide as the cell) or the row's name column.
    SkIRect nameBox(size_t index) const noexcept;

    /// Keeps the name being edited over its row (it moved, e.g. scrolled). Out of view: ended.
    void placeRename() noexcept;

    /// Ends the name being edited, renaming it if possible.
    void endRename() noexcept;

    void openItemsMenu(const CZPointerButtonEvent &event) noexcept;
    void openFolderMenu(const CZPointerButtonEvent &event) noexcept;
    void openFavoriteMenu(const std::filesystem::path &folder, const CZPointerButtonEvent &event) noexcept;

    std::filesystem::path m_dir;
    std::vector<std::filesystem::path> m_backStack, m_forwardStack;
    // The model: the files (in grid order) and the selected ones (by key)
    std::vector<DeskEntry> m_entries;
    std::unordered_map<std::string, size_t> m_indexByKey;
    std::unordered_set<std::string> m_selected;

    // Reading (see rescan())
    UInt64 m_scanId { 0 };          // The read in progress (0: none)
    UInt32 m_scanStartMs { 0 };
    std::string m_selectWhenLoaded; // Selected once the folder is read

    std::string m_lastSelectedKey; // Where the arrow keys move from
    std::string m_anchorKey;       // Where a Shift+click range starts (the list, see selectRange())
    bool m_listView { false };

    // The list's columns: the shared ones (Settings::listColumns), with the name's width for this
    // window's width (see layoutNameColumn())
    DeskListColumns m_columns;
    SortColumn m_sortColumn { SortColumn::Name };
    bool m_sortAscending { true };

    UInt64 m_lastClickMs { 0 };
    std::string m_lastClickKey;
    bool m_showsHidden { false };

    Int32 m_inotify { -1 }, m_watch { -1 };
    std::shared_ptr<CZEventSource> m_inotifySource;
    CZTimer m_rescanTimer;

    /* Going to another folder, the files of the previous one stay in view (not clickable) until
     * the new one is read: no frame shows an empty window. If reading takes longer than
     * DESK_STALE_VIEW_MS, they're cleared. */
    bool m_stale { false };
    CZTimer m_staleTimer;

    // Rubber band selection: a press that may start one (dragged far enough), and the one in progress
    bool m_bandPossible { false }, m_bandActive { false };
    SkPoint m_bandPressPos {};          // Scene coordinates
    SkPoint m_bandStart {};             // In the view's content (see toViewContent())
    std::unordered_set<std::string> m_bandBase; // Kept selected (Ctrl/Shift)
    CZTimer m_bandScrollTimer;          // Auto-scroll ticks (see bandAutoScroll())
    UInt64 m_bandScrollMs { 0 };        // The last tick
    SkScalar m_bandScrollRest { 0.f };  // Fractions of a point not scrolled yet

    // A press on a file that may start a drag (moved far enough, see pointerMoveEvent())
    std::string m_dragPressKey;
    std::shared_ptr<CZPointerButtonEvent> m_press;
    SkPoint m_pressPos {};
    bool m_selectOnRelease { false }; // Pressed an already selected file: only it selected on release (no drag)

    // Drag & drop (the session and its icon surface are reused)
    std::unique_ptr<MDND> m_dnd;
    std::unique_ptr<FolderItem> m_dndItem;  // What's dragged (one file): a copy of its item
    std::unique_ptr<DragStack> m_dndStack;  // What's dragged (several): their icons stacked, with a count
    std::unique_ptr<DragGather> m_gather;
    std::vector<std::shared_ptr<RImage>> m_dragImages; // Back to front (the grabbed one last)
    std::unordered_set<std::string> m_dragKeys;        // Dimmed meanwhile
    SkIPoint m_grabInIcon {}; // Pointer position within the grabbed icon (at the stack's size)
    std::string m_dropTargetKey; // Folder/disk highlighted as the drop target

    // A rename ended with Enter: that same key press doesn't open the file
    UInt64 m_renameSubmitMs { 0 };

    // Clicking the name of the only file selected renames it, unless a double click follows
    std::string m_pressRenameKey, m_renameCandidate;
    CZTimer m_renameTimer;

    std::unique_ptr<ContextMenu> m_menu;

    // Its menus in the global menu bar, shown while it's active (see WindowMenuBar)
    std::unique_ptr<WindowMenuBar> m_menus;
    void menusChanged() noexcept { if (m_menus) m_menus->scheduleUpdate(); }
};

#endif // FOLDERWINDOW_H
