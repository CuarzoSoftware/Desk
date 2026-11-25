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
#include <CZ/AK/Nodes/AKIconButton.h>
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
 * [sidebar (vibrancy, window buttons, Favorites, Locations)][toolbar (back, forward, name) / icons / status]
 *
 * Double clicking a folder (or Ctrl+Down) opens it in the same window, other files open with their
 * default apps. Back and forward go through the folders shown before, like a browser. The folder is
 * watched: the icons follow its changes. If it goes away (deleted, its disk ejected) the window
 * shows the closest folder that still exists.
 *
 * Keyboard (⌘ = Ctrl): arrows move the selection, Enter or ⌘↓ opens it, ⌘↑ / Backspace goes to the
 * enclosing folder, ⌘[ ⌘] / Alt+←→ go back and forward, ⌘A selects all, Delete or ⌘⌫ moves to the
 * trash, ⇧⌘N creates a folder, ⌘H shows hidden files, ⌘N opens another window and ⌘W closes it.
 * ⇧⌘H, ⇧⌘D, ⇧⌘O, ⇧⌘L and ⇧⌘C go to Home, Desktop, Documents, Downloads and the system disk.
 *
 * The files are a model (their entries and the names selected): the icons are an AKGridView, which
 * only creates those in view (FolderItem cells bound to the files as they scroll in), so folders
 * with thousands of files scroll as smoothly as small ones.
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

    /// Shows the enclosing folder, selecting the one shown now.
    void goUp() noexcept;

    /* Selection */
    std::vector<const DeskEntry*> selectedEntries() const noexcept;

    /// Selects only the file at @p index (none: nullopt), scrolling to it.
    void selectOnly(std::optional<size_t> index) noexcept;
    void clearSelection() noexcept;
    void selectAll() noexcept;

    /**
     * @brief Opens the selected items.
     *
     * A single folder is shown in this window (in a new one with @p newWindow). Several folders
     * open in their own windows. Files open with their default apps.
     */
    void openSelection(bool newWindow = false) noexcept;
    void trashSelection() noexcept;

    /// Creates a new folder here and selects it.
    void newFolder() noexcept;

    /// Shows the hidden files (starting with ".") too.
    void setShowsHidden(bool shows) noexcept;
    bool showsHidden() const noexcept { return m_showsHidden; }

    FolderSidebar sidebar { this };

    // Next to the sidebar: a toolbar the window is dragged by, the icons (scrolled) and a status bar.
    struct Content : public AKSolidColor
    {
        Content(AKNode *parent) noexcept;
        AKContainer toolbar { YGFlexDirectionRow, false, this };
        AKIconButton back { "chevron_left", 26, &toolbar };
        AKIconButton forward { "chevron_right", 26, &toolbar };
        AKText title { "", &toolbar };
        AKGridView grid { this }; // The files' icons (FolderItem cells)
        AKContainer statusBar { YGFlexDirectionRow, false, this };
        AKText status { "", &statusBar };
        AKEdgeShadow separator { CZEdgeLeft, this }; // Over the sidebar's trailing edge

        // Under the toolbar, above the icons: shown while they're scrolled under it (like Finder)
        AKContainer toolbarShadowArea { YGFlexDirectionColumn, false, this };
        AKEdgeShadow toolbarShadow { CZEdgeBottom, &toolbarShadowArea };
    } content { this };

protected:
    void pointerButtonEvent(const CZPointerButtonEvent &e) override;
    void keyboardKeyEvent(const CZKeyboardKeyEvent &e) override;

private:
    /// Shows @p dir (without recording it), selecting the item @p select if any.
    void navigate(const std::filesystem::path &dir, const std::string &select = {}) noexcept;

    /// Reads the folder again (in the background), then updates the icons (keeping the selection).
    void rescan() noexcept;

    /// The folder was read: shown (keeping the selection of the files still there).
    void showEntries(std::vector<DeskEntry> &&entries) noexcept;

    /// Watches m_dir for changes (inotify).
    void watch() noexcept;

    /// The folder went away (deleted, its disk ejected...): shows the closest one that exists.
    bool leaveIfGone() noexcept;

    void updateNavigation() noexcept;
    void updateStatus() noexcept;

    /// The file whose icon or name is under @p pos (scene coordinates).
    std::optional<size_t> indexAt(const SkPoint &pos) const noexcept;

    /// The file named @p key (its file name).
    std::optional<size_t> indexOf(const std::string &key) const noexcept;

    bool isSelected(size_t index) const noexcept { return m_selected.contains(m_entries[index].key); }
    void setSelected(size_t index, bool selected) noexcept;

    /// Moves the selection to the neighbor of the last selected item (arrow keys).
    void moveSelection(Int32 dx, Int32 dy) noexcept;

    void openItemsMenu(const CZPointerButtonEvent &event) noexcept;
    void openFolderMenu(const CZPointerButtonEvent &event) noexcept;

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

    UInt64 m_lastClickMs { 0 };
    std::string m_lastClickKey;
    bool m_showsHidden { false };

    Int32 m_inotify { -1 }, m_watch { -1 };
    std::shared_ptr<CZEventSource> m_inotifySource;
    CZTimer m_rescanTimer;

    std::unique_ptr<ContextMenu> m_menu;
};

#endif // FOLDERWINDOW_H
