#include <sys/stat.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <Surfaces/Background.h>
#include <Core/MenuBar.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <CZ/XDG/XDGMimeDatabase.h>
#include <CZ/XDG/XDGTrash.h>
#include <cmath>
#include <CZ/XDG/XDGINI.h>
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

    // Timers register with the CZCore when created: only once Marco made it.
    m_rescanTimer = std::make_unique<CZTimer>([this](CZTimer *){ rescan(); });

    xdgkit = XDGKit::Make();
    settings.load();
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
    m_topbarScale = 1.f;
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

void Desk::setActiveBackground(Background *background) noexcept
{
    if (m_activeBackground.get() == background)
        return;

    // A single selection at a time: the menus and the keyboard act on it.
    if (m_activeBackground)
        static_cast<Background*>(m_activeBackground.get())->clearSelection();

    m_activeBackground.reset(background);
    selectionChanged();
}

void Desk::openFolder(const fs::path &path) noexcept
{
    DeskEntry dir;
    dir.path = path;
    dir.name = path.filename().string();
    dir.mimeType = "inode/directory";
    open({ &dir });
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
 * tile, like Finder's) and open a file manager window when it's clicked.
 */

static int NewWindowMethod(sd_bus_message *m, void *, sd_bus_error *)
{
    GetDesk()->openFolder(GetDesk()->xdgkit->homeDir());
    return sd_bus_reply_method_return(m, "");
}

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

std::vector<DeskEntry> Desk::scanFiles() noexcept
{
    std::vector<DeskEntry> files;
    std::error_code ec;
    auto &mime { xdgkit->mimeDatabase() };

    for (const auto &dirEntry : fs::directory_iterator(m_desktopDir, fs::directory_options::skip_permission_denied, ec))
    {
        const std::string name { dirEntry.path().filename().string() };

        // Hidden and backup files.
        if (name.empty() || name.front() == '.' || name.back() == '~')
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

bool Desk::Spawn(const std::vector<std::string> &args) noexcept
{
    if (args.empty())
        return false;

    std::vector<char*> argv;
    for (const auto &a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    const pid_t child { fork() };

    if (child < 0)
        return false;

    if (child == 0)
    {
        // Double fork so the process is reparented to init and never becomes our zombie.
        setsid();
        if (fork() != 0) _exit(0);
        syscall(SYS_close_range, 3, ~0U, 0);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    waitpid(child, nullptr, 0);
    return true;
}

std::vector<std::shared_ptr<const XDGDesktopEntry>> Desk::appsFor(const DeskEntry &entry) noexcept
{
    return xdgkit->mimeDatabase().apps(entry.mimeType);
}

void Desk::open(const std::vector<const DeskEntry*> &entries) noexcept
{
    for (const DeskEntry *e : entries)
    {
        if (e->launcher)
        {
            e->launcher->launch();
            continue;
        }

        if (auto app { xdgkit->mimeDatabase().defaultApp(e->mimeType) })
        {
            Log(CZInfo, CZLN, "Opening {} ({}) with {}", e->path.string(), e->mimeType, app->id);
            app->open({ e->path });
            continue;
        }

        // Let the session's opener try (it may know more handlers, e.g. portals).
        Log(CZInfo, CZLN, "No app found for {} ({}): trying xdg-open", e->path.string(), e->mimeType);
        Spawn({ "xdg-open", e->path.string() });
    }
}

void Desk::openWith(const DeskEntry &entry, const XDGDesktopEntry &app) noexcept
{
    Log(CZInfo, CZLN, "Opening {} with {}", entry.path.string(), app.id);
    app.open({ entry.path });
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
    std::string name { "untitled folder" };
    std::error_code ec;

    for (int i = 2; fs::exists(m_desktopDir / name, ec); i++)
        name = "untitled folder " + std::to_string(i);

    if (!fs::create_directory(m_desktopDir / name, ec))
    {
        Log(CZError, CZLN, "Can't create {}: {}", (m_desktopDir / name).string(), ec.message());
        return {};
    }

    rescan();
    return name;
}

/* ---------------------------------------- Settings ---------------------------------------- */

void Desk::setPicture(const fs::path &picture) noexcept
{
    settings.picture = picture;
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
