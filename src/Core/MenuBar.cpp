#include <Core/MenuBar.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <Surfaces/Background.h>
#include <CZ/Heaven/Client/HNClient.h>
#include <CZ/Heaven/Client/HNTopbar.h>
#include <CZ/Heaven/Client/HNMenu.h>
#include <CZ/Heaven/Client/HNAction.h>
#include <CZ/Heaven/Client/HNToggle.h>
#include <CZ/Heaven/Client/HNDivider.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <linux/input-event-codes.h>
#include <algorithm>

using namespace CZ::HNClientAPI;
namespace fs = std::filesystem;

MenuBar::MenuBar() noexcept
{
    m_client = HNClient::GetOrMake();

    if (!m_client)
    {
        Log(CZWarning, CZLN, "Heaven unavailable: no menus in the global menu bar");
        return;
    }

    auto *desk { GetDesk() };
    m_client->setName("Desk");
    m_topbar = HNTopbar::Make();

    /* File */

    m_file = addMenu(m_topbar.get(), "File");
    addAction(m_file.get(), "New Window", "⌘N", [desk]{ desk->openFolder(desk->xdgkit->homeDir()); });
    m_shortcuts.push_back({ KEY_N, false, nullptr, [desk]{ desk->openFolder(desk->xdgkit->homeDir()); } });
    addAction(m_file.get(), "New Folder", "⇧⌘N", [desk]{ if (auto *bg { desk->iconsBackground() }) bg->newFolder(); });
    m_shortcuts.push_back({ KEY_N, true, nullptr, [desk]{ if (auto *bg { desk->iconsBackground() }) bg->newFolder(); } });
    addDivider(m_file.get());
    m_open = addAction(m_file.get(), "Open", "⌘O", [desk]{ if (auto *bg { desk->iconsBackground() }) bg->openSelection(); });
    m_shortcuts.push_back({ KEY_O, false, m_open, [desk]{ if (auto *bg { desk->iconsBackground() }) bg->openSelection(); } });
    m_openWith = addMenu(m_file.get(), "Open With");
    addDivider(m_file.get());
    m_trash = addAction(m_file.get(), "Move to Trash", "⌘⌫", [desk]{ if (auto *bg { desk->iconsBackground() }) bg->trashSelection(); });
    m_shortcuts.push_back({ KEY_BACKSPACE, false, m_trash, [desk]{ if (auto *bg { desk->iconsBackground() }) bg->trashSelection(); } });
    const auto eject = [desk]{
        if (auto *bg { desk->iconsBackground() })
            for (const DeskEntry *e : bg->selectedEntries())
                desk->eject(*e);
    };
    m_eject = addAction(m_file.get(), "Eject", "⌘E", eject);
    m_shortcuts.push_back({ KEY_E, false, m_eject, eject });
    addDivider(m_file.get());
    m_setPicture = addAction(m_file.get(), "Set Desktop Picture", "", [desk]{
        if (auto *bg { desk->iconsBackground() })
            if (auto entries { bg->selectedEntries() }; entries.size() == 1)
                desk->setPicture(entries.front()->path);
    });

    /* Edit (like Finder: the clipboard isn't supported on the desktop yet) */

    m_edit = addMenu(m_topbar.get(), "Edit");
    addAction(m_edit.get(), "Undo", "⌘Z", nullptr, false);
    addAction(m_edit.get(), "Redo", "⇧⌘Z", nullptr, false);
    addDivider(m_edit.get());
    addAction(m_edit.get(), "Cut", "⌘X", nullptr, false);
    addAction(m_edit.get(), "Copy", "⌘C", nullptr, false);
    addAction(m_edit.get(), "Paste", "⌘V", nullptr, false);
    auto selectAll { addAction(m_edit.get(), "Select All", "⌘A", [desk]{ if (auto *bg { desk->iconsBackground() }) bg->selectAll(); }) };
    m_shortcuts.push_back({ KEY_A, false, selectAll, [desk]{ if (auto *bg { desk->iconsBackground() }) bg->selectAll(); } });

    /* View */

    m_view = addMenu(m_topbar.get(), "View");

    auto sortMenu { addMenu(m_view.get(), "Sort By") };

    for (const auto &[title, sortBy] : { std::pair { "Name", Settings::SortBy::Name }, std::pair { "Kind", Settings::SortBy::Kind },
                                         std::pair { "Size", Settings::SortBy::Size }, std::pair { "Custom", Settings::SortBy::Custom } })
        m_sortToggles.emplace_back(addToggle(sortMenu.get(), title, [desk, sortBy]{ desk->setSortBy(sortBy); }), sortBy);

    m_cleanUp = addAction(m_view.get(), "Clean Up", "", [desk]{ desk->clearPositions(); });
    addDivider(m_view.get());

    auto sizeMenu { addMenu(m_view.get(), "Icon Size") };

    for (const Int32 size : DESK_ICON_SIZES)
        m_sizeToggles.emplace_back(addToggle(sizeMenu.get(), std::to_string(size) + " pt", [desk, size]{ desk->setIconSize(size); }), size);

    m_larger = addAction(m_view.get(), "Larger Icons", "⌘+", [desk]{ desk->setIconSize(desk->largerIconSize()); });
    m_shortcuts.push_back({ KEY_EQUAL, false, m_larger, [desk]{ desk->setIconSize(desk->largerIconSize()); } });
    m_shortcuts.push_back({ KEY_KPPLUS, false, m_larger, [desk]{ desk->setIconSize(desk->largerIconSize()); } });
    m_smaller = addAction(m_view.get(), "Smaller Icons", "⌘−", [desk]{ desk->setIconSize(desk->smallerIconSize()); });
    m_shortcuts.push_back({ KEY_MINUS, false, m_smaller, [desk]{ desk->setIconSize(desk->smallerIconSize()); } });
    m_shortcuts.push_back({ KEY_KPMINUS, false, m_smaller, [desk]{ desk->setIconSize(desk->smallerIconSize()); } });
    addDivider(m_view.get());
    m_showPicture = addToggle(m_view.get(), "Show Desktop Picture", [desk]{ desk->setUsePicture(!desk->settings.usePicture); });
    m_fill = addToggle(m_view.get(), "Fill Screen", [desk]{ desk->setFit(Settings::Fit::Cover); });
    m_fit = addToggle(m_view.get(), "Fit to Screen", [desk]{ desk->setFit(Settings::Fit::Contain); });
    m_stretch = addToggle(m_view.get(), "Stretch to Fill Screen", [desk]{ desk->setFit(Settings::Fit::Fill); });

    /* Go */

    m_go = addMenu(m_topbar.get(), "Go");
    const fs::path home { desk->xdgkit->homeDir() };
    addGoItem("Documents", "⇧⌘O", desk->userDir("DOCUMENTS", "Documents"));
    addGoItem("Desktop", "⇧⌘D", desk->desktopDir());
    addGoItem("Downloads", "⇧⌘L", desk->userDir("DOWNLOAD", "Downloads"));
    addGoItem("Home", "⇧⌘H", home);
    addGoItem("Computer", "⇧⌘C", "/");
    addDivider(m_go.get());
    addGoItem("Pictures", "", desk->userDir("PICTURES", "Pictures"));
    addGoItem("Music", "", desk->userDir("MUSIC", "Music"));
    addGoItem("Videos", "", desk->userDir("VIDEOS", "Videos"));
    addDivider(m_go.get());
    addAction(m_go.get(), "Trash", "", []{ Desk::Spawn({ "xdg-open", "trash:///" }); });

    // The app menu's Quit doesn't apply: the desktop is always there (like Finder by default).
    m_client->onQuit.subscribe(this, []{ Log(CZInfo, CZLN, "Ignoring Quit: the desktop keeps running"); });

    m_client->setActiveTopbar(m_topbar.get());
    m_client->setPrivateHandle(desk->marco->privateHandle());
    desk->marco->onPrivateHandleChanged.subscribe(this, [this]{
        m_client->setPrivateHandle(GetDesk()->marco->privateHandle());
    });

    update();
}

MenuBar::~MenuBar() noexcept = default;

std::shared_ptr<HNMenu> MenuBar::addMenu(HNObject *parent, const std::string &title) noexcept
{
    auto menu { HNMenu::Make(title, "", true, parent) };
    m_objects.emplace_back(menu);
    return menu;
}

std::shared_ptr<HNAction> MenuBar::addAction(HNObject *menu, const std::string &title, const std::string &shortcut, Action action, bool enabled) noexcept
{
    auto item { HNAction::Make(title, "", shortcut, enabled, menu) };

    if (action)
        item->onClicked.subscribe(this, [action](HNObject*){ action(); });

    m_objects.emplace_back(item);
    return item;
}

std::shared_ptr<HNToggle> MenuBar::addToggle(HNObject *menu, const std::string &title, Action action) noexcept
{
    auto item { HNToggle::Make(title, "", "", false, true, menu) };

    // The checked state follows the settings (see update()).
    item->onClicked.subscribe(this, [action](HNObject*){ action(); });
    m_objects.emplace_back(item);
    return item;
}

void MenuBar::addDivider(HNObject *menu) noexcept
{
    m_objects.emplace_back(HNDivider::Make("", menu));
}

void MenuBar::addGoItem(const std::string &title, const std::string &shortcut, const fs::path &path) noexcept
{
    const auto go = [path]{ GetDesk()->openFolder(path); };
    auto item { addAction(m_go.get(), title, shortcut, go) };

    // "⇧⌘X" → Ctrl+Shift+X
    if (!shortcut.empty())
    {
        static const std::pair<char, UInt32> keys[] { { 'O', KEY_O }, { 'D', KEY_D }, { 'L', KEY_L }, { 'H', KEY_H }, { 'C', KEY_C } };

        for (const auto &[c, key] : keys)
            if (shortcut.back() == c)
                m_shortcuts.push_back({ key, true, item, go });
    }
}

void MenuBar::updateOpenWith() noexcept
{
    auto *desk { GetDesk() };
    auto *bg { desk->iconsBackground() };
    const auto entries { bg ? bg->selectedEntries() : std::vector<const DeskEntry*> {} };
    const DeskEntry *file { entries.size() == 1 && entries.front()->kind == DeskEntry::Kind::File && !entries.front()->launcher ? entries.front() : nullptr };
    const std::string key { file ? file->key + "|" + file->mimeType : std::string {} };

    if (key == m_openWithKey)
        return;

    m_openWithKey = key;
    m_openWithItems.clear();

    if (file)
    {
        auto apps { desk->appsFor(*file) };

        for (size_t i = 0; i < apps.size(); i++)
        {
            if (apps[i]->noDisplay)
                continue;

            auto item { HNAction::Make(i == 0 ? apps[i]->name + " (default)" : apps[i]->name, apps[i]->icon, "", true, m_openWith.get()) };
            item->onClicked.subscribe(this, [fileKey = file->key, app = apps[i]](HNObject*){
                if (const DeskEntry *e { GetDesk()->entry(fileKey) })
                    GetDesk()->openWith(*e, *app);
            });
            m_openWithItems.emplace_back(item);

            if (i == 0 && apps.size() > 1)
                m_openWithItems.emplace_back(HNDivider::Make("", m_openWith.get()));
        }
    }

    m_openWith->setEnabled(!m_openWithItems.empty());
}

void MenuBar::update() noexcept
{
    if (!m_client)
        return;

    auto *desk { GetDesk() };
    auto *bg { desk->iconsBackground() };
    const auto entries { bg ? bg->selectedEntries() : std::vector<const DeskEntry*> {} };
    const bool single { entries.size() == 1 };
    const bool anyFile { std::any_of(entries.begin(), entries.end(), [](const DeskEntry *e){ return e->kind == DeskEntry::Kind::File; }) };
    const bool anyEjectable { std::any_of(entries.begin(), entries.end(), [](const DeskEntry *e){ return e->kind == DeskEntry::Kind::Volume && !e->internal; }) };
    const bool picture { single && entries.front()->kind == DeskEntry::Kind::File &&
        (Assets::HasThumbnail(entries.front()->mimeType) || entries.front()->mimeType == "image/svg+xml") };
    const Settings &s { desk->settings };

    m_open->setEnabled(!entries.empty());
    m_trash->setEnabled(anyFile);
    m_eject->setEnabled(anyEjectable);
    m_setPicture->setEnabled(picture);
    m_cleanUp->setEnabled(s.sortBy == Settings::SortBy::Custom && !s.positions.empty());

    for (auto &[toggle, sortBy] : m_sortToggles)
        toggle->setChecked(s.sortBy == sortBy);

    for (auto &[toggle, size] : m_sizeToggles)
        toggle->setChecked(s.iconSize == size);
    m_larger->setEnabled(s.iconSize < DESK_ICON_SIZE_MAX);
    m_smaller->setEnabled(s.iconSize > DESK_ICON_SIZE_MIN);

    m_showPicture->setEnabled(!s.picture.empty());
    m_showPicture->setChecked(s.showsPicture());

    const std::pair<HNToggle*, Settings::Fit> fits[] {
        { m_fill.get(), Settings::Fit::Cover }, { m_fit.get(), Settings::Fit::Contain }, { m_stretch.get(), Settings::Fit::Fill } };

    for (const auto &[toggle, fit] : fits)
    {
        toggle->setEnabled(s.showsPicture());
        toggle->setChecked(s.fit == fit);
    }

    updateOpenWith();
    m_client->commit();
}

bool MenuBar::shortcut(UInt32 key, bool shift) noexcept
{
    for (const auto &s : m_shortcuts)
    {
        if (s.key != key || s.shift != shift)
            continue;

        if (s.item && !s.item->enabled())
            return true; // Disabled: swallowed

        s.action();
        return true;
    }

    return false;
}
