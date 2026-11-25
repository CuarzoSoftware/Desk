#include <Window/FolderWindow.h>
#include <Core/Log.h>
#include <Core/FolderScanner.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/CZCore.h>
#include <CZ/Core/CZKeymap.h>
#include <CZ/Core/CZTime.h>
#include <CZ/Core/CZEventSource.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZKeyboardKeyEvent.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <linux/input-event-codes.h>
#include <sys/inotify.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace fs = std::filesystem;

static bool KeyHeld(UInt32 left, UInt32 right) noexcept
{
    auto keymap { CZCore::Get()->keymap() };
    return keymap && (keymap->pressedKeys().contains(left) || keymap->pressedKeys().contains(right));
}

static bool CtrlHeld() noexcept { return KeyHeld(KEY_LEFTCTRL, KEY_RIGHTCTRL); }
static bool ShiftHeld() noexcept { return KeyHeld(KEY_LEFTSHIFT, KEY_RIGHTSHIFT); }
static bool AltHeld() noexcept { return KeyHeld(KEY_LEFTALT, KEY_RIGHTALT); }

// E.g. "1,2 GB" (decimal units, like Finder)
static std::string FormatBytes(UInt64 bytes) noexcept
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
        snprintf(buf, sizeof(buf), "%llu %s", (unsigned long long)bytes, units[0]);
    else
        snprintf(buf, sizeof(buf), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);

    return buf;
}

FolderWindow::FolderWindow(const fs::path &dir) noexcept
{
    setColor(CZAdaptiveColor(0)); // Transparent: the sidebar shows the background blur
    layout().setFlexDirection(YGFlexDirectionRow);
    layout().setWidth(DESK_WINDOW_WIDTH);
    layout().setHeight(DESK_WINDOW_HEIGHT);
    setMinSize(SkISize::Make(Int32(sidebar.minWidth()) + DESK_WINDOW_MIN_CONTENT_WIDTH, DESK_WINDOW_MIN_HEIGHT));
    setKeyboardFocusable(true); // Takes the keyboard when clicked (see pointerButtonEvent())

    sidebar.onFolderClicked.subscribe(this, [this](const fs::path &folder){ go(folder); });
    content.back.onClick.subscribe(this, [this](const auto &){ goBack(); });
    content.forward.onClick.subscribe(this, [this](const auto &){ goForward(); });

    m_rescanTimer.setCallback([this](CZTimer*){ rescan(); });

    // The icons: cells bound to the files in view
    content.grid.makeCell = []{ return std::make_unique<FolderItem>(); };
    content.grid.bindCell = [this](AKNode &cell, size_t index){
        auto &item { static_cast<FolderItem&>(cell) };
        item.setEntry(m_entries[index]);
        item.setSelected(isSelected(index));
    };

    auto *desk { GetDesk() };

    desk->assets.onThumbnailReady.subscribe(this, [this](const fs::path &path){
        for (const auto &[index, cell] : content.grid.visibleCells())
            static_cast<FolderItem*>(cell)->thumbnailReady(path.string());
    });

    // Disks ejected (its folder may be gone)
    desk->onEntriesChanged.subscribe(this, [this]{ leaveIfGone(); });

    m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (m_inotify >= 0)
        m_inotifySource = CZEventSource::Make(m_inotify, EPOLLIN, CZOwn::Borrow, [this](int fd, UInt32, CZEventSource*){
            alignas(inotify_event) char buf[4096];

            while (read(fd, buf, sizeof(buf)) > 0) {}

            // Bursts (e.g. copying many files) are coalesced
            m_rescanTimer.start(DESK_RESCAN_DELAY_MS);
        });
    else
        Log(CZWarning, CZLN, "inotify unavailable ({}): folder windows won't refresh by themselves", strerror(errno));

    navigate(dir);
    updateNavigation();
}

FolderWindow::~FolderWindow() noexcept
{
    if (m_scanId && GetDesk()->scanner)
        GetDesk()->scanner->cancel(m_scanId);

    m_inotifySource.reset();

    if (m_inotify >= 0)
        close(m_inotify);
}

FolderWindow::Content::Content(AKNode *parent) noexcept : AKSolidColor(AKTheme::WindowBackground, parent)
{
    layout().setFlex(1.f);
    layout().setMinWidth(DESK_WINDOW_MIN_CONTENT_WIDTH);
    layout().setHeightPercent(100.f);
    enableChildrenClipping(false); // The separator is drawn just outside its left edge

    toolbar.userCaps.add(UCWindowMove);
    toolbar.layout().setHeight(MSidebar::HeaderHeight);
    toolbar.layout().setFlexShrink(0.f);
    toolbar.layout().setAlignItems(YGAlignCenter);
    toolbar.layout().setPadding(YGEdgeHorizontal, 12.f);
    toolbar.layout().setGap(YGGutterColumn, 4.f);

    const SkRegion empty;
    title.setFontSize(15.f);
    title.setFontStyle(SkFontStyle(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    title.setTextColor(AKTheme::Label);
    title.layout().setMargin(YGEdgeLeft, 8.f);
    title.layout().setFlexShrink(1.f);
    title.setInputRegion(&empty);

    // Cells of the same size, as many per row as fit
    grid.layout().setFlex(1.f);
    grid.setCellSize(FolderItem::CellSize());
    grid.setSpacing(0.f, DESK_WINDOW_GRID_GAP);
    grid.setContentPadding(DESK_WINDOW_GRID_PADDING);
    grid.setRowBackground(AKTheme::WindowBackground); // Like the window's: opaque rows, faster to draw

    statusBar.layout().setHeight(24.f);
    statusBar.layout().setFlexShrink(0.f);
    statusBar.layout().setAlignItems(YGAlignCenter);
    statusBar.layout().setJustifyContent(YGJustifyCenter);
    status.setFontSize(11.f);
    status.setTextColor(AKTheme::SecondaryLabel);
    status.setInputRegion(&empty);

    separator.enableDiminishOpacityOnInactive(true);

    // The toolbar's area, over everything else (added last): its shadow falls on the icons
    toolbarShadowArea.setInputRegion(&empty);
    toolbarShadowArea.layout().setPositionType(YGPositionTypeAbsolute);
    toolbarShadowArea.layout().setPosition(YGEdgeTop, 0.f);
    toolbarShadowArea.layout().setPosition(YGEdgeLeft, 0.f);
    toolbarShadowArea.layout().setWidthPercent(100.f);
    toolbarShadowArea.layout().setHeight(MSidebar::HeaderHeight);
    toolbarShadow.setStrength(0.6f); // Subtle, like Finder's
    toolbarShadow.showWhileScrolled(&grid);
}

/* --------------------------------------- Navigation --------------------------------------- */

fs::path FolderWindow::Normalized(const fs::path &dir) noexcept
{
    std::error_code ec;
    fs::path normal { fs::weakly_canonical(dir, ec) };

    if (ec)
        normal = dir.lexically_normal();

    // No trailing separator ("/home/user/" → "/home/user"), except for the root
    if (normal.has_relative_path() && normal.filename().empty())
        normal = normal.parent_path();

    return normal;
}

void FolderWindow::select(const std::string &name) noexcept
{
    if (name.empty())
        return;

    if (const auto index { indexOf(name) })
        selectOnly(index);
    else if (m_scanId)
        m_selectWhenLoaded = name; // Still reading the folder
}

void FolderWindow::go(const fs::path &requested) noexcept
{
    const fs::path dir { Normalized(requested) };

    if (dir == m_dir)
        return;

    std::error_code ec;

    if (!fs::is_directory(dir, ec))
    {
        // E.g. a favorite that was deleted meanwhile
        sidebar.setCurrentFolder(m_dir);
        return;
    }

    m_backStack.push_back(m_dir);
    m_forwardStack.clear();
    navigate(dir);
    updateNavigation();
}

void FolderWindow::goBack() noexcept
{
    if (m_backStack.empty())
        return;

    const fs::path dir { m_backStack.back() };
    m_backStack.pop_back();
    m_forwardStack.push_back(m_dir);

    // The folder we come from selected (like Finder)
    navigate(dir, m_dir.parent_path() == dir ? m_dir.filename().string() : std::string {});
    updateNavigation();
}

void FolderWindow::goForward() noexcept
{
    if (m_forwardStack.empty())
        return;

    const fs::path dir { m_forwardStack.back() };
    m_forwardStack.pop_back();
    m_backStack.push_back(m_dir);
    navigate(dir);
    updateNavigation();
}

void FolderWindow::goUp() noexcept
{
    if (!m_dir.has_parent_path() || m_dir.parent_path() == m_dir)
        return;

    const std::string from { m_dir.filename().string() };
    m_backStack.push_back(m_dir);
    m_forwardStack.clear();
    navigate(m_dir.parent_path(), from);
    updateNavigation();
}

void FolderWindow::navigate(const fs::path &requested, const std::string &select) noexcept
{
    const fs::path dir { Normalized(requested) };
    m_dir = dir;
    m_lastClickMs = 0;
    m_lastSelectedKey.clear();

    const std::string name { GetDesk()->displayName(dir) };
    setTitle(name);
    content.title.setText(name);
    sidebar.setCurrentFolder(dir);

    // A new folder: new files, from the top
    m_entries.clear();
    m_indexByKey.clear();
    m_selected.clear();
    content.grid.setItemCount(0);
    m_selectWhenLoaded = select;
    watch();
    content.grid.setOffset(0.f, 0.f);
    rescan();
}

void FolderWindow::updateNavigation() noexcept
{
    content.back.setEnabled(!m_backStack.empty());
    content.forward.setEnabled(!m_forwardStack.empty());
}

void FolderWindow::watch() noexcept
{
    if (m_inotify < 0)
        return;

    if (m_watch >= 0)
        inotify_rm_watch(m_inotify, m_watch);

    m_watch = inotify_add_watch(m_inotify, m_dir.c_str(),
        IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);

    if (m_watch < 0)
        Log(CZWarning, CZLN, "Can't watch {}: {}", m_dir.string(), strerror(errno));
}

bool FolderWindow::leaveIfGone() noexcept
{
    std::error_code ec;

    if (fs::is_directory(m_dir, ec))
        return false;

    fs::path dir { m_dir };

    while (dir.has_parent_path() && dir.parent_path() != dir && !fs::is_directory(dir, ec))
        dir = dir.parent_path();

    if (!fs::is_directory(dir, ec))
        dir = GetDesk()->xdgkit->homeDir();

    Log(CZInfo, CZLN, "{} is gone: showing {}", m_dir.string(), dir.string());

    // Its history may point to it too: forgotten
    m_backStack.clear();
    m_forwardStack.clear();
    navigate(dir);
    updateNavigation();
    return true;
}

void FolderWindow::rescan() noexcept
{
    m_rescanTimer.stop();

    if (leaveIfGone())
        return;

    // Read in the background (big or slow folders never block the window)
    auto &scanner { *GetDesk()->scanner };

    if (m_scanId)
        scanner.cancel(m_scanId);

    m_scanStartMs = CZTime::Ms();
    const fs::path dir { m_dir };

    if (m_entries.empty())
        content.status.setText("Loading…");

    m_scanId = scanner.scan(m_dir, m_showsHidden, [this, dir](std::vector<DeskEntry> &&entries){
        m_scanId = 0;

        if (dir == m_dir)
            showEntries(std::move(entries));
    });
}

void FolderWindow::showEntries(std::vector<DeskEntry> &&entries) noexcept
{
    Log(CZDebug, CZLN, "{}: {} files read in {} ms", m_dir.string(), entries.size(), CZTime::Ms() - m_scanStartMs);

    m_entries = std::move(entries);
    m_indexByKey.clear();

    for (size_t i = 0; i < m_entries.size(); i++)
        m_indexByKey[m_entries[i].key] = i;

    // The files still there stay selected
    std::erase_if(m_selected, [this](const std::string &key){ return !m_indexByKey.contains(key); });

    // Only the icons in view are (re)bound
    content.grid.setItemCount(m_entries.size());

    if (!m_selectWhenLoaded.empty())
    {
        const std::string name { std::move(m_selectWhenLoaded) };
        m_selectWhenLoaded.clear();
        selectOnly(indexOf(name));
    }

    updateStatus();
}

void FolderWindow::updateStatus() noexcept
{
    const size_t count { m_entries.size() };
    const size_t selected { m_selected.size() };
    std::string text;

    if (selected > 0)
        text = std::to_string(selected) + " of " + std::to_string(count) + " selected";
    else
        text = std::to_string(count) + (count == 1 ? " item" : " items");

    if (struct statvfs st; statvfs(m_dir.c_str(), &st) == 0)
        text += ", " + FormatBytes(UInt64(st.f_bavail) * st.f_frsize) + " available";

    content.status.setText(text);
}

/* ---------------------------------------- Selection ---------------------------------------- */

std::vector<const DeskEntry*> FolderWindow::selectedEntries() const noexcept
{
    std::vector<const DeskEntry*> entries;

    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
            entries.emplace_back(&entry);

    return entries;
}

void FolderWindow::setSelected(size_t index, bool selected) noexcept
{
    const std::string &key { m_entries[index].key };

    if (selected)
        m_selected.insert(key);
    else
        m_selected.erase(key);

    // Its icon, if in view
    if (AKNode *cell { content.grid.cell(index) })
        static_cast<FolderItem*>(cell)->setSelected(selected);
}

void FolderWindow::selectOnly(std::optional<size_t> index) noexcept
{
    for (const auto &[i, cell] : content.grid.visibleCells())
        static_cast<FolderItem*>(cell)->setSelected(index && i == *index);

    m_selected.clear();

    if (index && *index < m_entries.size())
    {
        m_selected.insert(m_entries[*index].key);
        m_lastSelectedKey = m_entries[*index].key;
        content.grid.scrollToItem(*index);
    }

    updateStatus();
}

void FolderWindow::clearSelection() noexcept
{
    selectOnly(std::nullopt);
}

void FolderWindow::selectAll() noexcept
{
    for (const auto &entry : m_entries)
        m_selected.insert(entry.key);

    for (const auto &[i, cell] : content.grid.visibleCells())
        static_cast<FolderItem*>(cell)->setSelected(true);

    updateStatus();
}

std::optional<size_t> FolderWindow::indexAt(const SkPoint &pos) const noexcept
{
    // Over its icon or the text of its name (not the rest of its cell)
    const auto index { content.grid.indexAt(pos) };

    if (!index)
        return std::nullopt;

    const AKNode *cell { content.grid.cell(*index) };

    if (!cell || !static_cast<const FolderItem*>(cell)->contains(pos))
        return std::nullopt;

    return index;
}

std::optional<size_t> FolderWindow::indexOf(const std::string &key) const noexcept
{
    const auto it { m_indexByKey.find(key) };
    return it == m_indexByKey.end() ? std::nullopt : std::optional<size_t>(it->second);
}

void FolderWindow::moveSelection(Int32 dx, Int32 dy) noexcept
{
    if (m_entries.empty())
        return;

    const Int64 count { Int64(m_entries.size()) };
    const auto last { indexOf(m_lastSelectedKey) };
    Int64 index;

    // Nothing selected yet: the first one (or the last one going backwards)
    if (!last || !isSelected(*last))
        index = (dx < 0 || dy < 0) ? count - 1 : 0;
    else
        index = std::clamp(Int64(*last) + dx + dy * Int64(content.grid.columns()), Int64(0), count - 1);

    selectOnly(size_t(index));
}

/* ----------------------------------------- Actions ----------------------------------------- */

void FolderWindow::openSelection(bool newWindow) noexcept
{
    const auto entries { selectedEntries() };

    if (entries.empty())
        return;

    // A single folder: shown here (like Finder)
    if (entries.size() == 1 && !newWindow)
    {
        const DeskEntry *e { entries.front() };

        if (e->kind == DeskEntry::Kind::Volume || (!e->launcher && e->mimeType == "inode/directory"))
        {
            go(e->path);
            return;
        }
    }

    // The entries are owned by the items: copied (opening may rescan)
    std::vector<DeskEntry> copies;

    for (const DeskEntry *e : entries)
        copies.emplace_back(*e);

    std::vector<const DeskEntry*> pointers;

    for (const auto &e : copies)
        pointers.emplace_back(&e);

    GetDesk()->open(pointers);
}

void FolderWindow::trashSelection() noexcept
{
    const auto entries { selectedEntries() };

    if (entries.empty())
        return;

    GetDesk()->trash(entries);
    m_rescanTimer.start(DESK_RESCAN_DELAY_MS);
}

void FolderWindow::newFolder() noexcept
{
    const std::string name { GetDesk()->newFolder(m_dir) };

    if (name.empty())
        return;

    // Selected once it's read
    m_selectWhenLoaded = name;
    rescan();
}

void FolderWindow::setShowsHidden(bool shows) noexcept
{
    if (m_showsHidden == shows)
        return;

    m_showsHidden = shows;
    rescan();
}

/* ------------------------------------------ Input ------------------------------------------ */

void FolderWindow::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    MToplevel::pointerButtonEvent(e);

    if (!e.pressed)
        return;

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    // Over the icons area: the window's keys from now on
    if (!SkRect::Make(content.grid.worldRect()).contains(pos.x(), pos.y()))
        return;

    setKeyboardFocus(true);
    const auto target { indexAt(pos) };

    if (e.button == BTN_RIGHT)
    {
        if (target)
        {
            if (!isSelected(*target))
                selectOnly(target);

            openItemsMenu(e);
        }
        else
        {
            clearSelection();
            openFolderMenu(e);
        }
        return;
    }

    if (e.button != BTN_LEFT)
        return;

    if (!target)
    {
        m_lastClickMs = 0;

        if (!CtrlHeld() && !ShiftHeld())
            clearSelection();

        return;
    }

    const UInt64 now { CZTime::Ms() };

    const std::string &key { m_entries[*target].key };

    // Double click: open (Ctrl: folders in a new window)
    if (m_lastClickKey == key && now - m_lastClickMs <= DESK_DOUBLE_CLICK_MS)
    {
        m_lastClickMs = 0;
        selectOnly(target);
        openSelection(CtrlHeld());
        return;
    }

    m_lastClickMs = now;
    m_lastClickKey = key;

    if (CtrlHeld() || ShiftHeld())
    {
        const bool selected { !isSelected(*target) };
        setSelected(*target, selected);

        if (selected)
            m_lastSelectedKey = key;

        updateStatus();
    }
    else
        selectOnly(target);
}

void FolderWindow::keyboardKeyEvent(const CZKeyboardKeyEvent &e)
{
    MToplevel::keyboardKeyEvent(e);

    if (!e.isPressed)
        return;

    auto *desk { GetDesk() };
    const bool ctrl { CtrlHeld() }, shift { ShiftHeld() }, alt { AltHeld() };

    // Go menu shortcuts (like Finder's)
    if (ctrl && shift)
    {
        switch (e.code)
        {
        case KEY_H: go(desk->xdgkit->homeDir()); return;
        case KEY_D: go(desk->desktopDir()); return;
        case KEY_O: go(desk->userDir("DOCUMENTS", "Documents")); return;
        case KEY_L: go(desk->userDir("DOWNLOAD", "Downloads")); return;
        case KEY_C: go("/"); return;
        case KEY_N: newFolder(); return;
        default: return;
        }
    }

    switch (e.code)
    {
    case KEY_ENTER:
    case KEY_KPENTER:
        if (!e.isRepeat)
            openSelection(ctrl);
        break;
    case KEY_DOWN:
        if (ctrl)
            openSelection();
        else
            moveSelection(0, 1);
        break;
    case KEY_UP:
        if (ctrl || alt)
            goUp();
        else
            moveSelection(0, -1);
        break;
    case KEY_LEFT:
        if (alt)
            goBack();
        else
            moveSelection(-1, 0);
        break;
    case KEY_RIGHT:
        if (alt)
            goForward();
        else
            moveSelection(1, 0);
        break;
    case KEY_LEFTBRACE:
        if (ctrl)
            goBack();
        break;
    case KEY_RIGHTBRACE:
        if (ctrl)
            goForward();
        break;
    case KEY_BACKSPACE:
        if (ctrl)
            trashSelection();
        else
            goUp();
        break;
    case KEY_DELETE:
        trashSelection();
        break;
    case KEY_ESC:
        clearSelection();
        break;
    case KEY_A:
        if (ctrl)
            selectAll();
        break;
    case KEY_O:
        if (ctrl)
            openSelection();
        break;
    case KEY_H:
        if (ctrl)
            setShowsHidden(!m_showsHidden);
        break;
    case KEY_N:
        if (ctrl)
            desk->openFolder(m_dir, true);
        break;
    case KEY_W:
        if (ctrl)
            setMapped(false); // Destroyed by Desk
        break;
    default:
        break;
    }
}

/* ------------------------------------------ Menus ------------------------------------------ */

void FolderWindow::openItemsMenu(const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };
    const auto entries { selectedEntries() };

    if (entries.empty())
        return;

    // Copied: the items may be gone when an action runs
    std::vector<DeskEntry> copies;

    for (const DeskEntry *e : entries)
        copies.emplace_back(*e);

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [this, copies](ContextMenu &menu){
        auto *desk { GetDesk() };
        const bool single { copies.size() == 1 };
        const DeskEntry &first { copies.front() };
        const bool folder { first.kind == DeskEntry::Kind::Volume || (!first.launcher && first.mimeType == "inode/directory") };

        menu.addAction("Open", [this]{ openSelection(); });

        if (single && folder)
            menu.addAction("Open in New Window", [this]{ openSelection(true); });

        // Other apps that can open it (the default one is "Open").
        if (single && !folder && !first.launcher)
        {
            auto apps { desk->appsFor(first) };
            size_t shown { 0 };

            for (size_t i = 1; i < apps.size() && shown < 4; i++)
            {
                if (apps[i]->noDisplay)
                    continue;

                menu.addAction("Open With " + apps[i]->name, [entry = first, app = apps[i]]{
                    GetDesk()->openWith(entry, *app);
                });
                shown++;
            }
        }

        if (single && !folder && (Assets::HasThumbnail(first.mimeType) || first.mimeType == "image/svg+xml"))
        {
            menu.addDivider();
            menu.addAction("Set Desktop Picture", [path = first.path]{ GetDesk()->setPicture(path); });
        }

        menu.addDivider();
        menu.addAction("Move to Trash", [this]{ trashSelection(); });
    });
}

void FolderWindow::openFolderMenu(const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [this](ContextMenu &menu){
        menu.addAction("New Folder", [this]{ newFolder(); });
        menu.addDivider();
        menu.addAction("Show Hidden Files", [this]{ setShowsHidden(!m_showsHidden); }).setChecked(m_showsHidden);
        menu.addAction("Open in New Window", [dir = m_dir]{ GetDesk()->openFolder(dir, true); });
    });
}
