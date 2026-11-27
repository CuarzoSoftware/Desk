#ifndef WINDOWMENUBAR_H
#define WINDOWMENUBAR_H

#include <Core/Types.h>
#include <CZ/Heaven/Heaven.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZTimer.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class FolderWindow;

/**
 * @brief A folder window's menus in the global menu bar (Heaven), modeled after Finder's.
 *
 * Each window has its own (an HNTopbar of Desk's Heaven client), shown while it's the active one
 * (activate()); the desktop's are MenuBar's. They act on their window:
 *
 * - **File**: New Window, New Folder, Open, Open in New Window, Get Info, Rename, Move to Trash, Close Window
 * - **Edit**: Cut, Copy, Paste (files, see Desk::copyFiles()), Select All (Undo and Redo disabled)
 * - **View**: as Icons / as List, Sort By (Name, Date Modified, Size, Kind), Show Hidden Files
 * - **Go**: Back, Forward, Enclosing Folder, the user folders and Computer
 *
 * Their shortcuts are the window's (see FolderWindow::keyboardKeyEvent()).
 */
class WindowMenuBar final : public CZObject
{
public:
    WindowMenuBar(FolderWindow &window) noexcept;
    ~WindowMenuBar() noexcept;

    /// Shown in the menu bar (the window was activated).
    void activate() noexcept;

    /// Refreshes the enabled/checked states soon (the selection, the folder or the view changed).
    void scheduleUpdate() noexcept;

private:
    using Action = std::function<void()>;
    void update() noexcept;
    std::shared_ptr<CZ::HNClientAPI::HNMenu> addMenu(CZ::HNClientAPI::HNObject *parent, const std::string &title) noexcept;
    std::shared_ptr<CZ::HNClientAPI::HNAction> addAction(CZ::HNClientAPI::HNObject *menu, const std::string &title,
        const std::string &shortcut, Action action, bool enabled = true) noexcept;
    std::shared_ptr<CZ::HNClientAPI::HNToggle> addToggle(CZ::HNClientAPI::HNObject *menu, const std::string &title,
        const std::string &shortcut, Action action) noexcept;
    void addDivider(CZ::HNClientAPI::HNObject *menu) noexcept;

    FolderWindow &m_window;
    std::shared_ptr<CZ::HNClientAPI::HNClient> m_client;
    std::shared_ptr<CZ::HNClientAPI::HNTopbar> m_topbar;
    std::vector<std::shared_ptr<CZ::HNClientAPI::HNObject>> m_objects; // Kept alive (destroyed: removed from the bar)
    std::shared_ptr<CZ::HNClientAPI::HNAction> m_cut, m_copy, m_paste;
    std::shared_ptr<CZ::HNClientAPI::HNAction> m_open, m_openNewWindow, m_rename, m_trash, m_back, m_forward, m_up;
    std::shared_ptr<CZ::HNClientAPI::HNToggle> m_icons, m_list, m_hidden;
    std::vector<std::pair<std::shared_ptr<CZ::HNClientAPI::HNToggle>, int>> m_sortToggles; // By FolderListHeader::Column
    CZTimer m_updateTimer;
};

#endif // WINDOWMENUBAR_H
