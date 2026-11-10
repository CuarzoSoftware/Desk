#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <Core/Types.h>
#include <Nodes/DeskItem.h>
#include <Nodes/SelectionBox.h>
#include <Surfaces/ContextMenu.h>
#include <CZ/Marco/Roles/MLayerSurface.h>
#include <CZ/Marco/Roles/MDND.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/Core/CZWeak.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

/**
 * @brief The desktop of one screen: a fully opaque layer surface on the background layer, covering
 *        the whole screen (under the top bar and the dock too).
 *
 * Its central node is filled with the (opaque) background color, so the whole surface is always
 * reported as opaque; the picture, if any, is drawn over it.
 *
 * On the primary screen it also shows the desktop icons (see Desk): a grid filled column by column
 * from the top-right corner, like Finder. Icons can be selected (click, Ctrl+click, rubber band),
 * opened (double click, Enter), moved to another cell or dropped on other apps (drag & drop), and
 * have context menus. The keyboard (Enter, Delete, Ctrl+A, Escape) works once the desktop is
 * clicked.
 */
class Background final : public MLayerSurface
{
public:
    Background(MScreen &screen) noexcept;
    ~Background() noexcept;

    /// Shows / hides the desktop icons (only the primary screen shows them).
    void setShowsIcons(bool shows) noexcept;
    bool showsIcons() const noexcept { return m_showsIcons; }

    /* Selection (the menu bar and the keyboard act on it) */
    std::vector<DeskItem*> selection() const noexcept;
    std::vector<const DeskEntry*> selectedEntries() const noexcept;
    void selectOnly(DeskItem *item) noexcept;
    void clearSelection() noexcept;
    void selectAll() noexcept;
    void openSelection() noexcept;
    void trashSelection() noexcept;

    /// Creates a new folder and selects it.
    void newFolder() noexcept;

    AKImageFrame picture { this };
    AKContainer icons { YGFlexDirectionColumn, false, this };
    SelectionBox rubberBand { this };

protected:
    void suggestedSizeChanged() override;
    void pointerButtonEvent(const CZPointerButtonEvent &event) override;
    void pointerMoveEvent(const CZPointerMoveEvent &event) override;
    void keyboardKeyEvent(const CZKeyboardKeyEvent &event) override;

private:
    /* Background */
    void applyBackground() noexcept;

    /* Items and grid */
    void rebuildItems() noexcept;
    void layoutItems() noexcept;
    void updateGrid() noexcept;
    SkIPoint cellOrigin(const SkIPoint &cell) const noexcept;
    SkIPoint cellAt(const SkPoint &topLeft) const noexcept;
    SkIRect iconRectAt(const SkIPoint &cell) const noexcept;
    SkRect labelRectAt(const DeskItem &item) const noexcept;
    DeskItem *itemAt(const SkPoint &pos) const noexcept;
    DeskItem *item(const std::string &key) const noexcept;

    void updateRubberBand() noexcept;
    void endRubberBand() noexcept;

    /* Drag & drop */
    void startDrag(DeskItem &item) noexcept;
    void dragOver(MDNDOffer &offer, const SkPoint &pos) noexcept;
    void dropped(MDNDOffer &offer, const SkPoint &pos) noexcept;
    void endDrag() noexcept;

    /* Menus */
    void openItemsMenu(const CZPointerButtonEvent &event) noexcept;
    void openDesktopMenu(const CZPointerButtonEvent &event) noexcept;

    static bool CtrlOrShiftHeld() noexcept;
    static bool CtrlHeld() noexcept;
    static bool ShiftHeld() noexcept;

    SkISize m_size {};
    bool m_showsIcons { false };

    std::vector<std::unique_ptr<DeskItem>> m_items; // Entry order (auto placement order)
    Int32 m_iconSize { DESK_ICON_SIZE_DEFAULT };
    SkISize m_cell {};
    Int32 m_cols { 1 }, m_rows { 1 };

    // Pointer interaction
    CZWeak<DeskItem> m_pressItem;
    std::shared_ptr<CZPointerButtonEvent> m_press;
    SkPoint m_pressPos {};
    bool m_selectOnRelease { false }; // Click on an already selected item: select only it on release
    UInt64 m_lastClickMs { 0 };
    std::string m_lastClickKey;

    bool m_rubberActive { false };
    SkPoint m_rubberStart {};
    std::unordered_set<std::string> m_rubberBase; // Kept selected (Ctrl/Shift + rubber band)

    // Drag & drop (the session and its icon surface are reused)
    std::unique_ptr<MDND> m_dnd;
    std::unique_ptr<AKImageFrame> m_dndIcon;
    std::vector<std::string> m_dragKeys;
    std::string m_dragGrabKey;
    SkPoint m_dragOffset {}; // Pointer position within the grabbed item's cell

    std::unique_ptr<ContextMenu> m_menu;
};

#endif // BACKGROUND_H
