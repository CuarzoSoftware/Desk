#ifndef FOLDERSIDEBAR_H
#define FOLDERSIDEBAR_H

#include <Core/Types.h>
#include <CZ/Marco/Nodes/MSidebar.h>
#include <CZ/AK/Nodes/AKSidebarList.h>
#include <CZ/AK/Effects/AKEdgeShadow.h>
#include <CZ/Core/CZSignal.h>
#include <filesystem>
#include <optional>
#include <CZ/AK/AKTheme.h>
#include <CZ/AK/Nodes/AKSolidColor.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKFontIcon.h>
#include <CZ/AK/Nodes/AKText.h>
#include <CZ/Marco/Roles/MDND.h>
#include <memory>
#include <unordered_map>

/**
 * @brief The sidebar of a folder window, like Finder's.
 *
 * [window buttons]
 * Favorites: the folders chosen by the user (see Desk::favorites(): by default the home folder
 *            and the user folders), in their order. Rows are dragged (drag & drop, their icon a
 *            rounded copy of the row) to reorder them, a blue line showing where they go. A folder
 *            dragged from windows, the desktop or other apps is added at the end; files dragged
 *            over a favorite go into its folder (it's highlighted). See FolderWindow, which also
 *            opens a favorite's menu (Remove from Sidebar).
 * Locations: the system disk and the mounted disks (the removable ones with an eject button),
 *            kept up to date as disks come and go.
 *
 * Rows are identified by their folder's path (AKSidebarItem::id). Clicking one emits onFolderClicked.
 */
class FolderSidebar final : public CZ::MSidebar
{
public:
    FolderSidebar(CZ::AKNode *parent) noexcept;

    /// Highlights the row of @p dir (none if it has no row), without emitting onFolderClicked.
    void setCurrentFolder(const std::filesystem::path &dir) noexcept;

    /// The folder of the favorite under @p pos (scene coordinates), if any.
    std::optional<std::filesystem::path> favoriteAt(const SkPoint &pos) const noexcept;

    /// Whether @p pos (scene coordinates) is over the favorites (their title, rows or the space between).
    bool overFavorites(const SkPoint &pos) const noexcept;

    /// Where a favorite moved to @p pos (scene coordinates) would go: before the row whose middle
    /// is below it (above or below the favorites: the first or last place).
    size_t favoritesIndexAt(const SkPoint &pos) const noexcept;

    /// Shows the blue line where a favorite would go (nullopt: hidden).
    void showInsertionLine(std::optional<size_t> index) noexcept;

    /// Highlights the favorite of @p dir (files would be dropped into it), none with nullopt.
    void setDropTarget(const std::optional<std::filesystem::path> &dir) noexcept;

    /// The favorite being dragged by a sidebar of any window (empty if none), see DESK_FAVORITE_MIME_TYPE.
    static const std::filesystem::path &DraggedFavorite() noexcept;

    CZ::CZSignal<const std::filesystem::path &> onFolderClicked;

    CZ::AKSidebarList list { &content };
    CZ::AKSidebarSection &favorites { list.addSection("Favorites") };
    CZ::AKSidebarSection &locations { list.addSection("Locations") };

    // Under the window buttons: shown while the list is scrolled under them
    CZ::AKContainer headerShadowArea { YGFlexDirectionColumn, false, this };
    CZ::AKEdgeShadow headerShadow { CZEdgeBottom, &headerShadowArea };

    // Where favorites go (dropped or reordered), over the rows
    CZ::AKSolidColor insertionLine { CZ::AKTheme::ControlAccent, this };

    /// The window's pointer button events (any node): a favorite pressed may be dragged (see favoriteMoved()).
    void favoritePressed(const CZ::CZPointerButtonEvent &e) noexcept;

    /// The window's pointer motion: a favorite pressed and moved far enough is dragged (to reorder it).
    void favoriteMoved() noexcept;

private:
    void updateFavorites() noexcept;
    void updateLocations() noexcept;

    /// Drags the favorite of @p dir (to reorder it), its icon a rounded copy of its row.
    void startDrag(const std::string &dir) noexcept;

    std::filesystem::path m_current;
    bool m_selecting { false }; // Highlighting the current folder: not a click

    // Each favorite's name and icon (by folder), for the drag icon
    struct Look { std::string name, icon; };
    std::unordered_map<std::string, Look> m_looks;

    // A favorite pressed (maybe dragged, see pointerMoveEvent())
    std::string m_pressedFavorite;
    std::shared_ptr<CZ::CZPointerButtonEvent> m_press;
    SkPoint m_pressPos {};

    // The drag & drop session (reused) and its icon
    std::unique_ptr<CZ::MDND> m_dnd;
    struct DragIcon
    {
        DragIcon(CZ::AKNode *parent) noexcept : box(0, CZ::CZBorderRadius::Make(6), parent) {}
        CZ::AKRoundSolidColor box;
        CZ::AKFontIcon icon { "", 16, &box };
        CZ::AKText label { "", &box };
    };
    std::unique_ptr<DragIcon> m_dragIcon;
    std::string m_dropTarget;
};

#endif // FOLDERSIDEBAR_H
