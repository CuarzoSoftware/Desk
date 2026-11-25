#include <Window/FolderSidebar.h>
#include <Core/Desk.h>
#include <Window/FolderWindow.h>
#include <CZ/AK/AKTheme.h>

namespace fs = std::filesystem;

// Like Finder's: icons without a tile, in the accent color
static const CZAdaptiveColor NoTile { 0 };

FolderSidebar::FolderSidebar(AKNode *parent) noexcept : MSidebar(parent)
{
    setWidth(DESK_WINDOW_SIDEBAR_WIDTH);

    // The header's area, over the list (added last): its shadow falls on the rows
    const SkRegion empty;
    headerShadowArea.setInputRegion(&empty);
    headerShadowArea.layout().setPositionType(YGPositionTypeAbsolute);
    headerShadowArea.layout().setPosition(YGEdgeTop, 0.f);
    headerShadowArea.layout().setPosition(YGEdgeLeft, 0.f);
    headerShadowArea.layout().setWidthPercent(100.f);
    headerShadowArea.layout().setHeight(HeaderHeight);
    headerShadow.setStrength(0.6f); // Subtle, like Finder's
    headerShadow.showWhileScrolled(&list);
    setWidthRange(150.f, 320.f);
    list.layout().setFlex(1.f);

    auto *desk { GetDesk() };
    const fs::path home { desk->xdgkit->homeDir() };
    addFavorite(desk->xdgkit->username(), "home", home);
    addFavorite("Desktop", "desktop_windows", desk->desktopDir());
    addFavorite("Documents", "description", desk->userDir("DOCUMENTS", "Documents"));
    addFavorite("Downloads", "download", desk->userDir("DOWNLOAD", "Downloads"));
    addFavorite("Pictures", "image", desk->userDir("PICTURES", "Pictures"));
    addFavorite("Music", "music_note", desk->userDir("MUSIC", "Music"));
    addFavorite("Videos", "movie", desk->userDir("VIDEOS", "Videos"));

    list.onSelectionChanged.subscribe(this, [this](AKSidebarItem *item){
        if (item && !m_selecting)
            onFolderClicked.notify(fs::path(item->id));
    });

    // Disks plugged / unplugged
    desk->onEntriesChanged.subscribe(this, [this]{ updateLocations(); });
    updateLocations();
}

void FolderSidebar::addFavorite(const std::string &name, const char *icon, const fs::path &dir) noexcept
{
    // Folders that don't exist aren't listed (e.g. no Music folder)
    std::error_code ec;

    const std::string id { FolderWindow::Normalized(dir).string() };

    if (!fs::is_directory(dir, ec) || favorites.list().item(id))
        return;

    favorites.addItem(name, icon, NoTile, id).setIconColor(AKTheme::ControlAccent);
}

void FolderSidebar::updateLocations() noexcept
{
    auto *desk { GetDesk() };
    const auto volumes { desk->volumes() };

    // Nothing changed (most of the time: files changing on the desktop)
    bool same { volumes.size() == locations.items().size() };

    for (size_t i = 0; same && i < volumes.size(); i++)
        same = locations.items()[i]->id == FolderWindow::Normalized(volumes[i]->path).string() &&
               locations.items()[i]->label.text() == volumes[i]->name;

    if (same)
        return;

    locations.clear();

    for (const DeskEntry *volume : volumes)
    {
        const char *icon { volume->internal ? "computer" : "sd_storage" };

        if (volume->fsType == "iso9660" || volume->fsType == "udf")
            icon = "album";
        else if (volume->usb)
            icon = "usb";

        auto &item { locations.addItem(volume->name, icon, NoTile, FolderWindow::Normalized(volume->path).string()) };
        item.setIconColor(AKTheme::SecondaryLabel);

        if (!volume->internal)
        {
            item.setAccessory("eject");
            item.onAccessoryClicked.subscribe(this, [key = volume->key]{
                if (const DeskEntry *e { GetDesk()->entry(key) })
                    GetDesk()->eject(*e);
            });
        }
    }

    // The row of the current folder may have just appeared
    setCurrentFolder(m_current);
}

void FolderSidebar::setCurrentFolder(const fs::path &dir) noexcept
{
    m_current = dir;
    m_selecting = true;
    list.setSelectedItem(list.item(FolderWindow::Normalized(dir).string()));
    m_selecting = false;
}
