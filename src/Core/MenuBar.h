#ifndef MENUBAR_H
#define MENUBAR_H

#include <Core/Types.h>
#include <CZ/Heaven/Heaven.h>
#include <CZ/Core/CZObject.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Desk's menus in the global menu bar (a Heaven client), modeled after Finder's.
 *
 * The compositor shows them while no window is active (e.g. after clicking the desktop). They act
 * on the desktop of the primary screen: its selection, icons and background.
 *
 * - **File**: New Window, New Folder, Open, Open With, Move to Trash, Eject, Set Desktop Picture
 * - **Edit**: Select All (Undo, Cut, Copy and Paste are shown disabled, like Finder with icons)
 * - **View**: Clean Up, icon size, desktop picture options
 * - **Go**: the user folders, Computer and Trash
 *
 * The shortcuts (⌘ = Ctrl) also work from the keyboard while the desktop has focus (shortcut()).
 */
class MenuBar final : public CZObject
{
public:
    MenuBar() noexcept;
    ~MenuBar() noexcept;

    /// Refreshes the enabled/checked states (selection or settings changed) and commits.
    void update() noexcept;

    /// Runs the menu item with the shortcut Ctrl(+Shift)+@p key. Returns false if there's none.
    bool shortcut(UInt32 key, bool shift) noexcept;

private:
    using Action = std::function<void()>;

    std::shared_ptr<CZ::HNClientAPI::HNAction> addAction(CZ::HNClientAPI::HNObject *menu, const std::string &title,
        const std::string &shortcut, Action action, bool enabled = true) noexcept;
    std::shared_ptr<CZ::HNClientAPI::HNToggle> addToggle(CZ::HNClientAPI::HNObject *menu, const std::string &title, Action action) noexcept;
    std::shared_ptr<CZ::HNClientAPI::HNMenu> addMenu(CZ::HNClientAPI::HNObject *parent, const std::string &title) noexcept;
    void addDivider(CZ::HNClientAPI::HNObject *menu) noexcept;
    void addGoItem(const std::string &title, const std::string &shortcut, const std::filesystem::path &path) noexcept;
    void updateOpenWith() noexcept;

    std::shared_ptr<CZ::HNClientAPI::HNClient> m_client;
    std::shared_ptr<CZ::HNClientAPI::HNTopbar> m_topbar;
    std::shared_ptr<CZ::HNClientAPI::HNMenu> m_file, m_edit, m_view, m_go, m_openWith;

    // Every static object (kept alive: destroying it removes it from the bar).
    std::vector<std::shared_ptr<CZ::HNClientAPI::HNObject>> m_objects;

    // The Open With submenu's items, rebuilt when the selection changes.
    std::vector<std::shared_ptr<CZ::HNClientAPI::HNObject>> m_openWithItems;
    std::string m_openWithKey;

    std::shared_ptr<CZ::HNClientAPI::HNAction> m_open, m_trash, m_eject, m_setPicture, m_larger, m_smaller, m_cleanUp;
    std::shared_ptr<CZ::HNClientAPI::HNToggle> m_showPicture, m_fill, m_fit, m_stretch;

    struct Shortcut
    {
        UInt32 key;
        bool shift;
        std::shared_ptr<CZ::HNClientAPI::HNAction> item; // Runs only if enabled
        Action action;
    };

    std::vector<Shortcut> m_shortcuts;
};

#endif // MENUBAR_H
