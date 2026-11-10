#ifndef CONTEXTMENU_H
#define CONTEXTMENU_H

#include <Core/Types.h>
#include <CZ/Marco/Roles/MMenu.h>
#include <CZ/Marco/Nodes/MMenuView.h>
#include <CZ/Marco/Nodes/MMenuAction.h>
#include <CZ/Marco/Nodes/MMenuDivider.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief A right click menu opened at the pointer (reused: its rows are rebuilt on each open()).
 */
class ContextMenu final : public MMenu
{
public:
    ContextMenu() noexcept;

    /// Builds the rows: call addAction() / addDivider() from it.
    using Builder = std::function<void(ContextMenu &menu)>;

    /**
     * @brief Opens the menu at @p pos (surface coordinates of @p parent).
     *
     * @param event The button press that requested it (for the popup grab).
     */
    void open(MSurface &parent, const SkIPoint &pos, const CZPointerButtonEvent &event, const Builder &build) noexcept;

    /// Unmaps the menu (rows are destroyed on the next open(), this may run from a row's handler).
    void close() noexcept { setMapped(false); }

    MMenuAction &addAction(const std::string &text, std::function<void()> onClicked, bool enabled = true) noexcept;
    void addDivider() noexcept;

private:
    std::unique_ptr<MMenuView> m_view;
    std::vector<std::unique_ptr<MMenuItem>> m_items;
};

#endif // CONTEXTMENU_H
