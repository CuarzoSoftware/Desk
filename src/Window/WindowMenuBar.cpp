#include <Window/WindowMenuBar.h>
#include <Window/FolderWindow.h>
#include <Core/Desk.h>
#include <CZ/Heaven/Client/HNClient.h>
#include <CZ/Heaven/Client/HNTopbar.h>
#include <CZ/Heaven/Client/HNMenu.h>
#include <CZ/Heaven/Client/HNAction.h>
#include <CZ/Heaven/Client/HNToggle.h>
#include <CZ/Heaven/Client/HNDivider.h>
#include <algorithm>

using namespace CZ::HNClientAPI;
namespace fs = std::filesystem;

WindowMenuBar::WindowMenuBar(FolderWindow &window) noexcept : m_window(window)
{
    m_updateTimer.setCallback([this](CZTimer*){ update(); });

    // Desk's client (shared with the desktop's menus, see MenuBar)
    m_client = HNClient::GetOrMake();

    if (!m_client)
        return;

    auto *desk { GetDesk() };
    FolderWindow *w { &window };
    m_topbar = HNTopbar::Make();

    /* File */

    auto file { addMenu(m_topbar.get(), "File") };
    addAction(file.get(), "New Window", "⌘N", [w]{ GetDesk()->openFolder(w->folder(), true); });
    addAction(file.get(), "New Folder", "⇧⌘N", [w]{ w->newFolder(); });
    addDivider(file.get());
    m_open = addAction(file.get(), "Open", "⌘O", [w]{ w->openSelection(); });
    m_openNewWindow = addAction(file.get(), "Open in New Window", "", [w]{ w->openSelection(true); });
    addDivider(file.get());
    addAction(file.get(), "Get Info", "⌘I", [w]{ w->infoSelection(); }); // Nothing selected: the folder's
    addDivider(file.get());
    m_rename = addAction(file.get(), "Rename", "F2", [w]{ w->renameSelection(); });
    m_trash = addAction(file.get(), "Move to Trash", "⌘⌫", [w]{ w->trashSelection(); });
    addDivider(file.get());
    addAction(file.get(), "Close Window", "⌘W", [w]{ w->setMapped(false); }); // Destroyed by Desk

    /* Edit (files: see Desk::copyFiles()) */

    auto edit { addMenu(m_topbar.get(), "Edit") };
    addAction(edit.get(), "Undo", "⌘Z", nullptr, false);
    addAction(edit.get(), "Redo", "⇧⌘Z", nullptr, false);
    addDivider(edit.get());
    m_cut = addAction(edit.get(), "Cut", "⌘X", [w]{ w->copySelection(true); });
    m_copy = addAction(edit.get(), "Copy", "⌘C", [w]{ w->copySelection(false); });
    m_paste = addAction(edit.get(), "Paste", "⌘V", [w]{ w->paste(); });
    addAction(edit.get(), "Select All", "⌘A", [w]{ w->selectAll(); });

    /* View */

    auto view { addMenu(m_topbar.get(), "View") };
    m_icons = addToggle(view.get(), "as Icons", "⌘1", [w]{ w->setListView(false); });
    m_list = addToggle(view.get(), "as List", "⌘2", [w]{ w->setListView(true); });
    addDivider(view.get());
    auto sort { addMenu(view.get(), "Sort By") };
    using Column = FolderWindow::SortColumn;

    for (const auto &[title, column] : { std::pair { "Name", Column::Name }, std::pair { "Date Modified", Column::Date },
                                         std::pair { "Size", Column::Size }, std::pair { "Kind", Column::Kind } })
        m_sortToggles.emplace_back(addToggle(sort.get(), title, "", [w, column]{
            // Again: reversed (like clicking the list's column title)
            w->sortBy(column, w->sortColumn() == column ? !w->sortAscending() : true);
        }), int(column));

    addDivider(view.get());
    m_hidden = addToggle(view.get(), "Show Hidden Files", "⌘H", [w]{ w->setShowsHidden(!w->showsHidden()); });

    /* Go */

    auto go { addMenu(m_topbar.get(), "Go") };
    m_back = addAction(go.get(), "Back", "⌘[", [w]{ w->goBack(); });
    m_forward = addAction(go.get(), "Forward", "⌘]", [w]{ w->goForward(); });
    m_up = addAction(go.get(), "Enclosing Folder", "⌘↑", [w]{ w->goUp(); });
    addDivider(go.get());

    for (const auto &[title, shortcut, dir] : {
            std::tuple { "Documents", "⇧⌘O", desk->userDir("DOCUMENTS", "Documents") },
            std::tuple { "Desktop", "⇧⌘D", desk->desktopDir() },
            std::tuple { "Downloads", "⇧⌘L", desk->userDir("DOWNLOAD", "Downloads") },
            std::tuple { "Home", "⇧⌘H", fs::path(desk->xdgkit->homeDir()) },
            std::tuple { "Computer", "⇧⌘C", fs::path("/") } })
        addAction(go.get(), title, shortcut, [w, dir]{ w->go(dir); });

    addDivider(go.get());

    for (const auto &[title, dir] : { std::pair { "Pictures", desk->userDir("PICTURES", "Pictures") },
                                      std::pair { "Music", desk->userDir("MUSIC", "Music") },
                                      std::pair { "Videos", desk->userDir("VIDEOS", "Videos") } })
        addAction(go.get(), title, "", [w, dir]{ w->go(dir); });

    update();
}

WindowMenuBar::~WindowMenuBar() noexcept = default;

std::shared_ptr<HNMenu> WindowMenuBar::addMenu(HNObject *parent, const std::string &title) noexcept
{
    auto menu { HNMenu::Make(title, "", true, parent) };
    m_objects.emplace_back(menu);
    return menu;
}

std::shared_ptr<HNAction> WindowMenuBar::addAction(HNObject *menu, const std::string &title, const std::string &shortcut, Action action, bool enabled) noexcept
{
    auto item { HNAction::Make(title, "", shortcut, enabled, menu) };

    if (action)
        item->onClicked.subscribe(this, [action](HNObject*){ action(); });

    m_objects.emplace_back(item);
    return item;
}

std::shared_ptr<HNToggle> WindowMenuBar::addToggle(HNObject *menu, const std::string &title, const std::string &shortcut, Action action) noexcept
{
    // Checked as the window is (see update())
    auto item { HNToggle::Make(title, "", shortcut, false, true, menu) };
    item->onClicked.subscribe(this, [action](HNObject*){ action(); });
    m_objects.emplace_back(item);
    return item;
}

void WindowMenuBar::addDivider(HNObject *menu) noexcept
{
    m_objects.emplace_back(HNDivider::Make("", menu));
}

void WindowMenuBar::activate() noexcept
{
    if (!m_client || !m_topbar)
        return;

    update();
    m_client->setActiveTopbar(m_topbar.get());
    m_client->commit();
}

void WindowMenuBar::scheduleUpdate() noexcept
{
    if (m_client && !m_updateTimer.running())
        m_updateTimer.start(0);
}

void WindowMenuBar::update() noexcept
{
    m_updateTimer.stop();

    if (!m_client || !m_topbar)
        return;

    const auto entries { m_window.selectedEntries() };
    const bool single { entries.size() == 1 };
    const bool folder { single && (entries.front()->kind == DeskEntry::Kind::Volume ||
                                   (!entries.front()->launcher && entries.front()->mimeType == "inode/directory")) };
    const bool anyFile { std::any_of(entries.begin(), entries.end(), [](const DeskEntry *e){ return e->kind == DeskEntry::Kind::File; }) };
    const fs::path &dir { m_window.folder() };

    m_open->setEnabled(!entries.empty());
    m_openNewWindow->setEnabled(folder);
    m_rename->setEnabled(single && entries.front()->kind == DeskEntry::Kind::File && !entries.front()->launcher);
    m_trash->setEnabled(anyFile);
    m_cut->setEnabled(anyFile);
    m_copy->setEnabled(anyFile);
    m_paste->setEnabled(GetDesk()->canPaste());
    m_back->setEnabled(m_window.canGoBack());
    m_forward->setEnabled(m_window.canGoForward());
    m_up->setEnabled(dir.has_parent_path() && dir.parent_path() != dir);

    m_icons->setChecked(!m_window.listView());
    m_list->setChecked(m_window.listView());
    m_hidden->setChecked(m_window.showsHidden());

    for (auto &[toggle, column] : m_sortToggles)
        toggle->setChecked(int(m_window.sortColumn()) == column);

    m_client->commit();
}
