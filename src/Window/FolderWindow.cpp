#include <CZ/Core/CZMouseSettings.h>
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
#include <CZ/Core/Events/CZLayoutEvent.h>
#include <CZ/Core/Events/CZWindowStateEvent.h>
#include <CZ/Core/Events/CZPointerMoveEvent.h>
#include <CZ/Core/CZEase.h>
#include <CZ/Marco/MDNDOffer.h>
#include <CZ/Ream/RImage.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <linux/input-event-codes.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <strings.h>

namespace fs = std::filesystem;

static bool KeyHeld(UInt32 left, UInt32 right) noexcept
{
    auto keymap { CZCore::Get()->keymap() };
    return keymap && (keymap->pressedKeys().contains(left) || keymap->pressedKeys().contains(right));
}

static bool CtrlHeld() noexcept { return KeyHeld(KEY_LEFTCTRL, KEY_RIGHTCTRL); }
static bool ShiftHeld() noexcept { return KeyHeld(KEY_LEFTSHIFT, KEY_RIGHTSHIFT); }
static bool AltHeld() noexcept { return KeyHeld(KEY_LEFTALT, KEY_RIGHTALT); }

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
    m_staleTimer.setCallback([this](CZTimer*){ clearStale(); });
    m_bandScrollTimer.setCallback([this](CZTimer*){ bandAutoScroll(); });
    content.selectionBox.setWindowColors();

    // Files dragged over the window (from it, other windows, the desktop or other apps)
    onDNDEnter.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dragOver(offer, pos); });
    onDNDMotion.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dragOver(offer, pos); });
    onDNDDrop.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dropped(offer, pos); });
    onDNDLeave.subscribe(this, [this](MDNDOffer &){ clearDropFeedback(); });

    // A slow second click on a name (see pointerButtonEvent())
    m_renameTimer.setCallback([this](CZTimer*){
        const std::string key { std::move(m_renameCandidate) };
        m_renameCandidate.clear();

        if (m_selected.size() == 1 && m_selected.contains(key))
            renameSelection();
    });

    // Renamed: selected again once the folder is read
    for (RenameField *field : { &content.gridRenameField, &content.listRenameField })
    {
        field->onRenamed = [this](const std::string &key){
            m_selectWhenLoaded = key;
            rescan();
        };

        // Enter: the key press reaches the window too (after the field closed), see keyboardKeyEvent()
        field->editor.onSubmit.subscribe(this, [this]{ m_renameSubmitMs = CZTime::Ms(); });
    }

    // Scrolled: the name being edited and the rubber band follow
    for (AKGridView *v : { static_cast<AKGridView*>(&content.grid), static_cast<AKGridView*>(&content.list) })
        v->onOffsetChanged.subscribe(this, [this]{
            placeRename();

            if (m_bandActive)
                updateBand();
        });

    // The column titles follow the list horizontally
    content.list.onOffsetChanged.subscribe(this, [this]{
        content.listHeader.layout().setPosition(YGEdgeLeft, content.list.offsetX());
    });

    // The icons: cells bound to the files in view
    content.grid.makeCell = []{ return std::make_unique<FolderItem>(); };
    content.grid.bindCell = [this](AKNode &cell, size_t index){
        auto &item { static_cast<FolderItem&>(cell) };
        item.setEntry(m_entries[index]);
        item.setSelected(isSelected(index));

        // Dragged (see startDrag()) or a drop target
        const std::string &key { m_entries[index].key };
        item.setDimmed(m_dragKeys.contains(key));
        item.setDropTarget(key == m_dropTargetKey);

        // Its name being edited: the field is over it (see renameSelection())
        const auto &field { content.gridRenameField };
        item.label.setOpacity(field.active() && field.key() == key ? 0.f : 1.f);
    };

    // The rows: cells bound to the files in view
    content.list.makeCell = []{ return std::make_unique<FolderListRow>(); };
    content.list.bindCell = [this](AKNode &cell, size_t index){
        auto &row { static_cast<FolderListRow&>(cell) };
        row.setColumns(m_columns);
        row.setEntry(m_entries[index]);
        row.setSelected(isSelected(index));
        row.setDimmed(m_dragKeys.contains(m_entries[index].key));
        row.setDropTarget(m_entries[index].key == m_dropTargetKey);
    };

    content.viewSwitch.onSelected.subscribe(this, [this](size_t index){ setListView(index == 1); });

    auto *desk { GetDesk() };

    // The list's columns: resized from the header of any window, shown by all (the name's as a
    // share of each window's width, see layoutNameColumn())
    m_columns = desk->settings.listColumns;
    applyColumns();
    content.listHeader.onColumnsResized.subscribe(this, [this](const DeskListColumns &columns, bool done){
        auto *desk { GetDesk() };
        m_columns = columns;

        // The name keeps this width, then follows the window's width (see layoutNameColumn())
        if (const SkScalar viewport { listViewportWidth() }; viewport > 0.f)
            desk->settings.listNameOffset = columns.name - Int32(std::lround(viewport));

        desk->settings.listColumns = columns;
        applyColumns();
        desk->onListColumnsChanged.notify();

        if (done)
            desk->saveSettings();
    });
    desk->onListColumnsChanged.subscribe(this, [this]{
        const auto &shared { GetDesk()->settings.listColumns };
        m_columns.date = shared.date;
        m_columns.size = shared.size;
        m_columns.kind = shared.kind;
        layoutNameColumn();
        applyColumns();
    });

    // The view chosen last
    setListView(desk->settings.listView);
    content.listHeader.setSort(m_sortColumn, m_sortAscending);

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
    m_menus = std::make_unique<WindowMenuBar>(*this);
}

FolderWindow::~FolderWindow() noexcept
{
    // Closed while active: the desktop's menus come back (unless another window is active)
    m_menus.reset();
    GetDesk()->windowDeactivated();

    if (m_dnd && m_dnd->dragging())
        m_dnd->cancel();

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
    toolbar.layout().setGap(YGGutterColumn, 2.f);

    const SkRegion empty;
    title.setFontSize(16.f);
    title.setFontStyle(SkFontStyle(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    title.setTextColor(AKTheme::Label);
    title.layout().setMargin(YGEdgeLeft, 8.f);
    title.layout().setFlexShrink(1.f);
    title.setTruncates(true); // "Very long folder na…" when the toolbar is narrow
    title.setInputRegion(&empty);

    // Grid or List, at the right (like Finder's)
    viewSwitch.layout().setMarginAuto(YGEdgeLeft);

    // Cells of the same size, as many per row as fit
    grid.layout().setFlex(1.f);
    grid.setCellSize(FolderItem::CellSize());
    grid.setSpacing(0.f, DESK_WINDOW_GRID_GAP);
    grid.setContentPadding(DESK_WINDOW_GRID_PADDING);
    grid.setRowBackground(AKTheme::WindowBackground); // Like the window's: opaque rows, faster to draw

    // Rows with alternating stripes (opaque, like the grid's), inset from the edges
    list.layout().setFlex(1.f);
    list.setRowHeight(DESK_LIST_ROW_HEIGHT);
    list.setContentPadding(DESK_WINDOW_LIST_INSET);
    list.setRowBackground(AKTheme::WindowBackground);
    list.setRowColors(std::pair { AKTheme::WindowBackground, CZAdaptiveColor(0xFFF4F5F5, 0xFF262626) });

    separator.enableDiminishOpacityOnInactive(true);

    // The toolbar's area, over everything else (added last): its shadow falls on the icons
    toolbarShadowArea.setInputRegion(&empty);
    toolbarShadowArea.layout().setPositionType(YGPositionTypeAbsolute);
    toolbarShadowArea.layout().setPosition(YGEdgeTop, 0.f);
    toolbarShadowArea.layout().setPosition(YGEdgeLeft, 0.f);
    toolbarShadowArea.layout().setWidthPercent(100.f);
    toolbarShadowArea.layout().setHeight(MSidebar::HeaderHeight);
    toolbarShadow.setStrength(0.6f); // Subtle, like Finder's
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
    content.gridRenameField.cancel();
    content.listRenameField.cancel();
    endBand();
    m_renameTimer.stop();
    m_renameCandidate.clear();
    m_dir = dir;
    m_lastClickMs = 0;
    m_lastSelectedKey.clear();
    m_anchorKey.clear();

    const std::string name { GetDesk()->displayName(dir) };
    setTitle(name);
    content.title.setText(name);
    sidebar.setCurrentFolder(dir);

    // A new folder: the previous one's files stay in view until it's read (see m_stale), unselected
    for (const auto &[i, cell] : view().visibleCells())
        setCellSelected(*cell, false);

    m_selected.clear();
    m_selectWhenLoaded = select;

    if (!m_entries.empty())
    {
        m_stale = true;
        m_staleTimer.start(DESK_STALE_VIEW_MS);
    }

    watch();
    rescan();
}

void FolderWindow::clearStale() noexcept
{
    m_staleTimer.stop();

    if (!m_stale)
        return;

    m_stale = false;
    m_entries.clear();
    m_indexByKey.clear();
    view().setItemCount(0);
    view().setOffset(0.f, 0.f);
}

void FolderWindow::updateNavigation() noexcept
{
    content.back.setEnabled(!m_backStack.empty());
    content.forward.setEnabled(!m_forwardStack.empty());
    menusChanged();
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

    m_scanId = scanner.scan(m_dir, m_showsHidden, [this, dir](std::vector<DeskEntry> &&entries){
        m_scanId = 0;

        if (dir == m_dir)
            showEntries(std::move(entries));
    });
}

void FolderWindow::showEntries(std::vector<DeskEntry> &&entries) noexcept
{
    menusChanged();

    Log(CZDebug, CZLN, "{}: {} files read in {} ms", m_dir.string(), entries.size(), CZTime::Ms() - m_scanStartMs);

    // Another folder: replaces the previous one's files at once, from the top
    if (m_stale)
    {
        m_stale = false;
        m_staleTimer.stop();
        view().setOffset(0.f, 0.f);
    }

    m_entries = std::move(entries);
    sortEntries();

    // The files still there stay selected
    std::erase_if(m_selected, [this](const std::string &key){ return !m_indexByKey.contains(key); });

    // Only the cells in view are (re)bound
    view().setItemCount(m_entries.size());

    if (!m_selectWhenLoaded.empty())
    {
        const std::string name { std::move(m_selectWhenLoaded) };
        m_selectWhenLoaded.clear();
        selectOnly(indexOf(name));
    }

    // The file being renamed may have moved (or be gone)
    placeRename();
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
    menusChanged();

    const std::string &key { m_entries[index].key };

    if (selected)
        m_selected.insert(key);
    else
        m_selected.erase(key);

    // Its icon, if in view
    if (AKNode *cell { view().cell(index) })
        setCellSelected(*cell, selected);
}

void FolderWindow::selectOnly(std::optional<size_t> index) noexcept
{
    menusChanged();

    for (const auto &[i, cell] : view().visibleCells())
        setCellSelected(*cell, index && i == *index);

    m_selected.clear();

    if (index && *index < m_entries.size())
    {
        m_selected.insert(m_entries[*index].key);
        m_lastSelectedKey = m_entries[*index].key;
        m_anchorKey = m_entries[*index].key;
        view().scrollToItem(*index);
    }
}

void FolderWindow::clearSelection() noexcept
{
    selectOnly(std::nullopt);
}

void FolderWindow::selectRange(size_t index) noexcept
{
    menusChanged();

    if (index >= m_entries.size())
        return;

    // The anchor stays: another Shift+click changes the range from it
    const size_t anchor { indexOf(m_anchorKey).value_or(index) };
    const size_t first { std::min(anchor, index) }, last { std::max(anchor, index) };

    m_selected.clear();

    for (size_t i = first; i <= last; i++)
        m_selected.insert(m_entries[i].key);

    for (const auto &[i, cell] : view().visibleCells())
        setCellSelected(*cell, isSelected(i));

    m_lastSelectedKey = m_entries[index].key;

    if (m_anchorKey.empty())
        m_anchorKey = m_lastSelectedKey;
}

void FolderWindow::selectAll() noexcept
{
    menusChanged();

    if (m_stale)
        return;

    for (const auto &entry : m_entries)
        m_selected.insert(entry.key);

    for (const auto &[i, cell] : view().visibleCells())
        setCellSelected(*cell, true);
}

std::optional<size_t> FolderWindow::indexAt(const SkPoint &pos) const noexcept
{
    // The previous folder's files (see m_stale)
    if (m_stale)
        return std::nullopt;

    const auto index { view().indexAt(pos) };

    // Lists: the whole row
    if (!index || m_listView)
        return index;

    // Icons: over the icon or the text of its name (not the rest of its cell)
    const AKNode *cell { view().cell(*index) };

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
    if (m_entries.empty() || m_stale)
        return;

    const Int64 count { Int64(m_entries.size()) };
    const auto last { indexOf(m_lastSelectedKey) };
    Int64 index;

    // Nothing selected yet: the first one (or the last one going backwards)
    if (!last || !isSelected(*last))
        index = (dx < 0 || dy < 0) ? count - 1 : 0;
    else
        index = std::clamp(Int64(*last) + dx + dy * Int64(view().columns()), Int64(0), count - 1);

    selectOnly(size_t(index));
}

void FolderWindow::setCellSelected(AKNode &cell, bool selected) noexcept
{
    if (m_listView)
        static_cast<FolderListRow&>(cell).setSelected(selected);
    else
        static_cast<FolderItem&>(cell).setSelected(selected);
}

/* ---------------------------------------- Views ---------------------------------------- */

void FolderWindow::setListView(bool list) noexcept
{
    menusChanged();

    const bool changed { m_listView != list };
    const size_t count { m_entries.size() };

    if (changed)
    {
        endRename();
        endBand();
        content.listHeader.endResize();
    }

    // The other view lets its cells go
    view().setItemCount(0);
    m_listView = list;
    content.viewSwitch.setListView(list);

    content.grid.setVisible(!list);
    content.list.setVisible(list);
    content.listHeaderClip.setVisible(list);
    view().setItemCount(count);

    if (list)
        layoutNameColumn();

    // The shadows follow the view shown (below the list's titles in the list)
    content.toolbarShadowArea.layout().setHeight(MSidebar::HeaderHeight + (list ? DESK_LIST_HEADER_HEIGHT : 0));
    content.toolbarShadow.showWhileScrolled(&view());

    // The selection in view
    if (const auto selected { indexOf(m_lastSelectedKey) }; selected && isSelected(*selected))
        view().scrollToItem(*selected);

    if (changed)
    {
        auto &settings { GetDesk()->settings };
        settings.listView = list;
        GetDesk()->saveSettings();
    }
}

void FolderWindow::sortBy(SortColumn column, bool ascending) noexcept
{
    menusChanged();

    m_sortColumn = column;
    m_sortAscending = ascending;
    content.listHeader.setSort(column, ascending);
    sortEntries();
    view().reloadItems();
    placeRename();

    if (const auto selected { indexOf(m_lastSelectedKey) }; selected && isSelected(*selected))
        view().scrollToItem(*selected);
}

void FolderWindow::sortEntries() noexcept
{
    const auto byName = [](const DeskEntry &a, const DeskEntry &b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };

    // Ties by name (always ascending, like Finder)
    std::stable_sort(m_entries.begin(), m_entries.end(), [&](const DeskEntry &a, const DeskEntry &b) {
        int order { 0 };

        switch (m_sortColumn)
        {
        case SortColumn::Name: break;
        case SortColumn::Date: order = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0; break;
        case SortColumn::Size: order = a.size < b.size ? -1 : a.size > b.size ? 1 : 0; break;
        case SortColumn::Kind: order = strcasecmp(a.kindName.c_str(), b.kindName.c_str()); break;
        }

        if (m_sortColumn == SortColumn::Name)
            order = strcasecmp(a.name.c_str(), b.name.c_str());

        if (order != 0)
            return m_sortAscending ? order < 0 : order > 0;

        return byName(a, b);
    });

    m_indexByKey.clear();

    for (size_t i = 0; i < m_entries.size(); i++)
        m_indexByKey[m_entries[i].key] = i;
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

void FolderWindow::copySelection(bool cut) noexcept
{
    std::vector<fs::path> files;

    for (const DeskEntry *e : selectedEntries())
        if (e->kind == DeskEntry::Kind::File)
            files.emplace_back(e->path);

    GetDesk()->copyFiles(files, cut);
    menusChanged(); // Paste enabled
}

void FolderWindow::paste() noexcept
{
    if (!m_stale)
        GetDesk()->pasteInto(m_dir);
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
    menusChanged();

    if (m_showsHidden == shows)
        return;

    m_showsHidden = shows;
    rescan();
}

/* ----------------------------------------- Renaming ----------------------------------------- */

void FolderWindow::renameSelection() noexcept
{
    m_renameTimer.stop();
    m_renameCandidate.clear();

    if (m_stale || m_selected.size() != 1)
        return;

    const auto index { indexOf(*m_selected.begin()) };

    if (!index)
        return;

    // Fully in view first (now or once laid out: the field follows, see placeRename())
    view().scrollToItem(*index);
    if (m_listView)
    {
        content.listRenameField.begin(m_entries[*index], nameBox(*index));
        return;
    }

    // The label hidden meanwhile (see the grid's bindCell)
    content.gridRenameField.begin(m_entries[*index], nameBox(*index), [this, key = m_entries[*index].key](bool){
        if (const auto i { indexOf(key) })
            content.grid.reloadItem(*i);
    });
}

SkIRect FolderWindow::nameBox(size_t index) const noexcept
{
    // Icons: the label's box, below the icon's (see FolderItem), as wide as the cell
    if (!m_listView)
    {
        const SkRect cell { content.grid.cellRect(index) };
        const SkIRect grid { content.grid.worldRect().makeOffset(-content.worldRect().topLeft()) };
        const SkScalar labelY { SkScalar(DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING + DESK_LABEL_GAP) };
        const Int32 lineHeight { Int32(std::ceil(DESK_WINDOW_LABEL_FONT_SIZE * 1.3f)) };
        return SkIRect::MakeXYWH(Int32(grid.x() + content.grid.offsetX() + cell.x()),
                                 Int32(grid.y() + content.grid.offsetY() + cell.y() + labelY) - 1,
                                 Int32(cell.width()), lineHeight + 2);
    }

    // The row (content coordinates): its cell in the list, scrolled
    const SkRect cell { content.list.cellRect(index) };
    const SkIRect list { content.list.worldRect().makeOffset(-content.worldRect().topLeft()) };
    const SkScalar x { list.x() + content.list.offsetX() + cell.x() };
    const SkScalar y { list.y() + content.list.offsetY() + cell.y() };

    // After the icon, as wide as the name column (see FolderListRow), the field's padding around the text
    const SkScalar nameX { x + DESK_LIST_PADDING + DESK_LIST_ICON_SIZE + DESK_LIST_GAP };
    const SkScalar nameWidth { SkScalar(m_columns.name - DESK_LIST_ICON_SIZE - DESK_LIST_GAP) };

    return SkIRect::MakeXYWH(Int32(nameX) - 4, Int32(y) + 1, std::max(40, Int32(nameWidth) + 8), Int32(cell.height()) - 2);
}

void FolderWindow::placeRename() noexcept
{
    auto &field { renameField() };

    if (!field.active())
        return;

    const auto index { indexOf(field.key()) };

    if (!index)
    {
        field.cancel();
        return;
    }

    // Scrolled out of view: ended, as if clicked elsewhere
    const SkIRect box { nameBox(*index) };
    const SkIRect shown { view().worldRect().makeOffset(-content.worldRect().topLeft()) };

    if (view().layout().calculatedHeight() > 0.f && (box.top() < shown.top() || box.bottom() > shown.bottom()))
    {
        endRename();
        return;
    }

    field.place(box);
}

void FolderWindow::endRename() noexcept
{
    // Both: called when the view changes too
    for (RenameField *field : { &content.gridRenameField, &content.listRenameField })
        if (field->active() && !field->commit())
            field->cancel();
}

/* ------------------------------------- Rubber band ------------------------------------- */

SkPoint FolderWindow::toViewContent(const SkPoint &pos) const noexcept
{
    const SkIRect v { view().worldRect() };
    return SkPoint::Make(pos.x() - v.x() - view().offsetX(), pos.y() - v.y() - view().offsetY());
}

void FolderWindow::updateBand() noexcept
{
    // In the view's content: scrolling keeps its start where it was
    const SkPoint end { toViewContent(AKApp::Get()->pointer().pos()) };
    const SkRect band { SkRect::MakeLTRB(
        std::min(end.x(), m_bandStart.x()), std::min(end.y(), m_bandStart.y()),
        std::max(end.x(), m_bandStart.x()), std::max(end.y(), m_bandStart.y())) };

    // The files it touches: their icon or name (in the list, their row)
    std::unordered_set<std::string> selected { m_bandBase };
    const SkScalar iconBox { SkScalar(DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING) };

    for (size_t i = 0; i < m_entries.size(); i++)
    {
        const SkRect cell { view().cellRect(i) };

        if (!SkRect::Intersects(cell, band))
            continue;

        bool hit { m_listView };

        if (!hit)
        {
            const SkRect icon { SkRect::MakeXYWH(cell.x() + (cell.width() - iconBox) / 2.f, cell.y(), iconBox, iconBox) };

            // The name's text if in view (else the whole label's line, close enough)
            SkRect name { SkRect::MakeXYWH(cell.x(), cell.y() + iconBox + DESK_LABEL_GAP, cell.width(), cell.height() - iconBox - DESK_LABEL_GAP) };

            if (const AKNode *node { view().cell(i) })
                name = static_cast<const FolderItem*>(node)->label.textBounds().makeOffset(cell.x(), name.y());

            hit = SkRect::Intersects(icon, band) || SkRect::Intersects(name, band);
        }

        if (hit)
            selected.insert(m_entries[i].key);
    }

    m_selected = std::move(selected);

    for (const auto &[i, cell] : view().visibleCells())
        setCellSelected(*cell, isSelected(i));

    // Shown over the files (content node coordinates), within the view
    const SkIRect v { view().worldRect().makeOffset(-content.worldRect().topLeft()) };
    SkIRect shown { band.makeOffset(v.x() + view().offsetX(), v.y() + view().offsetY()).round() };

    if (!shown.intersect(v))
        shown.setEmpty();

    content.selectionBox.setRect(shown);

    // Near an edge: scrolls (see bandAutoScroll())
    if (!m_bandScrollTimer.running() && bandScrollSpeed() != 0.f)
    {
        m_bandScrollMs = CZTime::Ms();
        m_bandScrollRest = 0.f;
        m_bandScrollTimer.start(16);
    }
}

SkScalar FolderWindow::bandScrollSpeed() const noexcept
{
    const SkScalar y { AKApp::Get()->pointer().pos().y() };
    const SkIRect v { view().worldRect() };
    SkScalar into { 0.f }; // How far into the edge zone (past the edge included), up negative

    if (y < v.top() + DESK_AUTOSCROLL_EDGE)
        into = y - (v.top() + DESK_AUTOSCROLL_EDGE);
    else if (y > v.bottom() - DESK_AUTOSCROLL_EDGE)
        into = y - (v.bottom() - DESK_AUTOSCROLL_EDGE);

    return std::clamp(into * DESK_AUTOSCROLL_SPEED, -DESK_AUTOSCROLL_MAX, DESK_AUTOSCROLL_MAX);
}

void FolderWindow::bandAutoScroll() noexcept
{
    const SkScalar speed { bandScrollSpeed() };

    if (!m_bandActive || speed == 0.f)
        return; // Stopped (started again by updateBand())

    // As far as the time since the last tick allows (whole points, the rest kept)
    const UInt64 now { CZTime::Ms() };
    const SkScalar delta { speed * SkScalar(now - m_bandScrollMs) / 1000.f + m_bandScrollRest };
    m_bandScrollMs = now;
    const Int32 points { Int32(delta) };
    m_bandScrollRest = delta - SkScalar(points);

    // Clamped to the content: at the top or bottom nothing changes (the band is updated by
    // onOffsetChanged when it does)
    if (points != 0)
        view().setOffsetY(Int32(view().offsetY()) - points);

    m_bandScrollTimer.start(16);
}

void FolderWindow::endBand() noexcept
{
    m_bandPossible = false;

    if (!m_bandActive)
        return;

    m_bandActive = false;
    m_bandBase.clear();
    m_bandScrollTimer.stop();
    content.selectionBox.setRect(SkIRect::MakeEmpty());

    if (scene().pointerGrab() == this)
        scene().setPointerGrab(nullptr);

    // The arrow keys continue from one of them
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
        {
            m_lastSelectedKey = entry.key;
            break;
        }
}

/* ---------------------------------------- Drag & drop ---------------------------------------- */

SkRect FolderWindow::iconRect(size_t index) const noexcept
{
    const SkRect cell { view().cellRect(index) };
    const SkIRect v { view().worldRect().makeOffset(-content.worldRect().topLeft()) };
    const SkScalar x { v.x() + view().offsetX() + cell.x() }, y { v.y() + view().offsetY() + cell.y() };

    // The list's at the row's start, centered vertically
    if (m_listView)
        return SkRect::MakeXYWH(x + DESK_LIST_PADDING, y + (cell.height() - DESK_LIST_ICON_SIZE) / 2.f, DESK_LIST_ICON_SIZE, DESK_LIST_ICON_SIZE);

    // The grid's centered at the top of the cell (see FolderItem)
    const SkScalar box { SkScalar(DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING) };
    return SkRect::MakeXYWH(x + (cell.width() - box) / 2.f + DESK_ICON_PADDING, y + DESK_ICON_PADDING, DESK_WINDOW_ICON_SIZE, DESK_WINDOW_ICON_SIZE);
}

std::shared_ptr<RImage> FolderWindow::iconImage(size_t index) const noexcept
{
    if (!m_listView)
        if (const AKNode *cell { view().cell(index) })
            if (auto image { static_cast<const FolderItem*>(cell)->icon.image() })
                return image;

    return GetDesk()->assets.icon(m_entries[index]);
}

void FolderWindow::startDrag(size_t grabbed) noexcept
{
    if (m_dnd && m_dnd->dragging())
        return;

    if (!m_dnd)
    {
        m_dnd = std::make_unique<MDND>();
        m_dnd->onFinished.subscribe(this, [this]{ endDrag(); });
        m_dnd->onCancelled.subscribe(this, [this]{ endDrag(); });

        // Other apps (file managers, editors...) get the files as URIs
        m_dnd->onSend = [](const std::string &mimeType, Int32 fd){
            if (mimeType != "text/uri-list")
                return;

            const std::string uris { Desk::URIList(GetDesk()->dragFiles()) };
            [[maybe_unused]] const auto written { write(fd, uris.data(), uris.size()) };
        };
    }

    // The selection, in view order (the grabbed one in front)
    std::vector<size_t> dragged;

    for (size_t i = 0; i < m_entries.size(); i++)
        if (isSelected(i))
            dragged.emplace_back(i);

    if (dragged.empty() || !isSelected(grabbed))
        return;

    m_dndItem.reset();
    m_dndStack.reset();
    m_gather.reset();
    std::vector<DragGather::Flight> flights; // Surface coordinates until the drag starts

    // Icons: where the grabbed icon was pressed. The list (its rows are wider than the drag icon):
    // the drag icon's top-left corner, a bit inside
    if (m_listView)
        m_grabInIcon = { DESK_LIST_DRAG_HOTSPOT, DESK_LIST_DRAG_HOTSPOT };
    else
    {
        const SkPoint press { m_pressPos.x() - content.worldRect().x(), m_pressPos.y() - content.worldRect().y() };
        const SkRect grabIcon { iconRect(grabbed) };
        m_grabInIcon = {
            std::clamp(Int32(press.x() - grabIcon.x()), 0, DESK_WINDOW_ICON_SIZE),
            std::clamp(Int32(press.y() - grabIcon.y()), 0, DESK_WINDOW_ICON_SIZE) };
    }

    if (dragged.size() == 1)
    {
        // Like Finder: the icon and its highlighted name, under the pointer where it was grabbed
        m_dndItem = std::make_unique<FolderItem>(m_entries[grabbed], m_dnd.get());
        m_dndItem->layout().setPosition(YGEdgeLeft, 0.f);
        m_dndItem->layout().setPosition(YGEdgeTop, 0.f);
        m_dndItem->label.setSelected(true);
        m_dndItem->icon.setOpacity(0.85f);
        m_dndItem->label.setOpacity(0.85f);

        const SkSize cell { FolderItem::CellSize() };
        const Int32 box { DESK_WINDOW_ICON_SIZE + 2 * DESK_ICON_PADDING };
        m_dnd->layout().setWidth(cell.width());
        m_dnd->layout().setHeight(cell.height());
        m_dnd->setHotspot(SkIPoint::Make((Int32(cell.width()) - box) / 2 + DESK_ICON_PADDING + m_grabInIcon.x(),
                                         DESK_ICON_PADDING + m_grabInIcon.y()));
    }
    else
    {
        // Several: the drag starts with the grabbed icon alone while the others in view fly to the
        // pointer (DragGather), then it becomes the whole stack with a count (gatherFinished())
        // The grabbed one in front and the others behind it, the farther the deeper. Those the
        // user sees fly (not those scrolled out of view, however many are dragged); if they're
        // fewer than the stack's layers (DESK_DRAG_STACK_MAX), the nearest hidden ones fill the
        // layers behind them (without flying). The count says how many.
        const SkIRect shown { view().worldRect().makeOffset(-content.worldRect().topLeft()) };
        const auto distance = [grabbed](size_t i){ return i > grabbed ? i - grabbed : grabbed - i; };
        std::vector<size_t> seen, hidden;

        for (size_t i : dragged)
            if (i != grabbed)
                (view().cell(i) && SkRect::Intersects(iconRect(i), SkRect::Make(shown)) ? seen : hidden).emplace_back(i);

        const auto nearest = [&](size_t a, size_t b){ return distance(a) < distance(b); };
        std::sort(seen.begin(), seen.end(), nearest);
        const size_t filling { std::min(hidden.size(), seen.size() < DESK_DRAG_STACK_MAX - 1 ? DESK_DRAG_STACK_MAX - 1 - seen.size() : size_t(0)) };
        std::partial_sort(hidden.begin(), hidden.begin() + filling, hidden.end(), nearest);
        hidden.resize(filling);

        // Back to front: the hidden ones deepest, then those seen (the farthest first)
        std::vector<size_t> stack(hidden.rbegin(), hidden.rend());
        stack.insert(stack.end(), seen.rbegin(), seen.rend());
        m_dragImages.clear();

        for (size_t idx = 0; idx < stack.size(); idx++)
        {
            const size_t i { stack[idx] };
            m_dragImages.emplace_back(iconImage(i));

            if (idx < hidden.size())
                continue; // Hidden: in the stack once it's complete

            const Int32 depth { DragStack::LayerDepth(idx, stack.size() + 1) };
            flights.push_back({ m_dragImages.back(), iconRect(i).makeOffset(content.worldRect().x(), content.worldRect().y()), depth });
        }

        m_dragImages.emplace_back(iconImage(grabbed)); // In front
        m_dndStack = std::make_unique<DragStack>(DESK_WINDOW_ICON_SIZE, m_dnd.get());
        setDragImage({ m_dragImages.back() }, 1);
    }

    if (!m_press || !m_dnd->start(*this, *m_press, { DESK_FILES_MIME_TYPE, "text/uri-list" },
                                  CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move)))
        return;

    // Dimmed meanwhile; their files offered to the targets (disks can't be moved into folders)
    std::vector<fs::path> files;

    for (size_t i : dragged)
    {
        m_dragKeys.insert(m_entries[i].key);

        if (m_entries[i].kind == DeskEntry::Kind::File)
            files.emplace_back(m_entries[i].path);
    }

    GetDesk()->setDragFiles(std::move(files));
    view().reloadItems();

    // The flights relative to the drag icon (under the pointer, at its hotspot)
    if (!flights.empty() && m_dndStack)
    {
        const SkPoint pointer { AKApp::Get()->pointer().pos() };
        const SkPoint icon { pointer.x() - m_dnd->hotspot().x(), pointer.y() - m_dnd->hotspot().y() };

        for (auto &flight : flights)
            flight.from.offset(-icon.x(), -icon.y());

        m_gather = std::make_unique<DragGather>(*m_dnd, SkPoint::Make(m_dndStack->frontOrigin().x(), m_dndStack->frontOrigin().y()),
            DESK_WINDOW_ICON_SIZE, Assets::WindowShadow, std::move(flights), [this]{ gatherFinished(); });
    }
    else if (m_dndStack)
        gatherFinished(); // None in view to fly: the whole stack right away
}

void FolderWindow::setDragImage(const std::vector<std::shared_ptr<RImage>> &images, UInt32 count) noexcept
{
    if (!m_dnd || !m_dndStack)
        return;

    m_dndStack->setItems(images, count);
    const SkISize size { m_dndStack->nodeSize() };
    const SkIPoint front { m_dndStack->frontOrigin() };
    m_dnd->layout().setWidth(size.width());
    m_dnd->layout().setHeight(size.height());
    m_dnd->setHotspot(SkIPoint::Make(front.x() + m_grabInIcon.x(), front.y() + m_grabInIcon.y()));
}

void FolderWindow::gatherFinished() noexcept
{
    // The flyers just landed (hidden): the stack takes their place

    if (m_dnd && m_dnd->dragging() && m_dndStack)
        setDragImage(m_dragImages, UInt32(m_dragKeys.size()));
}

void FolderWindow::endDrag() noexcept
{
    setDropTarget({});
    m_gather.reset();

    if (m_dragKeys.empty())
        return;

    m_dragKeys.clear();
    GetDesk()->setDragFiles({});
    view().reloadItems();
}

bool FolderWindow::dragsFromHere(MDNDOffer &offer) const noexcept
{
    const auto &files { GetDesk()->dragFiles() };

    if (files.empty() || !(offer.hasMimeType(DESK_FILES_MIME_TYPE) || offer.hasMimeType(DESK_ITEMS_MIME_TYPE)))
        return false;

    return std::all_of(files.begin(), files.end(), [this](const fs::path &file){ return Normalized(file.parent_path()) == m_dir; });
}

std::optional<size_t> FolderWindow::dropTargetAt(MDNDOffer &offer, const SkPoint &pos) const noexcept
{
    const auto index { indexAt(pos) };

    if (!index)
        return std::nullopt;

    const DeskEntry &entry { m_entries[*index] };

    if (entry.kind != DeskEntry::Kind::Volume && (entry.launcher || entry.mimeType != "inode/directory"))
        return std::nullopt;

    // Not into one of the folders dragged
    if (offer.hasMimeType(DESK_FILES_MIME_TYPE) || offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
    {
        const auto &files { GetDesk()->dragFiles() };

        if (std::find(files.begin(), files.end(), entry.path) != files.end())
            return std::nullopt;
    }

    return index;
}

void FolderWindow::setDropTarget(const std::string &key) noexcept
{
    if (key == m_dropTargetKey)
        return;

    const std::string prev { std::exchange(m_dropTargetKey, key) };

    for (const std::string *k : { &prev, &key })
        if (const auto index { indexOf(*k) })
            view().reloadItem(*index);
}

void FolderWindow::clearDropFeedback() noexcept
{
    setDropTarget({});
    sidebar.showInsertionLine(std::nullopt);
    sidebar.setDropTarget(std::nullopt);
}

bool FolderWindow::reorderingFavorite(MDNDOffer &offer) const noexcept
{
    return offer.hasMimeType(DESK_FAVORITE_MIME_TYPE) && !FolderSidebar::DraggedFavorite().empty();
}

void FolderWindow::dragOver(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    auto *desk { GetDesk() };

    // A favorite being reordered: over the favorites, where the line shows
    if (reorderingFavorite(offer))
    {
        clearDropFeedback();

        if (!SkRect::Make(sidebar.list.worldRect()).contains(pos.x(), pos.y()))
        {
            offer.accept("");
            return;
        }

        sidebar.showInsertionLine(sidebar.favoritesIndexAt(pos));
        offer.accept(DESK_FAVORITE_MIME_TYPE);
        offer.setActions(MDNDOffer::Move, MDNDOffer::Move);
        return;
    }

    if (!desk->offersFiles(offer))
    {
        clearDropFeedback();
        offer.accept("");
        return;
    }

    const bool own { !desk->dragFiles().empty() && (offer.hasMimeType(DESK_ITEMS_MIME_TYPE) || offer.hasMimeType(DESK_FILES_MIME_TYPE)) };
    const char *mime { !own ? "text/uri-list" : offer.hasMimeType(DESK_ITEMS_MIME_TYPE) ? DESK_ITEMS_MIME_TYPE : DESK_FILES_MIME_TYPE };

    // Over a favorite: into its folder (moved), the row highlighted
    if (const auto favorite { sidebar.favoriteAt(pos) })
    {
        setDropTarget({});
        sidebar.showInsertionLine(std::nullopt);
        sidebar.setDropTarget(favorite);
        offer.accept(mime);
        offer.setActions(CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move), MDNDOffer::Move);
        return;
    }

    sidebar.setDropTarget(std::nullopt);

    // Elsewhere over the favorites: a single folder is added at the end (the line at the bottom).
    // Desk's own drags are known; other apps' files are checked once dropped.
    if (sidebar.overFavorites(pos))
    {
        setDropTarget({});
        std::error_code ec;
        const auto &files { desk->dragFiles() };

        if (own && (files.size() != 1 || !fs::is_directory(files.front(), ec)))
        {
            sidebar.showInsertionLine(std::nullopt);
            offer.accept("");
            return;
        }

        sidebar.showInsertionLine(sidebar.favorites.items().size());
        offer.accept(mime);
        offer.setActions(MDNDOffer::Copy, MDNDOffer::Copy);
        return;
    }

    sidebar.showInsertionLine(std::nullopt);

    // Over a folder or disk: into it (moved, copied to disks)
    if (const auto target { dropTargetAt(offer, pos) })
    {
        setDropTarget(m_entries[*target].key);
        offer.accept(mime);
        offer.setActions(CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move),
                         m_entries[*target].kind == DeskEntry::Kind::Volume ? MDNDOffer::Copy : MDNDOffer::Move);
        return;
    }

    setDropTarget({});

    // Elsewhere over the files: into the folder shown (files already here stay). Not over the
    // rest of the sidebar or the toolbar
    if (m_stale || dragsFromHere(offer) || !SkRect::Make(view().worldRect()).contains(pos.x(), pos.y()))
    {
        offer.accept("");
        return;
    }

    offer.accept(mime);
    offer.setActions(CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move), MDNDOffer::Move);
}

void FolderWindow::dropped(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    clearDropFeedback();
    auto *desk { GetDesk() };

    // A favorite reordered: to where the line was
    if (reorderingFavorite(offer))
    {
        if (!SkRect::Make(sidebar.list.worldRect()).contains(pos.x(), pos.y()))
            return;

        const fs::path favorite { FolderSidebar::DraggedFavorite() };
        const size_t index { sidebar.favoritesIndexAt(pos) };

        if (const Int32 fd { offer.receive(DESK_FAVORITE_MIME_TYPE) }; fd >= 0)
            close(fd);

        offer.finish();
        desk->placeFavorites({ favorite }, index);
        return;
    }

    if (!desk->offersFiles(offer))
        return;

    // Into a favorite's folder
    if (const auto favorite { sidebar.favoriteAt(pos) })
    {
        desk->dropInto(*favorite, false, offer);
        return;
    }

    // A new favorite, at the end: a single folder (several files, or a file, are ignored)
    if (sidebar.overFavorites(pos))
    {
        const auto files { desk->receiveDrop(offer) };
        std::error_code ec;

        if (files.size() == 1 && fs::is_directory(files.front(), ec))
            desk->placeFavorites(files, sidebar.favorites.items().size());

        return;
    }

    if (const auto target { dropTargetAt(offer, pos) })
    {
        const DeskEntry entry { m_entries[*target] };
        desk->dropInto(entry.path, entry.kind == DeskEntry::Kind::Volume, offer);
        return;
    }

    if (!m_stale && !dragsFromHere(offer) && SkRect::Make(view().worldRect()).contains(pos.x(), pos.y()))
        desk->dropInto(m_dir, false, offer);
}

/* ------------------------------------------ Input ------------------------------------------ */

void FolderWindow::pointerMoveEvent(const CZPointerMoveEvent &e)
{
    MToplevel::pointerMoveEvent(e);
    sidebar.favoriteMoved();

    if (m_bandActive)
    {
        updateBand();
        return;
    }

    // A file pressed and moved far enough: dragged (the whole selection)
    if (!m_dragPressKey.empty() && m_press && SkPoint::Distance(AKApp::Get()->pointer().pos(), m_pressPos) >= DESK_DRAG_THRESHOLD)
    {
        const auto grabbed { indexOf(m_dragPressKey) };
        m_dragPressKey.clear();
        m_selectOnRelease = false;
        m_pressRenameKey.clear();
        m_lastClickMs = 0;

        if (grabbed)
            startDrag(*grabbed);

        m_press.reset();
        return;
    }

    if (!m_bandPossible || SkPoint::Distance(AKApp::Get()->pointer().pos(), m_bandPressPos) < DESK_DRAG_THRESHOLD)
        return;

    // Dragged far enough: a rubber band (adding to the selection with Ctrl/Shift)
    m_bandPossible = false;
    m_bandActive = true;
    m_selectOnRelease = false;
    m_pressRenameKey.clear();
    m_lastClickMs = 0;
    m_bandBase.clear();

    if (CtrlHeld() || ShiftHeld())
        m_bandBase = m_selected;

    scene().setPointerGrab(this);
    updateBand();
}

void FolderWindow::windowStateEvent(const CZWindowStateEvent &e)
{
    MToplevel::windowStateEvent(e);

    if (!e.changes.has(CZWinActivated))
        return;

    // Active: its menus in the menu bar. Inactive: the desktop's, unless another window is active
    if (e.newState.has(CZWinActivated))
    {
        if (m_menus)
            m_menus->activate();
    }
    else
        GetDesk()->windowDeactivated();
}

void FolderWindow::layoutEvent(const CZLayoutEvent &e)
{
    MToplevel::layoutEvent(e);

    // The name column keeps its share of the width (see layoutNameColumn())
    if (e.changes.has(CZLayoutChangeSize))
    {
        layoutNameColumn();
        placeRename();
    }
}

/* -------------------------------------- List columns -------------------------------------- */

void FolderWindow::applyColumns() noexcept
{
    content.listHeader.setColumns(m_columns);

    // Wider than the window: scrolled horizontally
    content.list.setMinContentWidth(FolderListHeader::ContentWidth(m_columns, DESK_WINDOW_LIST_INSET));

    for (const auto &[index, cell] : content.list.visibleCells())
        static_cast<FolderListRow*>(cell)->setColumns(m_columns);

    placeRename();
}

SkScalar FolderWindow::listViewportWidth() const noexcept
{
    // Hidden (the grid shown) it's not laid out: it's as wide as the content node
    const SkScalar list { content.list.visible() ? content.list.layout().calculatedWidth() : 0.f };
    return list > 0.f ? list : std::max(0.f, content.layout().calculatedWidth());
}

void FolderWindow::layoutNameColumn() noexcept
{
    const SkScalar viewport { listViewportWidth() };

    if (viewport <= 0.f)
        return;

    // As wide as chosen plus what the window grew since (minus what it shrank). Never chosen: the
    // room the other columns leave
    const auto &offset { GetDesk()->settings.listNameOffset };
    DeskListColumns others { m_columns };
    others.name = 0;
    const SkScalar width { offset ? viewport + SkScalar(*offset) : viewport - FolderListHeader::ContentWidth(others, DESK_WINDOW_LIST_INSET) };
    const Int32 name { std::max(DESK_LIST_NAME_MIN, Int32(std::lround(width))) };

    if (name == m_columns.name)
        return;

    m_columns.name = name;
    applyColumns();
}

void FolderWindow::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    MToplevel::pointerButtonEvent(e);

    // A favorite pressed (or released): maybe dragged to reorder it (see FolderSidebar::favoriteMoved())
    sidebar.favoritePressed(e);

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    // Clicks in the name being edited are its own
    if (renameField().active() && SkRect::Make(renameField().worldRect()).contains(pos.x(), pos.y()))
        return;

    if (!e.pressed)
    {
        m_bandPossible = false;

        if (m_bandActive && e.button == BTN_LEFT)
        {
            endBand();
            m_pressRenameKey.clear();
            return;
        }

        // Not dragged: a click on a file of the selection selects only it
        if (e.button == BTN_LEFT)
        {
            if (m_selectOnRelease && !m_dragPressKey.empty())
                if (const auto index { indexOf(m_dragPressKey) })
                    selectOnly(index);

            m_selectOnRelease = false;
            m_dragPressKey.clear();
            m_press.reset();
        }

        // Not a double click (yet): the name clicked is renamed if no other click follows soon
        if (e.button == BTN_LEFT && !m_pressRenameKey.empty())
        {
            m_renameCandidate = std::move(m_pressRenameKey);
            m_renameTimer.start(CZMouseSettings::DoubleClickMs());
        }

        m_pressRenameKey.clear();
        return;
    }

    m_renameTimer.stop();
    m_renameCandidate.clear();
    m_pressRenameKey.clear();
    m_bandPossible = false;
    m_dragPressKey.clear();
    m_press.reset();
    m_selectOnRelease = false;

    // Elsewhere: the name being edited is done (renamed if possible)
    endRename();

    // A favorite of the sidebar right clicked: its menu (like Finder's)
    if (e.button == BTN_RIGHT)
        if (const auto favorite { sidebar.favoriteAt(pos) })
        {
            openFavoriteMenu(*favorite, e);
            return;
        }

    // A column title of the list: sorted by it (again: reversed)
    if (e.button == BTN_LEFT)
        if (const auto column { content.listHeader.columnAt(pos) })
        {
            sortBy(*column, *column == m_sortColumn ? !m_sortAscending : true);
            return;
        }

    // Over the files: the window's keys from now on
    if (!SkRect::Make(view().worldRect()).contains(pos.x(), pos.y()))
        return;

    // The view's scroll bars are dragged by themselves (no selection change, no rubber band)
    if (view().verticalBar().hasPointerFocus() || view().horizontalBar().hasPointerFocus())
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

    // Icons: away from them (and their names), a rubber band may start (see pointerMoveEvent()).
    // Not in the list: its rows are dragged from anywhere.
    if (!target && !m_listView)
    {
        m_bandPossible = true;
        m_bandPressPos = pos;
        m_bandStart = toViewContent(pos);
    }

    if (!target)
    {
        m_lastClickMs = 0;

        if (!CtrlHeld() && !ShiftHeld())
            clearSelection();

        return;
    }

    const UInt64 now { CZTime::Ms() };

    const std::string &key { m_entries[*target].key };

    // The name of the only file selected: renamed on release (see above)
    if (!CtrlHeld() && !ShiftHeld() && m_selected.size() == 1 && isSelected(*target))
    {
        bool onName { false };

        if (m_listView)
            onName = nameBox(*target).makeOffset(content.worldRect().topLeft()).contains(Int32(pos.x()), Int32(pos.y()));
        else if (const AKNode *cell { view().cell(*target) })
            onName = static_cast<const FolderItem*>(cell)->nameContains(pos);

        if (onName)
            m_pressRenameKey = key;
    }

    // Double click: open (Ctrl: folders in a new window)
    if (m_lastClickKey == key && now - m_lastClickMs <= CZMouseSettings::DoubleClickMs())
    {
        m_pressRenameKey.clear();
        m_lastClickMs = 0;
        selectOnly(target);
        openSelection(CtrlHeld());
        return;
    }

    m_lastClickMs = now;
    m_lastClickKey = key;

    // The list: Shift selects a range (from the last row clicked), Ctrl adds or removes a row
    if (m_listView && ShiftHeld() && !CtrlHeld())
        selectRange(*target);
    else if (CtrlHeld() || ShiftHeld())
    {
        const bool selected { !isSelected(*target) };
        setSelected(*target, selected);

        if (!selected)
            return; // Nothing to drag

        m_lastSelectedKey = key;
        m_anchorKey = key;
    }
    else if (!isSelected(*target))
        selectOnly(target);
    else
        m_selectOnRelease = true; // Maybe a drag of the whole selection

    // Maybe the start of a drag (see pointerMoveEvent())
    m_dragPressKey = key;
    m_press = std::static_pointer_cast<CZPointerButtonEvent>(e.copy());
    m_pressPos = pos;
}

void FolderWindow::keyboardKeyEvent(const CZKeyboardKeyEvent &e)
{
    MToplevel::keyboardKeyEvent(e);

    // Typing a name (see RenameField)
    if (!e.isPressed || renameField().active())
        return;

    // The Enter that just renamed (the field is closed by now): not an open
    if ((e.code == KEY_ENTER || e.code == KEY_KPENTER) && m_renameSubmitMs != 0 && CZTime::Ms() - m_renameSubmitMs < 200)
    {
        m_renameSubmitMs = 0;
        return;
    }

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
    case KEY_F2:
        renameSelection();
        break;
    case KEY_1:
    case KEY_2:
        if (ctrl)
            setListView(e.code == KEY_2);
        break;
    case KEY_ESC:
        clearSelection();
        break;
    case KEY_A:
        if (ctrl)
            selectAll();
        break;
    case KEY_X:
    case KEY_C:
        if (ctrl)
            copySelection(e.code == KEY_X);
        break;
    case KEY_V:
        if (ctrl)
            paste();
        break;
    case KEY_O:
        if (ctrl)
            openSelection();
        break;
    case KEY_I:
        if (ctrl && !e.isRepeat)
            infoSelection();
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

void FolderWindow::infoSelection() noexcept
{
    if (const auto entries { selectedEntries() }; !entries.empty())
        GetDesk()->showInfo(entries);
    else
        GetDesk()->showInfo(m_dir);
}

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

        menu.addAction("Get Info", [this]{ infoSelection(); });

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

        // The clipboard (see Desk::copyFiles())
        if (std::any_of(copies.begin(), copies.end(), [](const DeskEntry &e){ return e.kind == DeskEntry::Kind::File; }))
        {
            menu.addDivider();
            menu.addAction("Cut", [this]{ copySelection(true); });
            menu.addAction("Copy", [this]{ copySelection(false); });
        }

        // In place (see renameSelection())
        if (single && first.kind == DeskEntry::Kind::File && !first.launcher)
        {
            menu.addDivider();
            menu.addAction("Rename", [this]{ renameSelection(); });
        }

        menu.addDivider();
        menu.addAction("Move to Trash", [this]{ trashSelection(); });
    });
}

void FolderWindow::openFavoriteMenu(const fs::path &folder, const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [folder](ContextMenu &menu){
        menu.addAction("Open in New Window", [folder]{ GetDesk()->openFolder(folder, true); });
        menu.addDivider();
        menu.addAction("Remove from Sidebar", [folder]{ GetDesk()->removeFavorite(folder); });
    });
}

void FolderWindow::openFolderMenu(const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [this](ContextMenu &menu){
        menu.addAction("New Folder", [this]{ newFolder(); });
        menu.addAction("Get Info", [dir = m_dir]{ GetDesk()->showInfo(dir); });
        menu.addAction("Paste", [this]{ paste(); }, GetDesk()->canPaste());
        menu.addDivider();
        menu.addAction("Show Hidden Files", [this]{ setShowsHidden(!m_showsHidden); }).setChecked(m_showsHidden);
        menu.addAction("Open in New Window", [dir = m_dir]{ GetDesk()->openFolder(dir, true); });
    });
}
