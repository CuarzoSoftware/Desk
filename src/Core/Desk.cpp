#include <sys/stat.h>
#include <CZ/Core/Utils/CZProcess.h>
#include <Core/Desk.h>
#include <CZ/Core/CZTime.h>
#include <Core/Log.h>
#include <Surfaces/Background.h>
#include <Core/MenuBar.h>
#include <Window/FolderWindow.h>
#include <Core/FolderScanner.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <CZ/XDG/XDGMimeDatabase.h>
#include <CZ/XDG/XDGTrash.h>
#include <cmath>
#include <CZ/XDG/XDGINI.h>
#include <CZ/Marco/MDNDOffer.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKClipboard.h>
#include <CZ/AK/Input/AKPointer.h>
#include <CZ/AK/Input/AKKeyboard.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZKeyboardKeyEvent.h>
#include <chrono>
#include <poll.h>
#include <string_view>
#include <CZ/Core/CZEventSource.h>
#include <CZ/Core/CZBus.h>
#include <systemd/sd-bus.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static Desk *_desk;

Desk *GetDesk() noexcept { return _desk; }

Desk::Desk() noexcept
{
    _desk = this;

    marco = MApp::Make();
    if (!marco)
        return;

    // Its windows (folders) go with the dock's Desk tile (see Shelf::DeskAppId)
    marco->setAppId("org.cuarzo.Desk");

    // Timers register with the CZCore when created: only once Marco made it.
    m_rescanTimer = std::make_unique<CZTimer>([this](CZTimer *){ rescan(); });
    scanner = std::make_unique<FolderScanner>();

    xdgkit = XDGKit::Make();
    settings.load();

    // Set before copies were kept (or by hand in desk.conf): copied now if it still exists
    if (std::error_code ec; !settings.picture.empty() && !Settings::IsStoredPicture(settings.picture) &&
        fs::is_regular_file(settings.picture, ec))
    {
        settings.picture = Settings::StorePicture(settings.picture);
        saveSettings();
    }

    loadTopbarScale();
    assets.init();
    initDesktopDir();
    initWatches();
    rescan();

    marco->onScreenPlugged.subscribe(this, [this](MScreen &s){ onScreenPlugged(s); });
    marco->onScreenUnplugged.subscribe(this, [this](MScreen &s){ onScreenUnplugged(s); });

    for (MScreen *s : marco->screens())
        onScreenPlugged(*s);

    m_menuTimer = std::make_unique<CZTimer>([this](CZTimer *){ if (menuBar) menuBar->update(); });
    onBackgroundChanged.subscribe(this, [this]{ selectionChanged(); });
    onIconsChanged.subscribe(this, [this]{ selectionChanged(); });
    menuBar = std::make_unique<MenuBar>();
    initBus();
}

Desk::~Desk() noexcept
{
    m_windows.clear();
    scanner.reset();
    menuBar.reset();

    if (marco)
        for (MScreen *s : marco->screens())
            onScreenUnplugged(*s);

    if (m_inotify >= 0)
    {
        m_inotifySource.reset();
        close(m_inotify);
    }

    _desk = nullptr;
}

/* ---------------------------------------- Screens ---------------------------------------- */

void Desk::onScreenPlugged(MScreen &screen) noexcept
{
    if (screen.userData)
        return;

    Log(CZInfo, CZLN, "Screen plugged: {}", screen.props().name);
    auto *background { new Background(screen) }; // Sets screen.userData
    background->setShowsIcons(true);
}

void Desk::onScreenUnplugged(MScreen &screen) noexcept
{
    auto *background { static_cast<Background*>(screen.userData) };
    screen.userData = nullptr;
    Log(CZInfo, CZLN, "Screen unplugged: {}", screen.props().name);
    delete background; // Also clears it as the active / drag source desktop (weak references)
    selectionChanged();  // The menus now act on another desktop
}

void Desk::loadTopbarScale() noexcept
{
    const fs::path path { Settings::FilePath().parent_path().parent_path() / "cz-firmament" / "firmament.conf" };
    m_topbarScale = 1.2f; // Firmament's default (TOPBAR_DEFAULT_SCALE)
    std::error_code ec;

    if (!fs::is_regular_file(path, ec))
        return;

    if (auto ini { XDGINI::LoadFile(path) })
        if (auto group { ini->find("Topbar") }; group != ini->end())
            if (auto it { group->second.find("Scale") }; it != group->second.end())
                m_topbarScale = std::clamp(std::strtof(it->second.c_str(), nullptr), 1.f, 4.f);
}

Int32 Desk::topbarHeight() const noexcept
{
    return Int32(std::round(DESK_TOPBAR_HEIGHT * m_topbarScale));
}

Background *Desk::iconsBackground() const noexcept
{
    if (m_activeBackground)
        return static_cast<Background*>(m_activeBackground.get());

    // Nothing used yet: the primary screen's (at 0,0), else any.
    Background *any { nullptr };

    for (MScreen *s : marco->screens())
        if (auto *background { static_cast<Background*>(s->userData) })
        {
            if (s->props().pos == SkIPoint::Make(0, 0))
                return background;

            if (!any)
                any = background;
        }

    return any;
}

Background *Desk::dragSource() const noexcept
{
    return static_cast<Background*>(m_dragSource.get());
}

void Desk::setDragSource(Background *background) noexcept
{
    m_dragSource.reset(background);
}

void Desk::windowDeactivated() noexcept
{
    // Later: the next window may be activated right after (its menus then). Owned: never after Desk.
    if (!m_deactivatedTimer)
        m_deactivatedTimer = std::make_unique<CZTimer>([this](CZTimer*){
            for (const auto &w : m_windows)
                if (w && w->activated())
                    return;

            if (menuBar)
                menuBar->activate();
        });

    m_deactivatedTimer->start(0);
}

void Desk::setActiveBackground(Background *background) noexcept
{
    // The desktop's menus in the menu bar (a window's may have been shown)
    if (menuBar)
        menuBar->activate();

    if (m_activeBackground.get() == background)
        return;

    // A single selection at a time: the menus and the keyboard act on it.
    if (m_activeBackground)
        static_cast<Background*>(m_activeBackground.get())->clearSelection();

    m_activeBackground.reset(background);
    selectionChanged();
}

bool Desk::openFolder(const fs::path &requested, bool newWindow, const std::string &select, const std::string &token,
                      const IconOrigin *origin) noexcept
{
    std::error_code ec;

    // E.g. trash:/// (or a folder that went away): the session's opener knows what to do
    if (!fs::is_directory(requested, ec))
    {
        Log(CZInfo, CZLN, "Not a folder: {} (trying xdg-open)", requested.string());
        Spawn({ "xdg-open", requested.string() });
        return false;
    }

    const fs::path path { FolderWindow::Normalized(requested) };

    // Already shown: that window comes to the front (like Finder)
    if (!newWindow)
        for (const auto &window : m_windows)
        {
            if (!window->mapped() || window->folder() != path)
                continue;

            Log(CZInfo, CZLN, "{} is already open: activating its window", path.string());
            window->select(select);
            window->activate(token);
            return false;
        }

    if (!m_windowsTimer)
        m_windowsTimer = std::make_unique<CZTimer>([this](CZTimer *){ destroyClosedWindows(); });

    Log(CZInfo, CZLN, "Opening a window for {}", path.string());
    auto &window { m_windows.emplace_back(std::make_unique<FolderWindow>(path)) };

    // Closed (unmapped): destroyed on the next loop iteration
    window->onMappedChanged.subscribe(window.get(), [this]{ m_windowsTimer->start(0); });
    // Opened from a desktop icon: grows from it (see announceFromIcon())
    const bool fromIcon { origin && !origin->rect.isEmpty() && announceFromIcon(window->title(), *origin) };

    window->setMapped(true);
    window->setKeyboardFocus(true);
    window->select(select);

    // Asked by another app: its token lets the new window take the focus
    if (!token.empty())
        window->activate(token);

    return fromIcon;
}

bool Desk::announceFromIcon(const std::string &title, const IconOrigin &origin) noexcept
{
    if (!m_bus)
        return false;

    sd_bus_message *call { nullptr };
    sd_bus_message *reply { nullptr };
    sd_bus_error error SD_BUS_ERROR_NULL;

    int r { sd_bus_message_new_method_call(m_bus->bus(), &call, "org.cuarzo.Crystals", "/org/cuarzo/Crystals",
                                           "org.cuarzo.Crystals", "OpenFromIcon") };

    if (r >= 0)
        r = sd_bus_message_append(call, "siiii", title.c_str(), origin.rect.x(), origin.rect.y(),
                                  origin.rect.width(), origin.rect.height());

    // Synchronous: Crystals must know before the window maps (it answers right away)
    if (r >= 0)
        r = sd_bus_call(m_bus->bus(), call, 200 * 1000, &error, &reply);

    if (r < 0)
        Log(CZDebug, CZLN, "OpenFromIcon failed (no Crystals?): {}", error.message ? error.message : strerror(-r));

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(call);
    return r >= 0;
}

void Desk::destroyClosedWindows() noexcept
{
    std::erase_if(m_windows, [](const auto &window){ return !window->mapped(); });
}

std::vector<const DeskEntry*> Desk::volumes() const noexcept
{
    std::vector<const DeskEntry*> volumes;

    for (const auto &e : m_entries)
        if (e.kind == DeskEntry::Kind::Volume)
            volumes.emplace_back(&e);

    return volumes;
}

std::string Desk::displayName(const fs::path &dir) const noexcept
{
    for (const DeskEntry *volume : volumes())
        if (volume->path == dir)
            return volume->name;

    const std::string name { dir.filename().string() };
    return name.empty() ? dir.string() : name;
}

void Desk::sortEntries(std::vector<DeskEntry> &entries) const noexcept
{
    // Disks always lead (the system one first): they aren't files.
    const auto rank = [this](const DeskEntry &e) -> int {
        if (e.kind == DeskEntry::Kind::Volume)
            return e.internal ? 0 : 1;

        // Kind: folders, then apps, then the other files (grouped by their kind's name).
        if (settings.sortBy == Settings::SortBy::Kind)
            return e.mimeType == "inode/directory" ? 2 : e.launcher ? 3 : 4;

        if (settings.sortBy == Settings::SortBy::Size && e.mimeType == "inode/directory")
            return 3; // Folders after files (no size), like Finder

        return 2;
    };

    const auto byName = [](const DeskEntry &a, const DeskEntry &b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };

    std::stable_sort(entries.begin(), entries.end(), [&](const DeskEntry &a, const DeskEntry &b) {
        if (const int ra { rank(a) }, rb { rank(b) }; ra != rb)
            return ra < rb;

        switch (settings.sortBy)
        {
        case Settings::SortBy::Kind:
            if (const int c { strcasecmp(a.kindName.c_str(), b.kindName.c_str()) }; c != 0)
                return c < 0;
            return byName(a, b);
        case Settings::SortBy::Size:
            if (a.size != b.size)
                return a.size > b.size; // Largest first
            return byName(a, b);
        default:
            return byName(a, b);
        }
    });
}

void Desk::setSortBy(Settings::SortBy sortBy) noexcept
{
    if (settings.sortBy == sortBy)
        return;

    settings.sortBy = sortBy;
    saveSettings();
    sortEntries(m_entries);
    onEntriesChanged.notify();
}

Int32 Desk::largerIconSize() const noexcept
{
    for (const Int32 size : DESK_ICON_SIZES)
        if (size > settings.iconSize)
            return size;

    return settings.iconSize;
}

Int32 Desk::smallerIconSize() const noexcept
{
    Int32 smaller { settings.iconSize };

    for (const Int32 size : DESK_ICON_SIZES)
        if (size < settings.iconSize)
            smaller = size;

    return smaller;
}

void Desk::setIconSize(Int32 size) noexcept
{
    size = std::clamp(size, DESK_ICON_SIZE_MIN, DESK_ICON_SIZE_MAX);

    if (settings.iconSize == size)
        return;

    settings.iconSize = size;
    saveSettings();
    onIconsChanged.notify();
}

void Desk::selectionChanged() noexcept
{
    if (m_menuTimer && !m_menuTimer->running())
        m_menuTimer->start(0);
}

void Desk::shortcut(UInt32 key, bool shift) noexcept
{
    if (menuBar)
        menuBar->shortcut(key, shift);
}

/* ----------------------------------------- D-Bus ----------------------------------------- */

/*
 * org.cuarzo.Desk on the session bus: lets the dock know the desktop is running (it shows a Desk
 * tile, like Finder's) and open a file manager window when it's clicked. Emits AppLaunched(s app
 * id) when the user opens an app (see announceLaunch()).
 */

static int NewWindowMethod(sd_bus_message *m, void *, sd_bus_error *)
{
    GetDesk()->openFolder(GetDesk()->xdgkit->homeDir(), true);
    return sd_bus_reply_method_return(m, "");
}

/*
 * org.freedesktop.FileManager1 (the file manager interface apps use, e.g. a browser's "Show in
 * folder"). URIs are file:// URIs; the startup id is an activation token.
 */

// file:///home/user/My%20File → /home/user/My File (empty if not a local file URI)
static fs::path PathFromURI(const std::string &uri) noexcept
{
    std::string_view rest { uri };

    if (rest.starts_with("file://"))
        rest.remove_prefix(7);
    else if (!rest.starts_with("/"))
        return {};

    // file://localhost/path
    if (!rest.starts_with("/"))
    {
        const auto slash { rest.find('/') };

        if (slash == std::string_view::npos || rest.substr(0, slash) != "localhost")
            return {};

        rest.remove_prefix(slash);
    }

    std::string path;

    for (size_t i = 0; i < rest.size(); i++)
    {
        if (rest[i] == '%' && i + 2 < rest.size() && std::isxdigit((unsigned char)rest[i + 1]) && std::isxdigit((unsigned char)rest[i + 2]))
        {
            path += char(std::stoi(std::string(rest.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        }
        else
            path += rest[i];
    }

    return fs::path(path).lexically_normal();
}

static int ShowURIs(sd_bus_message *m, bool items)
{
    std::vector<std::string> uris;
    char **strv { nullptr };
    const char *startupId { nullptr };

    int r { sd_bus_message_read_strv(m, &strv) };

    if (r >= 0)
        r = sd_bus_message_read(m, "s", &startupId);

    if (strv)
    {
        for (char **s = strv; *s; s++)
        {
            uris.emplace_back(*s);
            free(*s);
        }

        free(strv);
    }

    if (r < 0)
        return r;

    const std::string token { startupId ? startupId : "" };

    for (const auto &uri : uris)
    {
        const fs::path path { PathFromURI(uri) };

        if (path.empty())
        {
            Log(CZWarning, CZLN, "FileManager1: not a local file: {}", uri);
            continue;
        }

        // ShowItems: the folder holding it, with it selected
        if (items && path.has_parent_path() && path != path.root_path())
            GetDesk()->openFolder(path.parent_path(), false, path.filename().string(), token);
        else
            GetDesk()->openFolder(path, false, {}, token);
    }

    return sd_bus_reply_method_return(m, "");
}

static int ShowFoldersMethod(sd_bus_message *m, void *, sd_bus_error *) { return ShowURIs(m, false); }
static int ShowItemsMethod(sd_bus_message *m, void *, sd_bus_error *) { return ShowURIs(m, true); }

static const sd_bus_vtable FileManagerVTable[]
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ShowFolders", "ass", "", ShowFoldersMethod, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ShowItems", "ass", "", ShowItemsMethod, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ShowItemProperties", "ass", "", ShowItemsMethod, SD_BUS_VTABLE_UNPRIVILEGED), // No properties window yet
    SD_BUS_VTABLE_END
};

static const sd_bus_vtable DeskVTable[]
{
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("NewWindow", "", "", NewWindowMethod, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

void Desk::initBus() noexcept
{
    // Kept: the connection (and the name) goes away with its last reference.
    m_bus = CZBus::GetOrMakeUser();
    auto &bus { m_bus };

    if (!bus)
        return;

    int r { sd_bus_add_object_vtable(bus->bus(), nullptr, "/org/cuarzo/Desk", "org.cuarzo.Desk", DeskVTable, nullptr) };

    if (r >= 0)
        r = sd_bus_request_name(bus->bus(), "org.cuarzo.Desk", 0);

    if (r < 0)
        Log(CZWarning, CZLN, "Can't own org.cuarzo.Desk on the session bus: {}", strerror(-r));

    // The standard file manager interface (another file manager may own it: then it's theirs)
    r = sd_bus_add_object_vtable(bus->bus(), nullptr, "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", FileManagerVTable, nullptr);

    if (r >= 0)
        r = sd_bus_request_name(bus->bus(), "org.freedesktop.FileManager1", 0);

    if (r < 0)
        Log(CZWarning, CZLN, "Can't own org.freedesktop.FileManager1 on the session bus: {}", strerror(-r));
}

/* ------------------------------------ Desktop folder ------------------------------------- */

fs::path Desk::userDir(const std::string &key, const std::string &fallback) const noexcept
{
    const fs::path home { xdgkit->homeDir() };
    fs::path configHome { home / ".config" };

    if (const char *env { getenv("XDG_CONFIG_HOME") }; env && env[0] == '/')
        configHome = env;

    // user-dirs.dirs: XDG_DESKTOP_DIR="$HOME/Desktop"
    const std::string prefix { "XDG_" + key + "_DIR=" };
    std::ifstream in { configHome / "user-dirs.dirs" };
    std::string line;
    fs::path dir;

    while (std::getline(in, line))
    {
        if (!line.starts_with(prefix))
            continue;

        std::string value { line.substr(prefix.size()) };

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (value.starts_with("$HOME"))
            dir = home / value.substr(value.size() > 5 && value[5] == '/' ? 6 : 5);
        else if (!value.empty() && value.front() == '/')
            dir = value;
    }

    // Pointing at $HOME means "disabled".
    if (dir.empty() || dir == home)
        dir = home / fallback;

    return dir;
}

void Desk::initDesktopDir() noexcept
{
    m_desktopDir = userDir("DESKTOP", "Desktop");
    std::error_code ec;
    fs::create_directories(m_desktopDir, ec);
    Log(CZInfo, CZLN, "Desktop folder: {}", m_desktopDir.string());
}

void Desk::initWatches() noexcept
{
    m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (m_inotify < 0)
        Log(CZWarning, CZLN, "inotify unavailable ({}): the desktop won't refresh by itself", strerror(errno));
    else
    {
        m_desktopWatch = inotify_add_watch(m_inotify, m_desktopDir.c_str(),
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);

        std::error_code ec;
        fs::create_directories(Settings::FilePath().parent_path(), ec);
        m_configWatch = inotify_add_watch(m_inotify, Settings::FilePath().parent_path().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);

        // Firmament's settings (its bar size: the icons stay clear of the bar).
        const fs::path firmament { Settings::FilePath().parent_path().parent_path() / "cz-firmament" };
        fs::create_directories(firmament, ec);
        m_firmamentWatch = inotify_add_watch(m_inotify, firmament.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO);

        m_inotifySource = CZEventSource::Make(m_inotify, EPOLLIN, CZOwn::Borrow, [this](int fd, UInt32, CZEventSource*){
            alignas(inotify_event) char buf[4096];
            bool desktop { false }, config { false }, firmament { false };
            ssize_t n;

            while ((n = read(fd, buf, sizeof(buf))) > 0)
            {
                for (char *p = buf; p < buf + n; )
                {
                    const auto *e { reinterpret_cast<const inotify_event*>(p) };

                    if (e->wd == m_desktopWatch)
                        desktop = true;
                    else if (e->wd == m_configWatch && e->len > 0 && Settings::FilePath().filename() == e->name)
                        config = true;
                    else if (e->wd == m_firmamentWatch && e->len > 0 && std::strcmp(e->name, "firmament.conf") == 0)
                        firmament = true;

                    p += sizeof(inotify_event) + e->len;
                }
            }

            if (desktop)
                scheduleRescan();

            if (config)
                settingsFileChanged();

            if (firmament)
            {
                const float old { m_topbarScale };
                loadTopbarScale();

                if (old != m_topbarScale)
                    onIconsChanged.notify(); // Relayout below the new bar
            }
        });
    }

    // Mounts: /proc/self/mountinfo reports changes as an exceptional condition.
    const int mounts { ::open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC) };

    if (mounts >= 0)
        m_mountsSource = CZEventSource::Make(mounts, EPOLLPRI | EPOLLERR, CZOwn::Own, [this](int, UInt32, CZEventSource*){
            scheduleRescan();
        });
}

void Desk::scheduleRescan() noexcept
{
    m_rescanTimer->start(DESK_RESCAN_DELAY_MS);
}

static std::string Unescape(const std::string &s) noexcept
{
    // mountinfo escapes spaces, tabs, newlines and backslashes as \ooo
    std::string out;

    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 3 < s.size() &&
            std::isdigit((unsigned char)s[i + 1]) && std::isdigit((unsigned char)s[i + 2]) && std::isdigit((unsigned char)s[i + 3]))
        {
            out += char(std::stoi(s.substr(i + 1, 3), nullptr, 8));
            i += 3;
        }
        else
            out += s[i];
    }

    return out;
}

// The name shown for the system disk: the OS name (e.g. "Fedora Linux"), like "Macintosh HD".
static std::string SystemDiskName() noexcept
{
    for (const char *file : { "/etc/os-release", "/usr/lib/os-release" })
    {
        std::ifstream in { file };
        std::string line;

        while (std::getline(in, line))
        {
            if (!line.starts_with("NAME="))
                continue;

            std::string name { line.substr(5) };

            if (name.size() >= 2 && (name.front() == '"' || name.front() == '\''))
                name = name.substr(1, name.size() - 2);

            if (!name.empty())
                return name;
        }
    }

    return "Hard Drive";
}

// Whether a block device (e.g. /dev/sdb1) hangs from a USB bus.
static bool IsUSB(const std::string &device) noexcept
{
    if (!device.starts_with("/dev/"))
        return false;

    std::error_code ec;
    const fs::path sys { fs::canonical(fs::path("/sys/class/block") / fs::path(device).filename(), ec) };
    return !ec && sys.string().find("/usb") != std::string::npos;
}

std::vector<DeskEntry> Desk::scanVolumes() noexcept
{
    std::vector<DeskEntry> volumes;

    // Re-reading through the watched fd acknowledges the change.
    std::string content;

    if (m_mountsSource)
    {
        lseek(m_mountsSource->fd(), 0, SEEK_SET);
        char buf[4096];
        ssize_t n;

        while ((n = read(m_mountsSource->fd(), buf, sizeof(buf))) > 0)
            content.append(buf, size_t(n));
    }

    // The system disk first (like "Macintosh HD"), then removable media mounted by udisks (or
    // similar): /run/media/<user>/<label> or /media/...
    DeskEntry system;
    const std::string runMedia { "/run/media/" + xdgkit->username() + "/" };
    std::istringstream lines { content };
    std::string line;

    while (std::getline(lines, line))
    {
        // id parent major:minor root mountpoint options [optional fields...] - fstype source superoptions
        std::istringstream fields { line };
        std::string id, parent, devnum, root, mountPoint;
        fields >> id >> parent >> devnum >> root >> mountPoint;

        const auto sep { line.find(" - ") };

        if (sep == std::string::npos)
            continue;

        std::istringstream tail { line.substr(sep + 3) };
        std::string fsType, source;
        tail >> fsType >> source;

        mountPoint = Unescape(mountPoint);

        if (mountPoint == "/" && system.key.empty())
        {
            system.kind = DeskEntry::Kind::Volume;
            system.path = "/";
            system.key = "volume:/";
            system.name = SystemDiskName();
            system.mimeType = "inode/directory";
            system.device = Unescape(source);
            system.fsType = fsType;
            system.internal = true;
            continue;
        }

        if (fsType == "autofs" || !(mountPoint.starts_with(runMedia) || mountPoint.starts_with("/media/")))
            continue;

        // Only the top level mount of a volume (not nested mounts inside it).
        DeskEntry volume;
        volume.kind = DeskEntry::Kind::Volume;
        volume.path = mountPoint;
        volume.key = "volume:" + mountPoint;
        volume.name = fs::path(mountPoint).filename().string();
        volume.mimeType = "inode/directory";
        volume.device = Unescape(source);
        volume.fsType = fsType;
        volume.usb = IsUSB(volume.device);

        if (std::none_of(volumes.begin(), volumes.end(), [&](const DeskEntry &v){ return v.key == volume.key; }))
            volumes.emplace_back(std::move(volume));
    }

    std::sort(volumes.begin(), volumes.end(), [](const DeskEntry &a, const DeskEntry &b){ return a.name < b.name; });

    if (!system.key.empty())
        volumes.insert(volumes.begin(), std::move(system));

    return volumes;
}

std::vector<DeskEntry> Desk::scanFolder(const fs::path &dir, bool hidden) const noexcept
{
    return ScanFolder(*xdgkit, dir, hidden);
}

std::vector<DeskEntry> Desk::ScanFolder(XDGKit &kit, const fs::path &dir, bool hidden) noexcept
{
    std::vector<DeskEntry> files;
    std::error_code ec;
    auto &mime { kit.mimeDatabase() };

    for (const auto &dirEntry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
    {
        const std::string name { dirEntry.path().filename().string() };

        // Hidden and backup files.
        if (name.empty() || (!hidden && (name.front() == '.' || name.back() == '~')))
            continue;

        DeskEntry file;
        file.kind = DeskEntry::Kind::File;
        file.path = dirEntry.path();
        file.key = name;
        file.name = name;
        file.mimeType = mime.typeForFile(file.path);

        file.kindName = file.mimeType == "inode/directory" ? "Folder" : mime.description(file.mimeType);

        if (std::error_code sec; dirEntry.is_regular_file(sec))
            file.size = dirEntry.file_size(sec);

        if (std::error_code tec; true)
            if (const auto time { dirEntry.last_write_time(tec) }; !tec)
                file.modified = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(time));

        if (file.mimeType == "application/x-desktop" || name.ends_with(".desktop"))
        {
            if ((file.launcher = XDGDesktopEntry::Load(file.path)) && !file.launcher->name.empty())
                file.name = file.launcher->name;
        }

        files.emplace_back(std::move(file));
    }

    // Like Finder: natural-ish, case-insensitive.
    std::sort(files.begin(), files.end(), [](const DeskEntry &a, const DeskEntry &b){
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return files;
}

std::vector<DeskEntry> Desk::scanFiles() noexcept
{
    return scanFolder(m_desktopDir);
}

void Desk::rescan() noexcept
{
    if (m_rescanTimer)
        m_rescanTimer->stop();

    // The desktop folder itself may have been (re)created, moved or deleted.
    if (m_inotify >= 0)
    {
        std::error_code ec;
        fs::create_directories(m_desktopDir, ec);

        if (m_desktopWatch >= 0)
            inotify_rm_watch(m_inotify, m_desktopWatch);

        m_desktopWatch = inotify_add_watch(m_inotify, m_desktopDir.c_str(),
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
    }

    std::vector<DeskEntry> entries { scanVolumes() };
    auto files { scanFiles() };
    entries.insert(entries.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    sortEntries(entries);

    const auto same = [](const std::vector<DeskEntry> &a, const std::vector<DeskEntry> &b) {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); i++)
            if (a[i].key != b[i].key || a[i].name != b[i].name || a[i].mimeType != b[i].mimeType)
                return false;

        return true;
    };

    const bool changed { !same(entries, m_entries) };
    m_entries = std::move(entries);

    // Thumbnails of modified pictures are refreshed even if the list didn't change.
    assets.pruneThumbnails();

    if (changed)
        Log(CZDebug, CZLN, "{} desktop item(s)", m_entries.size());

    onEntriesChanged.notify();
}

const DeskEntry *Desk::entry(const std::string &key) const noexcept
{
    for (const auto &e : m_entries)
        if (e.key == key)
            return &e;

    return nullptr;
}

/* ---------------------------------------- Actions ---------------------------------------- */

std::string Desk::FormatBytes(UInt64 bytes) noexcept
{
    static const char *units[] { "bytes", "KB", "MB", "GB", "TB", "PB" };
    double value { double(bytes) };
    size_t unit { 0 };

    while (value >= 1000.0 && unit + 1 < std::size(units))
    {
        value /= 1000.0;
        unit++;
    }

    char buf[32];

    if (unit == 0)
        snprintf(buf, sizeof(buf), bytes == 1 ? "%llu byte" : "%llu %s", (unsigned long long)bytes, units[0]);
    else
        snprintf(buf, sizeof(buf), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);

    return buf;
}

std::string Desk::FormatDate(std::time_t time) noexcept
{
    if (time == 0)
        return "--";

    std::tm when {}, now {};
    const std::time_t current { std::time(nullptr) };
    localtime_r(&time, &when);
    localtime_r(&current, &now);

    // Days apart (calendar days, not 24 h periods)
    std::tm whenDay { when }, nowDay { now };
    whenDay.tm_hour = nowDay.tm_hour = 12;
    whenDay.tm_min = nowDay.tm_min = whenDay.tm_sec = nowDay.tm_sec = 0;
    const long days { std::lround(std::difftime(std::mktime(&nowDay), std::mktime(&whenDay)) / 86400.0) };

    char clock[16];
    std::strftime(clock, sizeof(clock), "%H:%M", &when);

    if (days == 0)
        return std::string("Today at ") + clock;

    if (days == 1)
        return std::string("Yesterday at ") + clock;

    char date[64];
    std::strftime(date, sizeof(date), "%e %b %Y at %H:%M", &when);
    std::string out { date };

    if (!out.empty() && out.front() == ' ')
        out.erase(0, 1);

    return out;
}

bool Desk::Spawn(const std::vector<std::string> &args) noexcept
{
    // Detached, started from a background thread: never blocks the UI
    return CZProcess::SpawnDetached(args);
}

std::vector<std::shared_ptr<const XDGDesktopEntry>> Desk::appsFor(const DeskEntry &entry) noexcept
{
    return xdgkit->mimeDatabase().apps(entry.mimeType);
}

std::unordered_set<std::string> Desk::open(const std::vector<const DeskEntry*> &entries, const std::unordered_map<std::string, IconOrigin> *origins) noexcept
{
    std::unordered_set<std::string> fromIcon;

    for (const DeskEntry *e : entries)
    {
        if (e->launcher)
        {
            if (e->launcher->launch())
                announceLaunch(*e->launcher);

            continue;
        }

        // Folders and disks: a window of ours (like Finder)
        if (e->kind == DeskEntry::Kind::Volume || e->mimeType == "inode/directory")
        {
            const IconOrigin *origin { nullptr };

            if (origins)
                if (auto it { origins->find(e->path.string()) }; it != origins->end())
                    origin = &it->second;

            if (openFolder(e->path, false, {}, {}, origin))
                fromIcon.insert(e->path.string());

            continue;
        }

        if (auto app { xdgkit->mimeDatabase().defaultApp(e->mimeType) })
        {
            Log(CZInfo, CZLN, "Opening {} ({}) with {}", e->path.string(), e->mimeType, app->id);

            if (app->open({ e->path }))
                announceLaunch(*app);

            continue;
        }

        // Let the session's opener try (it may know more handlers, e.g. portals).
        Log(CZInfo, CZLN, "No app found for {} ({}): trying xdg-open", e->path.string(), e->mimeType);
        Spawn({ "xdg-open", e->path.string() });
    }

    return fromIcon;
}

void Desk::openWith(const DeskEntry &entry, const XDGDesktopEntry &app) noexcept
{
    Log(CZInfo, CZLN, "Opening {} with {}", entry.path.string(), app.id);

    if (app.open({ entry.path }))
        announceLaunch(app);
}

void Desk::announceLaunch(const XDGDesktopEntry &app) noexcept
{
    // The user launched it (like from the dock): Crystals shows its first window, switching to the
    // workspace where it opens (other windows appearing elsewhere only make their icon bounce)
    if (!m_bus || app.id.empty())
        return;

    sd_bus_emit_signal(m_bus->bus(), "/org/cuarzo/Desk", "org.cuarzo.Desk", "AppLaunched", "s", app.id.c_str());

    // Its windows' app id, if it's another one (Crystals matches it against theirs)
    if (!app.startupWMClass.empty() && app.startupWMClass != app.id)
        sd_bus_emit_signal(m_bus->bus(), "/org/cuarzo/Desk", "org.cuarzo.Desk", "AppLaunched", "s", app.startupWMClass.c_str());

    sd_bus_flush(m_bus->bus());
}

void Desk::trash(const std::vector<const DeskEntry*> &entries) noexcept
{
    for (const DeskEntry *e : entries)
    {
        if (e->kind != DeskEntry::Kind::File)
            continue;

        std::string error;

        switch (XDGTrash::MoveToTrash(e->path, &error))
        {
        case XDGTrash::Result::Trashed:
            Log(CZInfo, CZLN, "Moved {} to the trash", e->path.string());
            break;
        case XDGTrash::Result::OtherDevice:
            // On another file system: its own trash directory is gio's business.
            Spawn({ "gio", "trash", e->path.string() });
            break;
        case XDGTrash::Result::Failed:
            Log(CZError, CZLN, "Can't move {} to the trash: {}", e->path.string(), error);
            break;
        }
    }

    scheduleRescan();
}

/* --------------------------------------- Drag & drop --------------------------------------- */

// Reads all of @p fd (another process writes it), giving up after @p timeoutMs.
static std::string ReadAll(Int32 fd, Int32 timeoutMs) noexcept
{
    std::string data;
    const auto deadline { std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs) };
    char buf[4096];

    while (true)
    {
        const auto left { std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count() };

        if (left <= 0)
        {
            Log(CZWarning, CZLN, "Timed out reading the dropped files");
            break;
        }

        pollfd pfd { fd, POLLIN, 0 };

        if (poll(&pfd, 1, Int32(left)) <= 0)
            continue;

        const ssize_t n { read(fd, buf, sizeof(buf)) };

        if (n > 0)
            data.append(buf, size_t(n));
        else if (n == 0 || errno != EINTR)
            break;
    }

    return data;
}

static bool IsOwnDrag(const Desk &desk, MDNDOffer &offer) noexcept
{
    return !desk.dragFiles().empty() && (offer.hasMimeType(DESK_ITEMS_MIME_TYPE) || offer.hasMimeType(DESK_FILES_MIME_TYPE));
}

bool Desk::offersFiles(MDNDOffer &offer) const noexcept
{
    // The dock's apps are launchers, not files to move
    return IsOwnDrag(*this, offer) || (offer.hasMimeType("text/uri-list") && !offer.hasMimeType("application/x-cz-shelf-app"));
}

std::string Desk::URIList(const std::vector<std::filesystem::path> &files) noexcept
{
    static constexpr char hex[] { "0123456789ABCDEF" };
    std::string uris;

    for (const auto &file : files)
    {
        std::string encoded;

        for (const unsigned char c : file.string())
        {
            if (std::isalnum(c) || std::strchr("/-._~", c))
                encoded += char(c);
            else
            {
                encoded += '%';
                encoded += hex[c >> 4];
                encoded += hex[c & 0xF];
            }
        }

        uris += "file://" + encoded + "\r\n";
    }

    return uris;
}

std::vector<std::filesystem::path> Desk::dropInto(const std::filesystem::path &dir, bool copyByDefault, MDNDOffer &offer) noexcept
{
    // The action chosen by the compositor (e.g. a modifier held), else the target's default.
    const bool copy { offer.action() == MDNDOffer::Copy || (offer.action() != MDNDOffer::Move && copyByDefault) };
    auto files { receiveDrop(offer) };
    transfer(files, dir, copy);
    return files;
}

std::vector<std::filesystem::path> Desk::receiveDrop(MDNDOffer &offer) noexcept
{
    std::vector<std::filesystem::path> files;

    if (IsOwnDrag(*this, offer))
    {
        // Desk's own (a desktop's icons, a folder window's files): the files are known (and Desk
        // itself is the source, so reading its data here would wait for ourselves).
        files = m_dragFiles;

        if (const Int32 fd { offer.receive(offer.hasMimeType(DESK_ITEMS_MIME_TYPE) ? DESK_ITEMS_MIME_TYPE : DESK_FILES_MIME_TYPE) }; fd >= 0)
            close(fd);
    }
    else if (const Int32 fd { offer.receive("text/uri-list") }; fd >= 0)
    {
        // Another app: a list of URIs, one per line ('#' lines are comments). Small, written right
        // away by the source; a short timeout in case it never does.
        const std::string list { ReadAll(fd, 1000) };
        close(fd);

        size_t start { 0 };

        while (start < list.size())
        {
            size_t end { list.find('\n', start) };

            if (end == std::string::npos)
                end = list.size();

            std::string line { list.substr(start, end - start) };
            start = end + 1;

            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();

            if (line.empty() || line.front() == '#')
                continue;

            if (auto path { PathFromURI(line) }; !path.empty())
                files.emplace_back(std::move(path));
        }
    }

    offer.finish();
    return files;
}

/* ---------------------------------------- Favorites ---------------------------------------- */

static fs::path NormalizedDir(const fs::path &dir) noexcept
{
    std::error_code ec;
    fs::path normal { fs::weakly_canonical(dir, ec) };

    if (ec)
        normal = dir.lexically_normal();

    if (normal.has_relative_path() && normal.filename().empty())
        normal = normal.parent_path();

    return normal;
}

std::vector<fs::path> Desk::favorites() const noexcept
{
    if (settings.favorites)
        return *settings.favorites;

    // Like Finder's: home, the desktop, the apps and the other user folders (those that exist)
    std::vector<fs::path> dirs;
    std::error_code ec;

    for (const fs::path &dir : { fs::path(xdgkit->homeDir()), desktopDir(), fs::path("/usr/share/applications"), userDir("DOCUMENTS", "Documents"),
                                 userDir("DOWNLOAD", "Downloads"), userDir("PICTURES", "Pictures"),
                                 userDir("MUSIC", "Music"), userDir("VIDEOS", "Videos") })
    {
        const fs::path normal { NormalizedDir(dir) };

        if (fs::is_directory(normal, ec) && std::find(dirs.begin(), dirs.end(), normal) == dirs.end())
            dirs.emplace_back(normal);
    }

    return dirs;
}

void Desk::placeFavorites(const std::vector<fs::path> &added, size_t index) noexcept
{
    auto dirs { favorites() };
    std::vector<fs::path> placed;
    std::error_code ec;

    for (const auto &dir : added)
    {
        const fs::path normal { NormalizedDir(dir) };

        if (!fs::is_directory(normal, ec) || std::find(placed.begin(), placed.end(), normal) != placed.end())
            continue;

        // Already there: moved (the place counts without it)
        if (auto it { std::find(dirs.begin(), dirs.end(), normal) }; it != dirs.end())
        {
            if (size_t(it - dirs.begin()) < index)
                index--;

            dirs.erase(it);
        }

        placed.emplace_back(normal);
    }

    if (placed.empty())
        return;

    index = std::min(index, dirs.size());
    dirs.insert(dirs.begin() + Int64(index), placed.begin(), placed.end());
    settings.favorites = std::move(dirs);
    saveSettings();
    onFavoritesChanged.notify();
}

void Desk::removeFavorite(const fs::path &dir) noexcept
{
    auto dirs { favorites() };
    const fs::path normal { NormalizedDir(dir) };

    if (std::erase_if(dirs, [&normal](const fs::path &d){ return NormalizedDir(d) == normal; }) == 0)
        return;

    // Saved even if empty (no favorites, not the defaults)
    settings.favorites = std::move(dirs);
    saveSettings();
    onFavoritesChanged.notify();
}

void Desk::transfer(const std::vector<std::filesystem::path> &files, const std::filesystem::path &dir, bool copy) noexcept
{
    std::error_code ec;
    const auto target { std::filesystem::weakly_canonical(dir, ec) };
    struct stat targetStat {};

    if (ec || !std::filesystem::is_directory(target, ec) || stat(target.c_str(), &targetStat) != 0)
        return;

    // cp/mv: without replacing what's there (-n), into the folder (-t), recursive copies.
    std::vector<std::string> moves { "mv", "-n", "-t", target.string(), "--" };
    std::vector<std::string> copies { "cp", "-rn", "-t", target.string(), "--" };
    const size_t fixed { moves.size() };

    for (const auto &file : files)
    {
        // The file itself (a symlink is moved as such, not what it points to), in a real folder.
        const auto abs { std::filesystem::absolute(file, ec).lexically_normal() };

        if (ec || abs.filename().empty())
            continue;

        const auto src { std::filesystem::weakly_canonical(abs.parent_path(), ec) / abs.filename() };
        struct stat srcStat {};

        if (ec || lstat(src.c_str(), &srcStat) != 0)
            continue;

        // Not into itself (or a folder inside it).
        const auto rel { target.lexically_relative(src) };

        if (!rel.empty() && *rel.begin() != "..")
            continue;

        // Copied where it already is: a copy next to it with another name ("Notes copy.txt", like Finder)
        if (copy && src.parent_path() == target)
        {
            const std::string stem { src.stem().string() }, ext { fs::is_directory(src, ec) ? std::string() : src.extension().string() };
            const std::string base { fs::is_directory(src, ec) ? src.filename().string() : stem };
            fs::path dest { target / (base + " copy" + ext) };

            for (int n { 2 }; fs::exists(fs::symlink_status(dest, ec)); n++)
                dest = target / (base + " copy " + std::to_string(n) + ext);

            Log(CZInfo, CZLN, "Copying {} to {}", src.string(), dest.string());
            Spawn({ "cp", "-rn", "--", src.string(), dest.string() });
            continue;
        }

        // Like Finder: moving to another disk copies.
        if (copy || srcStat.st_dev != targetStat.st_dev)
            copies.emplace_back(src.string());
        else if (src.parent_path() != target) // Not where it already is
            moves.emplace_back(src.string());
    }

    if (moves.size() > fixed)
    {
        Log(CZInfo, CZLN, "Moving {} item(s) into {}", moves.size() - fixed, target.string());
        Spawn(moves);
    }

    if (copies.size() > fixed)
    {
        Log(CZInfo, CZLN, "Copying {} item(s) into {}", copies.size() - fixed, target.string());
        Spawn(copies);
    }
}

/* ---------------------------------------- Clipboard ---------------------------------------- */

static constexpr const char *CopiedFilesMime { "x-special/gnome-copied-files" };

// The app's latest input event (the compositor gives the clipboard to clients that just got input;
// menu bar clicks come from the bar's process)
static const CZInputEvent &LatestInput() noexcept
{
    auto app { AKApp::Get() };
    const CZInputEvent &button { app->pointer().history().button };
    const CZInputEvent &key { app->keyboard().history().key };
    return key.serial > button.serial ? key : button;
}

void Desk::copyFiles(const std::vector<fs::path> &files, bool cut) noexcept
{
    AKClipboard *clipboard { AKApp::Get() ? AKApp::Get()->clipboard() : nullptr };

    if (!clipboard || files.empty())
        return;

    std::string uris { URIList(files) }; // "file://...\r\n" each
    std::string copied { cut ? "cut" : "copy" }, paths;

    for (size_t start { 0 }; start < uris.size();)
    {
        const size_t end { uris.find("\r\n", start) };
        copied += "\n" + uris.substr(start, end - start);
        start = end == std::string::npos ? uris.size() : end + 2;
    }

    for (const auto &file : files)
        paths += (paths.empty() ? "" : "\n") + file.string();

    Log(CZInfo, CZLN, "{} {} item(s)", cut ? "Cut" : "Copied", files.size());
    clipboard->setData({ { CopiedFilesMime, copied }, { "text/uri-list", uris }, { "text/plain;charset=utf-8", paths } }, LatestInput());
}

bool Desk::canPaste() const noexcept
{
    AKClipboard *clipboard { AKApp::Get() ? AKApp::Get()->clipboard() : nullptr };

    if (!clipboard)
        return false;

    const auto types { clipboard->mimeTypes() };
    return std::find(types.begin(), types.end(), CopiedFilesMime) != types.end() ||
           std::find(types.begin(), types.end(), "text/uri-list") != types.end();
}

void Desk::pasteInto(const fs::path &dir) noexcept
{
    AKClipboard *clipboard { AKApp::Get() ? AKApp::Get()->clipboard() : nullptr };

    if (!clipboard)
        return;

    clipboard->requestData({ CopiedFilesMime, "text/uri-list" }, [dir](std::optional<AKClipboard::Format> data){
        if (!data)
            return;

        // "copy" or "cut", then a URI per line (gnome-copied-files), or only URIs ('#': comments)
        std::vector<fs::path> files;
        bool cut { false }, first { true };
        size_t start { 0 };
        const std::string &list { data->second };

        while (start < list.size())
        {
            size_t end { list.find('\n', start) };

            if (end == std::string::npos)
                end = list.size();

            std::string line { list.substr(start, end - start) };
            start = end + 1;

            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();

            if (first && data->first == CopiedFilesMime)
            {
                first = false;
                cut = line == "cut";
                continue;
            }

            if (line.empty() || line.front() == '#')
                continue;

            if (auto path { PathFromURI(line) }; !path.empty())
                files.emplace_back(std::move(path));
        }

        if (!files.empty())
            GetDesk()->transfer(files, dir, !cut);
    });
}

void Desk::eject(const DeskEntry &volume) noexcept
{
    if (volume.kind != DeskEntry::Kind::Volume || volume.internal || volume.device.empty())
        return;

    Log(CZInfo, CZLN, "Ejecting {} ({})", volume.name, volume.device);
    Spawn({ "sh", "-c", "udisksctl unmount --no-user-interaction -b \"$1\" && udisksctl power-off --no-user-interaction -b \"$1\"",
            "sh", volume.device });
}

std::string Desk::newFolder() noexcept
{
    const std::string name { newFolder(m_desktopDir) };

    if (!name.empty())
        rescan();

    return name;
}

std::string Desk::newFolder(const fs::path &dir) noexcept
{
    std::string name { "untitled folder" };
    std::error_code ec;

    for (int i = 2; fs::exists(dir / name, ec); i++)
        name = "untitled folder " + std::to_string(i);

    if (!fs::create_directory(dir / name, ec))
    {
        Log(CZError, CZLN, "Can't create {}: {}", (dir / name).string(), ec.message());
        return {};
    }

    return name;
}

std::string Desk::rename(const DeskEntry &entry, const std::string &name) noexcept
{
    if (entry.kind != DeskEntry::Kind::File)
        return {};

    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos || name.find('\0') != std::string::npos)
    {
        Log(CZWarning, CZLN, "Can't rename {}: invalid name \"{}\"", entry.path.string(), name);
        return {};
    }

    const fs::path to { entry.path.parent_path() / name };

    if (to == entry.path)
        return entry.key;

    std::error_code ec;

    // Taken (unless it's the same file, e.g. only the letter case changes on a case-insensitive disk)
    if (fs::exists(fs::symlink_status(to, ec)) && !fs::equivalent(entry.path, to, ec))
    {
        Log(CZWarning, CZLN, "Can't rename {}: \"{}\" already exists", entry.path.string(), name);
        return {};
    }

    fs::rename(entry.path, to, ec);

    if (ec)
    {
        Log(CZError, CZLN, "Can't rename {} to \"{}\": {}", entry.path.string(), name, ec.message());
        return {};
    }

    // Keep its place (keys are file names)
    const std::string key { to.filename().string() };

    if (auto it { settings.positions.find(entry.key) }; entry.path.parent_path() == m_desktopDir && it != settings.positions.end())
    {
        const SkIPoint cell { it->second };
        settings.positions.erase(it);
        settings.positions[key] = cell;
        saveSettings();
    }

    Log(CZInfo, CZLN, "Renamed {} to \"{}\"", entry.path.string(), name);

    if (entry.path.parent_path() == m_desktopDir)
        rescan();
    return key;
}

/* ---------------------------------------- Settings ---------------------------------------- */

void Desk::setPicture(const fs::path &picture) noexcept
{
    // A copy, overwriting the previous one: shown even if the original goes away
    settings.picture = Settings::StorePicture(picture);
    settings.usePicture = true;
    saveSettings();
    onBackgroundChanged.notify();
}

void Desk::setUsePicture(bool use) noexcept
{
    if (settings.usePicture == use)
        return;

    settings.usePicture = use;
    saveSettings();
    onBackgroundChanged.notify();
}

void Desk::setFit(Settings::Fit fit) noexcept
{
    if (settings.fit == fit)
        return;

    settings.fit = fit;
    saveSettings();
    onBackgroundChanged.notify();
}

void Desk::setPosition(const std::string &key, const SkIPoint *cell) noexcept
{
    if (cell)
        settings.positions[key] = *cell;
    else
        settings.positions.erase(key);
}

void Desk::clearPositions() noexcept
{
    if (settings.positions.empty())
        return;

    settings.positions.clear();
    saveSettings();
    onIconsChanged.notify();
}

void Desk::saveSettings() noexcept
{
    settings.save();
}

void Desk::settingsFileChanged() noexcept
{
    Settings old { settings };
    settings.load();

    // Our own saves come back here too: only react to actual differences.
    if (old.picture != settings.picture || old.usePicture != settings.usePicture ||
        old.fit != settings.fit || old.color != settings.color)
    {
        Log(CZInfo, CZLN, "Background settings changed");
        onBackgroundChanged.notify();
    }

    if (old.sortBy != settings.sortBy)
    {
        sortEntries(m_entries);
        onEntriesChanged.notify();
    }

    if (old.iconSize != settings.iconSize || old.positions != settings.positions)
        onIconsChanged.notify();
}
