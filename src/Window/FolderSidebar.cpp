#include <Window/FolderSidebar.h>
#include <Core/Desk.h>
#include <Window/FolderWindow.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKScene.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZPointerMoveEvent.h>
#include <linux/input-event-codes.h>

namespace fs = std::filesystem;

// Like Finder's: icons without a tile, in the accent color
static const CZAdaptiveColor NoTile { 0 };

// Larger than Kay's defaults (made for icons on tiles, like Settings'): bare icons look small
static constexpr SkScalar RowHeight { 30.f };
static constexpr SkScalar FontSize  { 14.f };
static constexpr UInt32   IconSize  { 18 };
static constexpr SkScalar TitleSize { 12.f };

// The section's title, larger too (still at the bottom of its row, see AKSidebarSection)
static void StyleTitle(AKSidebarSection &section) noexcept
{
    section.titleText.setFontSize(TitleSize);
    section.titleText.layout().setMargin(YGEdgeTop, AKSidebarSection::TitleHeight - TitleSize * 1.3f - 4.f);
}

static AKSidebarItem &Styled(AKSidebarItem &item) noexcept
{
    item.layout().setHeight(RowHeight);
    item.label.setFontSize(FontSize);
    item.tile.icon.setSize(IconSize);
    return item;
}

FolderSidebar::FolderSidebar(AKNode *parent) noexcept : MSidebar(parent)
{
    setWidth(DESK_WINDOW_SIDEBAR_WIDTH);
    StyleTitle(favorites);
    StyleTitle(locations);

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

    // A thin rounded line between the rows (see showInsertionLine())
    insertionLine.setInputRegion(&empty);
    insertionLine.layout().setPositionType(YGPositionTypeAbsolute);
    insertionLine.layout().setPosition(YGEdgeLeft, 12.f);
    insertionLine.layout().setPosition(YGEdgeRight, 12.f);
    insertionLine.layout().setHeight(2.f);
    insertionLine.setVisible(false);

    auto *desk { GetDesk() };
    desk->onFavoritesChanged.subscribe(this, [this]{ updateFavorites(); });
    updateFavorites();

    list.onSelectionChanged.subscribe(this, [this](AKSidebarItem *item){
        if (item && !m_selecting)
            onFolderClicked.notify(fs::path(item->id));
    });

    // Disks plugged / unplugged
    desk->onEntriesChanged.subscribe(this, [this]{ updateLocations(); });
    updateLocations();
}

// Whether @p dir is where apps (their desktop entries) are installed: /usr/share/applications, or
// "applications" in $XDG_DATA_HOME (~/.local/share) or one of $XDG_DATA_DIRS (/usr/local/share:/usr/share)
static bool IsApplicationsDir(const fs::path &dir) noexcept
{
    std::vector<fs::path> dataDirs { "/usr/share" };

    if (const char *home { std::getenv("XDG_DATA_HOME") }; home && *home)
        dataDirs.emplace_back(home);
    else if (const char *user { std::getenv("HOME") }; user && *user)
        dataDirs.emplace_back(fs::path(user) / ".local/share");

    const char *dirs { std::getenv("XDG_DATA_DIRS") };
    std::string list { dirs && *dirs ? dirs : "/usr/local/share:/usr/share" };
    size_t start { 0 };

    while (start <= list.size())
    {
        const size_t end { std::min(list.find(':', start), list.size()) };

        if (end > start)
            dataDirs.emplace_back(list.substr(start, end - start));

        start = end + 1;
    }

    for (const auto &data : dataDirs)
        if (FolderWindow::Normalized(data / "applications") == dir)
            return true;

    return false;
}

void FolderSidebar::updateFavorites() noexcept
{
    auto *desk { GetDesk() };
    const fs::path home { FolderWindow::Normalized(desk->xdgkit->homeDir()) };

    // The user folders with their names and icons (like Finder's), others with the folder's
    const std::pair<fs::path, std::pair<std::string, const char*>> known[] {
        { home, { desk->xdgkit->username(), "home" } },
        { FolderWindow::Normalized(desk->desktopDir()), { "Desktop", "desktop_windows" } },
        { FolderWindow::Normalized(desk->userDir("DOCUMENTS", "Documents")), { "Documents", "description" } },
        { FolderWindow::Normalized(desk->userDir("DOWNLOAD", "Downloads")), { "Downloads", "download" } },
        { FolderWindow::Normalized(desk->userDir("PICTURES", "Pictures")), { "Pictures", "image" } },
        { FolderWindow::Normalized(desk->userDir("MUSIC", "Music")), { "Music", "music_note" } },
        { FolderWindow::Normalized(desk->userDir("VIDEOS", "Videos")), { "Videos", "movie" } } };

    favorites.clear();
    m_looks.clear();
    std::error_code ec;

    for (const fs::path &favorite : desk->favorites())
    {
        const fs::path dir { FolderWindow::Normalized(favorite) };

        // Folders that don't exist (anymore) aren't listed, but stay in the favorites (e.g. a disk)
        if (!fs::is_directory(dir, ec) || favorites.list().item(dir.string()))
            continue;

        std::string name { dir.filename().string() };
        const char *icon { "folder" };

        for (const auto &[path, look] : known)
            if (path == dir)
            {
                name = look.first;
                icon = look.second;
                break;
            }

        if (IsApplicationsDir(dir))
        {
            name = "Applications";
            icon = "apps";
        }

        if (name.empty())
            name = dir.string(); // The root

        Styled(favorites.addItem(name, icon, NoTile, dir.string())).setIconColor(AKTheme::ControlAccent);
        m_looks[dir.string()] = { name, icon };
    }

    setCurrentFolder(m_current);
    setDropTarget(m_dropTarget.empty() ? std::nullopt : std::optional<fs::path>(m_dropTarget));
}

// The favorite dragged by any sidebar (one drag at a time)
static fs::path s_draggedFavorite;

const fs::path &FolderSidebar::DraggedFavorite() noexcept
{
    return s_draggedFavorite;
}

std::optional<fs::path> FolderSidebar::favoriteAt(const SkPoint &pos) const noexcept
{
    if (!SkRect::Make(list.worldRect()).contains(pos.x(), pos.y()))
        return std::nullopt;

    for (const auto &row : favorites.items())
        if (SkRect::Make(row->worldRect()).contains(pos.x(), pos.y()))
            return fs::path(row->id);

    return std::nullopt;
}

bool FolderSidebar::overFavorites(const SkPoint &pos) const noexcept
{
    // Their title included, a few points around
    SkRect area { SkRect::Make(favorites.worldRect()) };
    area.outset(0.f, 4.f);
    return SkRect::Make(list.worldRect()).contains(pos.x(), pos.y()) && area.contains(pos.x(), pos.y());
}

size_t FolderSidebar::favoritesIndexAt(const SkPoint &pos) const noexcept
{
    // Before the first row whose middle is below the pointer
    const auto &rows { favorites.items() };

    for (size_t i = 0; i < rows.size(); i++)
        if (pos.y() < SkRect::Make(rows[i]->worldRect()).centerY())
            return i;

    return rows.size();
}

void FolderSidebar::showInsertionLine(std::optional<size_t> index) noexcept
{
    const auto &rows { favorites.items() };

    if (!index || rows.empty())
    {
        insertionLine.setVisible(false);
        return;
    }

    // Between the rows around it (or at the edge of the first / last one)
    SkScalar y;

    if (*index == 0)
        y = SkScalar(rows.front()->worldRect().top());
    else if (*index >= rows.size())
        y = SkScalar(rows.back()->worldRect().bottom());
    else
        y = (rows[*index - 1]->worldRect().bottom() + rows[*index]->worldRect().top()) / 2.f;

    insertionLine.layout().setPosition(YGEdgeTop, y - SkScalar(worldRect().y()) - 1.f);
    insertionLine.setVisible(true);
}

void FolderSidebar::setDropTarget(const std::optional<fs::path> &dir) noexcept
{
    m_dropTarget = dir ? dir->string() : std::string();

    for (const auto &row : favorites.items())
        row->setDropTarget(!m_dropTarget.empty() && row->id == m_dropTarget);
}

void FolderSidebar::favoritePressed(const CZPointerButtonEvent &e) noexcept
{
    if (e.button != BTN_LEFT)
        return;

    m_pressedFavorite.clear();
    m_press.reset();

    if (!e.pressed)
        return;

    // A favorite pressed: maybe dragged (see pointerMoveEvent())
    const SkPoint pos { AKApp::Get()->pointer().pos() };

    if (const auto dir { favoriteAt(pos) })
    {
        m_pressedFavorite = dir->string();
        m_press = std::static_pointer_cast<CZPointerButtonEvent>(e.copy());
        m_pressPos = pos;
    }
}

void FolderSidebar::favoriteMoved() noexcept
{
    if (m_pressedFavorite.empty() || !m_press ||
        SkPoint::Distance(AKApp::Get()->pointer().pos(), m_pressPos) < DESK_DRAG_THRESHOLD)
        return;

    const std::string dir { std::move(m_pressedFavorite) };
    m_pressedFavorite.clear();
    startDrag(dir);
    m_press.reset();
}

void FolderSidebar::startDrag(const std::string &dir) noexcept
{
    MSurface *window { scene() ? scene()->window() : nullptr };
    const AKSidebarItem *row { favorites.list().item(dir) };

    if (!window || !row || (m_dnd && m_dnd->dragging()))
        return;

    if (!m_dnd)
    {
        m_dnd = std::make_unique<MDND>();
        m_dnd->onFinished.subscribe(this, []{ s_draggedFavorite.clear(); });
        m_dnd->onCancelled.subscribe(this, []{ s_draggedFavorite.clear(); });
    }

    // A rounded copy of the row: its icon and name
    const Look look { m_looks.contains(dir) ? m_looks[dir] : Look { fs::path(dir).filename().string(), "folder" } };
    const CZColorScheme scheme { colorScheme() };
    m_dragIcon = std::make_unique<DragIcon>(m_dnd.get());
    auto &icon { *m_dragIcon };
    // Nearly opaque, like the sidebar without vibrancy
    icon.box.setBackgroundColor(CZAdaptiveColor(0xF2E6E7E7, 0xF2323232).resolve(scheme));
    icon.box.layout().setPositionType(YGPositionTypeAbsolute);
    icon.box.layout().setFlexDirection(YGFlexDirectionRow);
    icon.box.layout().setAlignItems(YGAlignCenter);
    icon.box.layout().setPadding(YGEdgeHorizontal, 8.f);
    icon.box.layout().setGap(YGGutterColumn, 6.f);
    icon.icon.setIconFromName(look.icon);
    icon.icon.setColor(AKTheme::ControlAccent.resolve(scheme));
    icon.label.setText(look.name);
    icon.label.setFontSize(FontSize);
    icon.icon.setSize(IconSize);
    icon.label.setTextColor(AKTheme::Label);

    const SkIRect rowRect { row->worldRect() };
    const Int32 width { std::max(rowRect.width(), 16 + Int32(IconSize) + 6 + Int32(icon.label.layout().width().value)) };
    icon.box.layout().setWidth(width);
    icon.box.layout().setHeight(rowRect.height());
    m_dnd->layout().setWidth(width);
    m_dnd->layout().setHeight(rowRect.height());

    // Under the pointer where the row was grabbed
    m_dnd->setHotspot(SkIPoint::Make(
        std::clamp(Int32(m_pressPos.x()) - rowRect.x(), 0, width),
        std::clamp(Int32(m_pressPos.y()) - rowRect.y(), 0, rowRect.height())));

    if (m_dnd->start(*window, *m_press, { DESK_FAVORITE_MIME_TYPE }, MDNDOffer::Move))
        s_draggedFavorite = dir;
}

void FolderSidebar::setCurrentFolder(const fs::path &dir) noexcept
{
    m_current = dir;
    m_selecting = true;
    list.setSelectedItem(list.item(FolderWindow::Normalized(dir).string()));
    m_selecting = false;
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

        auto &item { Styled(locations.addItem(volume->name, icon, NoTile, FolderWindow::Normalized(volume->path).string())) };
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
