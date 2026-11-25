#ifndef FOLDERSIDEBAR_H
#define FOLDERSIDEBAR_H

#include <Core/Types.h>
#include <CZ/Marco/Nodes/MSidebar.h>
#include <CZ/AK/Nodes/AKSidebarList.h>
#include <CZ/AK/Effects/AKEdgeShadow.h>
#include <CZ/Core/CZSignal.h>
#include <filesystem>

/**
 * @brief The sidebar of a folder window, like Finder's.
 *
 * [window buttons]
 * Favorites: the home folder and the user folders (Desktop, Documents, Downloads...)
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

    CZ::CZSignal<const std::filesystem::path &> onFolderClicked;

    CZ::AKSidebarList list { &content };
    CZ::AKSidebarSection &favorites { list.addSection("Favorites") };
    CZ::AKSidebarSection &locations { list.addSection("Locations") };

    // Under the window buttons: shown while the list is scrolled under them
    CZ::AKContainer headerShadowArea { YGFlexDirectionColumn, false, this };
    CZ::AKEdgeShadow headerShadow { CZEdgeBottom, &headerShadowArea };

private:
    void addFavorite(const std::string &name, const char *icon, const std::filesystem::path &dir) noexcept;
    void updateLocations() noexcept;
    std::filesystem::path m_current;
    bool m_selecting { false }; // Highlighting the current folder: not a click
};

#endif // FOLDERSIDEBAR_H
