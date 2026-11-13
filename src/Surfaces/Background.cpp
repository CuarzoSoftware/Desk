#include <filesystem>
#include <string_view>
#include <chrono>
#include <poll.h>
#include <Surfaces/Background.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <CZ/AK/AKApp.h>
#include <CZ/AK/AKScene.h>
#include <CZ/Core/CZCore.h>
#include <CZ/Core/CZKeymap.h>
#include <CZ/Core/CZTime.h>
#include <CZ/Core/CZEase.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZPointerMoveEvent.h>
#include <CZ/Core/Events/CZKeyboardKeyEvent.h>
#include <CZ/Ream/RImage.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <map>
#include <cmath>
#include <unistd.h>

namespace fs = std::filesystem;

Background::Background(MScreen &screen) noexcept :
    MLayerSurface(),
    m_gather(DESK_DRAG_GATHER_MS,
        [this](CZAnimation *a){ gatherStep(a->value()); },
        [this](CZAnimation *){ gatherFinished(); })
{
    screen.userData = this;
    setScreen(&screen);
    setScope("cz-desk");
    setLayer(Layer::Background);
    setAnchor(CZEdgeLeft | CZEdgeTop | CZEdgeRight | CZEdgeBottom);
    setExclusiveZone(-1); // The whole screen, under the top bar and the dock
    setKeyboardInteractivity(KeyboardInteractivity::OnDemand);
    setKeyboardFocusable(true); // Takes the keyboard when clicked (see pointerButtonEvent())

    renameField.onRenamed = [this](const std::string &key) {
        if (DeskItem *renamed { item(key) })
            selectOnly(renamed);
    };

    m_renameTimer.setCallback([this](CZTimer*) {
        DeskItem *target { item(m_renameCandidate) };
        m_renameCandidate.clear();

        if (target && target->selected() && selection().size() == 1)
            startRename(*target);
    });
    requestAvailableWidth();
    requestAvailableHeight();

    // Until the compositor tells the size: the screen's.
    for (const auto &mode : screen.props().modes)
    {
        if (!mode.isCurrent)
            continue;

        const Int32 scale { std::max(1, screen.props().scale) };
        const bool rotated { CZ::Is90Transform(screen.props().transform) };
        m_size = { (rotated ? mode.size.height() : mode.size.width()) / scale,
                   (rotated ? mode.size.width() : mode.size.height()) / scale };
        layout().setWidth(m_size.width());
        layout().setHeight(m_size.height());
    }

    for (AKNode *node : { (AKNode*)&picture, (AKNode*)&icons })
    {
        node->layout().setPositionType(YGPositionTypeAbsolute);
        node->layout().setPosition(YGEdgeLeft, 0.f);
        node->layout().setPosition(YGEdgeTop, 0.f);
        node->layout().setWidthPercent(100.f);
        node->layout().setHeightPercent(100.f);
    }

    SkRegion empty;
    picture.setInputRegion(&empty);
    icons.setInputRegion(&empty);
    picture.setAlignment(CZAlignCenter);

    auto *desk { GetDesk() };
    desk->onBackgroundChanged.subscribe(this, [this]{ applyBackground(); });
    desk->onEntriesChanged.subscribe(this, [this]{ rebuildItems(); });
    desk->onIconsChanged.subscribe(this, [this]{ rebuildItems(); });
    desk->assets.onThumbnailReady.subscribe(this, [this](const fs::path &path){
        for (auto &item : m_items)
            item->thumbnailReady(path.string());
    });

    // Drag & drop over the desktop (moving icons).
    onDNDEnter.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dragOver(offer, pos); });
    onDNDMotion.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dragOver(offer, pos); });
    onDNDDrop.subscribe(this, [this](MDNDOffer &offer, const SkPoint &pos){ dropped(offer, pos); });
    onDNDLeave.subscribe(this, [this](MDNDOffer &){ setDropTarget(nullptr); });

    applyBackground();
    setMapped(true);
}

Background::~Background() noexcept
{
    if (m_dnd && m_dnd->dragging())
        m_dnd->cancel();
}

/* --------------------------------------- Background --------------------------------------- */

void Background::applyBackground() noexcept
{
    auto *desk { GetDesk() };
    const Settings &s { desk->settings };

    // The whole surface is filled with an opaque color: it's always fully opaque (the compositor
    // can skip everything behind it), with or without a picture.
    setColor(CZAdaptiveColor(s.color | 0xFF000000));

    std::shared_ptr<RImage> image;

    if (s.showsPicture())
    {
        const Int32 scale { screen() ? std::max(1, screen()->props().scale) : 1 };
        image = desk->assets.picture(s.picture, { m_size.width() * scale, m_size.height() * scale }, s.fit);
    }

    picture.setImage(image);
    picture.setVisible(image != nullptr);
    picture.setSizeMode(s.fit == Settings::Fit::Contain ? AKImageFrame::SizeMode::Contain :
                        s.fit == Settings::Fit::Fill ? AKImageFrame::SizeMode::Fill : AKImageFrame::SizeMode::Cover);
}

void Background::suggestedSizeChanged()
{
    MLayerSurface::suggestedSizeChanged();

    const SkISize size { suggestedSize() };

    if (size.isEmpty() || size == m_size)
        return;

    m_size = size;
    applyBackground(); // The picture is decoded at the screen size
    layoutItems();
}

void Background::setShowsIcons(bool shows) noexcept
{
    if (m_showsIcons == shows)
        return;

    m_showsIcons = shows;
    rebuildItems();
}

/* ------------------------------------------ Grid ------------------------------------------ */

void Background::rebuildItems() noexcept
{
    auto *desk { GetDesk() };

    if (!m_showsIcons)
    {
        m_items.clear();
        GetDesk()->selectionChanged();
        return;
    }

    // Keep existing items (and their selection), create the new ones, drop the gone ones.
    std::vector<std::unique_ptr<DeskItem>> items;
    m_iconSize = desk->settings.iconSize;

    for (const auto &entry : desk->entries())
    {
        auto it { std::find_if(m_items.begin(), m_items.end(), [&](const auto &i){ return i && i->key() == entry.key; }) };

        if (it != m_items.end())
        {
            (*it)->setEntry(entry);

            if ((*it)->iconSize() != m_iconSize)
                (*it)->setIconSize(m_iconSize);

            items.emplace_back(std::move(*it));
        }
        else
            items.emplace_back(std::make_unique<DeskItem>(entry, m_iconSize, &icons));
    }

    m_items = std::move(items);
    layoutItems();
    GetDesk()->selectionChanged(); // Selected items may be gone
}

void Background::updateGrid() noexcept
{
    m_cell = DeskItem::CellSize(m_iconSize);
    const Int32 stepX { m_cell.width() + DESK_GRID_SPACING };
    const Int32 stepY { m_cell.height() + DESK_GRID_SPACING };

    // The icon is centered in its (wider) cell: the visible gap to the right edge is the cell's
    // margin plus that inset. Make it equal to the gap below the top bar.
    const Int32 box { m_iconSize + 2 * DESK_ICON_PADDING };
    const Int32 inset { (m_cell.width() - box) / 2 };
    m_marginRight = std::max(0, DESK_EDGE_GAP - inset);
    m_marginTop = GetDesk()->topbarHeight() + DESK_EDGE_GAP;

    m_cols = std::max(1, (m_size.width() - DESK_MARGIN_LEFT - m_marginRight + DESK_GRID_SPACING) / stepX);
    m_rows = std::max(1, (m_size.height() - m_marginTop - DESK_MARGIN_BOTTOM + DESK_GRID_SPACING) / stepY);
}

SkIPoint Background::cellOrigin(const SkIPoint &cell) const noexcept
{
    // Columns count from the right edge, like Finder.
    return {
        m_size.width() - m_marginRight - m_cell.width() - cell.x() * (m_cell.width() + DESK_GRID_SPACING),
        m_marginTop + cell.y() * (m_cell.height() + DESK_GRID_SPACING) };
}

SkIPoint Background::cellAt(const SkPoint &topLeft) const noexcept
{
    const float col { (m_size.width() - m_marginRight - m_cell.width() - topLeft.x()) / float(m_cell.width() + DESK_GRID_SPACING) };
    const float row { (topLeft.y() - m_marginTop) / float(m_cell.height() + DESK_GRID_SPACING) };
    return { std::clamp(Int32(std::lround(col)), 0, m_cols - 1), std::clamp(Int32(std::lround(row)), 0, m_rows - 1) };
}

SkIRect Background::iconRectAt(const SkIPoint &cell) const noexcept
{
    const SkIPoint origin { cellOrigin(cell) };
    const Int32 box { m_iconSize + 2 * DESK_ICON_PADDING };
    return SkIRect::MakeXYWH(origin.x() + (m_cell.width() - box) / 2, origin.y(), box, box);
}

SkIRect Background::labelBoxAt(const DeskItem &item) const noexcept
{
    const SkIPoint origin { cellOrigin(item.cell) };
    return SkIRect::MakeXYWH(origin.x(), origin.y() + m_iconSize + 2 * DESK_ICON_PADDING + DESK_LABEL_GAP,
                             m_cell.width(), Int32(item.label.layout().calculatedHeight()));
}

SkRect Background::labelRectAt(const DeskItem &item) const noexcept
{
    const SkIPoint origin { cellOrigin(item.cell) };
    SkRect rect { item.label.textBounds() };
    rect.offset(origin.x(), origin.y() + m_iconSize + 2 * DESK_ICON_PADDING + DESK_LABEL_GAP);
    return rect;
}

void Background::layoutItems() noexcept
{
    if (!m_showsIcons || m_size.isEmpty())
        return;

    updateGrid();

    // Arranged orders (Name, Kind, Size) ignore the cells chosen by the user.
    static const std::map<std::string, SkIPoint> none;
    const Settings &settings { GetDesk()->settings };
    const auto &positions { settings.sortBy == Settings::SortBy::Custom ? settings.positions : none };
    std::vector<bool> taken(size_t(m_cols * m_rows), false);
    std::vector<DeskItem*> unplaced;

    const auto take = [&](const SkIPoint &cell) {
        taken[size_t(cell.x() * m_rows + cell.y())] = true;
    };

    const auto isTaken = [&](const SkIPoint &cell) {
        return taken[size_t(cell.x() * m_rows + cell.y())];
    };

    // Cells chosen by the user first (if they still fit the screen and nothing took them yet).
    for (auto &item : m_items)
    {
        auto it { positions.find(item->key()) };

        if (it != positions.end() && it->second.x() < m_cols && it->second.y() < m_rows && !isTaken(it->second))
        {
            item->cell = it->second;
            take(item->cell);
        }
        else
            unplaced.emplace_back(item.get());
    }

    // The rest fill the free cells column by column, from the top-right corner.
    SkIPoint next { 0, 0 };

    for (DeskItem *item : unplaced)
    {
        while (next.x() < m_cols && isTaken(next))
        {
            next.fY++;

            if (next.y() >= m_rows)
                next = { next.x() + 1, 0 };
        }

        // Full screen: pile up on the last cell.
        item->cell = next.x() < m_cols ? next : SkIPoint::Make(m_cols - 1, m_rows - 1);

        if (next.x() < m_cols)
            take(next);
    }

    for (auto &item : m_items)
    {
        const SkIPoint origin { cellOrigin(item->cell) };
        item->layout().setPosition(YGEdgeLeft, origin.x());
        item->layout().setPosition(YGEdgeTop, origin.y());
    }

    // The item being renamed may have moved, or be gone
    if (renameField.active())
    {
        if (DeskItem *renamed { item(renameField.key()) })
            renameField.place(labelBoxAt(*renamed));
        else
            renameField.cancel();
    }
}

DeskItem *Background::itemAt(const SkPoint &pos) const noexcept
{
    for (AKNode *node { scene().nodeAt(pos) }; node; node = node->parent())
        if (auto *item { dynamic_cast<DeskItem*>(node) })
            return item;

    return nullptr;
}

DeskItem *Background::item(const std::string &key) const noexcept
{
    for (auto &item : m_items)
        if (item->key() == key)
            return item.get();

    return nullptr;
}

/* ---------------------------------------- Selection ---------------------------------------- */

std::vector<DeskItem*> Background::selection() const noexcept
{
    std::vector<DeskItem*> out;

    for (auto &item : m_items)
        if (item->selected())
            out.emplace_back(item.get());

    return out;
}

std::vector<const DeskEntry*> Background::selectedEntries() const noexcept
{
    std::vector<const DeskEntry*> out;

    for (DeskItem *item : selection())
        if (const DeskEntry *entry { GetDesk()->entry(item->key()) })
            out.emplace_back(entry);

    return out;
}

void Background::selectOnly(DeskItem *selected) noexcept
{
    for (auto &item : m_items)
        item->setSelected(item.get() == selected);
}

void Background::clearSelection() noexcept
{
    selectOnly(nullptr);
}

void Background::updateRubberBand() noexcept
{
    const SkPoint pos { AKApp::Get()->pointer().pos() };
    const SkRect rect { SkRect::MakeLTRB(
        std::min(pos.x(), m_rubberStart.x()), std::min(pos.y(), m_rubberStart.y()),
        std::max(pos.x(), m_rubberStart.x()), std::max(pos.y(), m_rubberStart.y())) };

    rubberBand.setRect(rect.round());

    for (auto &item : m_items)
    {
        const bool hit { SkRect::Make(iconRectAt(item->cell)).intersects(rect) || labelRectAt(*item).intersects(rect) };
        item->setSelected(hit || m_rubberBase.contains(item->key()));
    }
}

void Background::endRubberBand() noexcept
{
    m_rubberActive = false;
    m_rubberBase.clear();
    rubberBand.setRect(SkIRect::MakeEmpty());
    scene().setPointerGrab(nullptr);
}

void Background::selectAll() noexcept
{
    for (auto &item : m_items)
        item->setSelected(true);
}

void Background::newFolder() noexcept
{
    const std::string key { GetDesk()->newFolder() };

    if (DeskItem *created { item(key) })
        selectOnly(created);
}

void Background::startRename(DeskItem &target) noexcept
{
    m_renameTimer.stop();
    m_renameCandidate.clear();
    selectOnly(&target);
    renameField.begin(target, labelBoxAt(target));
}

void Background::openSelection() noexcept
{
    GetDesk()->open(selectedEntries());
}

void Background::trashSelection() noexcept
{
    GetDesk()->trash(selectedEntries());
}

bool Background::CtrlHeld() noexcept
{
    auto keymap { CZCore::Get()->keymap() };
    return keymap && (keymap->pressedKeys().contains(KEY_LEFTCTRL) || keymap->pressedKeys().contains(KEY_RIGHTCTRL));
}

bool Background::ShiftHeld() noexcept
{
    auto keymap { CZCore::Get()->keymap() };
    return keymap && (keymap->pressedKeys().contains(KEY_LEFTSHIFT) || keymap->pressedKeys().contains(KEY_RIGHTSHIFT));
}

bool Background::CtrlOrShiftHeld() noexcept
{
    return CtrlHeld() || ShiftHeld();
}

/* ------------------------------------------ Input ------------------------------------------ */

void Background::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    MLayerSurface::pointerButtonEvent(e);

    if (e.pressed)
        GetDesk()->setActiveBackground(this);

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    // Clicks in the name being edited are its own
    if (renameField.active() && SkRect::Make(renameField.worldRect()).contains(pos.x(), pos.y()))
        return;

    if (e.pressed)
    {
        m_renameTimer.stop();
        m_renameCandidate.clear();

        // The desktop's keys (also ends a rename: its editor loses the keyboard)
        setKeyboardFocus(true);
    }

    if (e.button == BTN_RIGHT && e.pressed)
    {
        if (!m_showsIcons)
        {
            openDesktopMenu(e);
            return;
        }

        if (DeskItem *item { itemAt(pos) })
        {
            if (!item->selected())
                selectOnly(item);

            openItemsMenu(e);
        }
        else
        {
            clearSelection();
            openDesktopMenu(e);
        }
        return;
    }

    if (e.button != BTN_LEFT || !m_showsIcons)
        return;

    if (!e.pressed)
    {
        if (m_rubberActive)
            endRubberBand();

        if (m_pressItem && m_selectOnRelease)
            selectOnly(m_pressItem);

        // Not dragged: the slow second click renames, unless a double click follows
        if (m_pressItem && !m_pressRenameKey.empty() && m_pressItem->key() == m_pressRenameKey)
        {
            m_renameCandidate = m_pressRenameKey;
            m_renameTimer.start(DESK_DOUBLE_CLICK_MS);
        }

        m_pressRenameKey.clear();
        m_pressItem.reset();
        m_press.reset();
        m_selectOnRelease = false;
        return;
    }

    DeskItem *item { itemAt(pos) };
    const UInt64 now { CZTime::Ms() };

    // A click on the name of the item that's already the only one selected (see the release)
    m_pressRenameKey.clear();

    if (item && item->selected() && selection().size() == 1 && !CtrlOrShiftHeld() &&
        item->labelRect().contains(pos.x(), pos.y()))
        m_pressRenameKey = item->key();

    if (!item)
    {
        m_lastClickMs = 0;

        if (!CtrlOrShiftHeld())
            clearSelection();

        // Rubber band selection (keeping the current selection with Ctrl/Shift).
        m_rubberActive = true;
        m_rubberStart = pos;
        m_rubberBase.clear();

        for (DeskItem *selected : selection())
            m_rubberBase.insert(selected->key());

        scene().setPointerGrab(this);
        return;
    }

    // Double click: open.
    if (m_lastClickKey == item->key() && now - m_lastClickMs <= DESK_DOUBLE_CLICK_MS)
    {
        m_lastClickMs = 0;
        m_pressRenameKey.clear();
        selectOnly(item);
        openSelection();
        return;
    }

    m_lastClickMs = now;
    m_lastClickKey = item->key();
    m_selectOnRelease = false;

    if (CtrlOrShiftHeld())
    {
        item->setSelected(!item->selected());

        if (!item->selected())
            return; // Nothing to drag
    }
    else if (!item->selected())
        selectOnly(item);
    else
        m_selectOnRelease = true; // Maybe a drag of the whole selection

    // Maybe the start of a drag.
    m_pressItem.reset(item);
    m_press = std::static_pointer_cast<CZPointerButtonEvent>(e.copy());
    m_pressPos = pos;
}

void Background::pointerMoveEvent(const CZPointerMoveEvent &e)
{
    MLayerSurface::pointerMoveEvent(e);

    if (m_rubberActive)
    {
        updateRubberBand();
        return;
    }

    if (!m_pressItem || !m_press)
        return;

    if (SkPoint::Distance(AKApp::Get()->pointer().pos(), m_pressPos) < DESK_DRAG_THRESHOLD)
        return;

    m_selectOnRelease = false;
    startDrag(*m_pressItem);
    m_pressItem.reset();
    m_press.reset();
}

void Background::keyboardKeyEvent(const CZKeyboardKeyEvent &e)
{
    MLayerSurface::keyboardKeyEvent(e);

    if (e.isPressed)
        GetDesk()->setActiveBackground(this);

    // Typing a name (see RenameField)
    if (renameField.active())
        return;

    if (!e.isPressed || !m_showsIcons)
        return;

    switch (e.code)
    {
    case KEY_ENTER:
    case KEY_KPENTER:
        openSelection();
        break;
    case KEY_DELETE:
        trashSelection();
        break;
    case KEY_ESC:
        clearSelection();
        break;
    default:
        // The menu bar's shortcuts (⌘ = Ctrl).
        if (CtrlHeld())
            GetDesk()->shortcut(e.code, ShiftHeld());
        break;
    }
}

/* ---------------------------------------- Drag & drop ---------------------------------------- */

void Background::startDrag(DeskItem &grabbed) noexcept
{
    if (m_dnd && m_dnd->dragging())
        return;

    if (!m_dnd)
    {
        m_dnd = std::make_unique<MDND>();
        m_dnd->onFinished.subscribe(this, [this]{ endDrag(); });
        m_dnd->onCancelled.subscribe(this, [this]{ endDrag(); });

        // Other apps (file managers, editors...) get the files as URIs.
        m_dnd->onSend = [this](const std::string &mimeType, Int32 fd){
            if (mimeType != "text/uri-list")
                return;

            std::string uris;

            for (const auto &key : m_dragKeys)
                if (const DeskEntry *entry { GetDesk()->entry(key) })
                {
                    std::string encoded;
                    static constexpr char hex[] { "0123456789ABCDEF" };

                    for (const unsigned char c : entry->path.string())
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

            [[maybe_unused]] const auto written { write(fd, uris.data(), uris.size()) };
        };
    }

    m_dragKeys.clear();

    for (DeskItem *item : selection())
        m_dragKeys.emplace_back(item->key());

    if (m_dragKeys.empty())
        return;

    m_dragGrabKey = grabbed.key();

    // Like Finder: the icon and its highlighted name (not the gray box behind the icon), under the
    // pointer exactly where the item was grabbed.
    const SkIPoint origin { cellOrigin(grabbed.cell) };
    m_dragOffset = { m_pressPos.x() - origin.x(), m_pressPos.y() - origin.y() };

    m_dndItem.reset();
    m_dndStack.reset();
    m_gather.pause();
    m_flyers.clear();

    if (m_dragKeys.size() == 1)
    {
        m_dndItem = std::make_unique<DeskItem>(grabbed.entry(), m_iconSize, m_dnd.get());
        m_dndItem->layout().setPosition(YGEdgeLeft, 0.f);
        m_dndItem->layout().setPosition(YGEdgeTop, 0.f);
        m_dndItem->label.setSelected(true); // Only the label: setSelected() would also show the gray box
        m_dndItem->icon.setOpacity(0.85f);
        m_dndItem->label.setOpacity(0.85f);

        m_dnd->layout().setWidth(m_cell.width());
        m_dnd->layout().setHeight(m_cell.height());
        m_dnd->setHotspot(SkIPoint::Make(
            std::clamp(Int32(m_dragOffset.x()), 0, m_cell.width()),
            std::clamp(Int32(m_dragOffset.y()), 0, m_cell.height())));
    }
    else
    {
        // Several: the drag starts with the grabbed icon alone while copies of the others fly to the
        // pointer (gatherStep()); then it becomes the whole stack with a count (gatherFinished()).
        const SkIRect iconRect { iconRectAt(grabbed.cell) };
        m_grabInIcon = {
            std::clamp(Int32(m_pressPos.x()) - iconRect.x() - DESK_ICON_PADDING, 0, m_iconSize),
            std::clamp(Int32(m_pressPos.y()) - iconRect.y() - DESK_ICON_PADDING, 0, m_iconSize) };

        m_dragImages.clear();

        for (const auto &key : m_dragKeys)
        {
            DeskItem *i { item(key) };

            if (!i || i == &grabbed)
                continue;

            m_dragImages.emplace_back(i->icon.image());

            const SkIRect rect { iconRectAt(i->cell) };
            // Its layer once stacked: the others are back to front in m_dragImages order, the
            // grabbed one (not flying, it's the drag icon) in front.
            const Int32 depth { DragStack::LayerDepth(m_dragImages.size() - 1, m_dragKeys.size()) };
            auto flyer { std::make_unique<Flyer>(this, SkPoint::Make(rect.x() + DESK_ICON_PADDING, rect.y() + DESK_ICON_PADDING), depth) };
            flyer->view.setImage(i->icon.image());
            flyer->view.layout().setWidth(m_iconSize);
            flyer->view.layout().setHeight(m_iconSize);
            flyer->box.layout().setPositionType(YGPositionTypeAbsolute);
            flyer->box.layout().setWidth(m_iconSize);
            flyer->box.layout().setHeight(m_iconSize);
            flyer->box.layout().setPosition(YGEdgeLeft, flyer->from.x());
            flyer->box.layout().setPosition(YGEdgeTop, flyer->from.y());
            m_flyers.emplace_back(std::move(flyer));
        }

        m_dragImages.emplace_back(grabbed.icon.image()); // In front
        m_dndStack = std::make_unique<DragStack>(m_iconSize, m_dnd.get());
        setDragImage({ grabbed.icon.image() }, 1);
        m_dndPointer = m_pressPos;
    }

    if (!m_dnd->start(*this, *m_press, { DESK_ITEMS_MIME_TYPE, "text/uri-list" },
                      CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move)))
    {
        m_dragKeys.clear();
        return;
    }

    for (const auto &key : m_dragKeys)
        if (DeskItem *i { item(key) })
            i->setDimmed(true);

    GetDesk()->setDragSource(this);

    if (!m_flyers.empty())
        m_gather.reset();
}

void Background::setDragImage(const std::vector<std::shared_ptr<RImage>> &images, UInt32 count) noexcept
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

void Background::gatherStep(Float64 progress) noexcept
{
    // Each one towards its layer of the stack that will be dragged: the front icon is under the
    // pointer where it was grabbed, the ones behind move right and up by a step per layer.
    const SkPoint front { m_dndPointer.x() - m_grabInIcon.x(), m_dndPointer.y() - m_grabInIcon.y() };
    const Int32 step { DragStack::LayerStep(m_iconSize) };
    const SkScalar t { SkScalar(CZEase::OutCubic(progress)) };

    for (auto &flyer : m_flyers)
    {
        const SkPoint target { front.x() + flyer->depth * step, front.y() - flyer->depth * step };
        flyer->box.layout().setPosition(YGEdgeLeft, flyer->from.x() + (target.x() - flyer->from.x()) * t);
        flyer->box.layout().setPosition(YGEdgeTop, flyer->from.y() + (target.y() - flyer->from.y()) * t);
    }
}

void Background::gatherFinished() noexcept
{
    m_flyers.clear();

    if (m_dnd && m_dnd->dragging() && m_dndStack)
        setDragImage(m_dragImages, UInt32(m_dragKeys.size()));
}

void Background::dragOver(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    Background *source { GetDesk()->dragSource() };

    if (source == this)
        m_dndPointer = pos; // The flying icons follow it (see gatherStep())

    // Over a folder or disk: files go into it (Desk's own icons or any file, e.g. from a file
    // manager). Moved by default, copied to other disks.
    DeskItem *target { dropTargetAt(offer, pos) };
    setDropTarget(target);

    if (target)
    {
        const bool own { source && !source->m_dragKeys.empty() && offer.hasMimeType(DESK_ITEMS_MIME_TYPE) };
        offer.accept(own ? DESK_ITEMS_MIME_TYPE : "text/uri-list");
        offer.setActions(CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move),
                         target->entry().kind == DeskEntry::Kind::Volume ? MDNDOffer::Copy : MDNDOffer::Move);
        return;
    }

    // Files from other apps: into the desktop folder.
    if (m_showsIcons && HasForeignFiles(offer))
    {
        offer.accept("text/uri-list");
        offer.setActions(CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move), MDNDOffer::Move);
        return;
    }

    // Otherwise only Desk's own icons can be dropped here (moving them, from this screen or another one).
    if (!m_showsIcons || !source || source->m_dragKeys.empty() || !offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
    {
        offer.accept("");
        return;
    }

    offer.accept(DESK_ITEMS_MIME_TYPE);
    offer.setActions(MDNDOffer::Move, MDNDOffer::Move);
}

void Background::dropped(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    Background *source { GetDesk()->dragSource() };

    if (DeskItem *target { dropTargetAt(offer, pos) })
    {
        setDropTarget(nullptr);
        droppedInto(target->entry().path, target->entry().kind == DeskEntry::Kind::Volume, offer);
        return;
    }

    setDropTarget(nullptr);

    // Files from other apps: into the desktop folder, placed where dropped (custom order): the first
    // in the cell under the pointer, the rest below it (or wherever there's room).
    if (m_showsIcons && HasForeignFiles(offer))
    {
        auto *desk { GetDesk() };
        const auto files { droppedInto(desk->desktopDir(), false, offer) };

        if (desk->settings.sortBy == Settings::SortBy::Custom && !files.empty())
        {
            const SkIPoint first { cellAt({ pos.x() - m_cell.width() / 2.f, pos.y() - m_cell.height() / 2.f }) };
            std::error_code ec;
            Int32 row { first.y() };

            for (const auto &file : files)
            {
                const std::string name { file.filename().string() };

                // Not one already there (it won't be replaced).
                if (name.empty() || std::filesystem::exists(desk->desktopDir() / name, ec) || row >= m_rows)
                    continue;

                const SkIPoint cell { std::clamp(first.x(), 0, m_cols - 1), row++ };
                desk->setPosition(name, &cell);
            }

            desk->saveSettings();
        }

        return;
    }

    if (!m_showsIcons || !source || source->m_dragKeys.empty() || !offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
        return;

    // Every screen shows the same icons: the dragged ones are here too, in this screen's cells.
    const std::vector<std::string> dragKeys { source->m_dragKeys };
    const SkPoint dragOffset { source->m_dragOffset };
    DeskItem *grabbed { item(source->m_dragGrabKey) };

    if (grabbed)
    {
        // Moving an icon while arranged switches to Custom, starting from the current layout.
        if (GetDesk()->settings.sortBy != Settings::SortBy::Custom)
        {
            for (auto &i : m_items)
                GetDesk()->setPosition(i->key(), &i->cell);

            GetDesk()->settings.sortBy = Settings::SortBy::Custom;
            GetDesk()->selectionChanged(); // Refresh the menus' checks
        }

        // Move every dragged item by the same number of cells as the grabbed one.
        const SkIPoint target { cellAt({ pos.x() - dragOffset.x(), pos.y() - dragOffset.y() }) };
        const SkIPoint delta { target.x() - grabbed->cell.x(), target.y() - grabbed->cell.y() };
        auto *desk { GetDesk() };
        std::vector<SkIPoint> newCells;

        for (const auto &key : dragKeys)
            if (DeskItem *i { item(key) })
            {
                const SkIPoint cell { std::clamp(i->cell.x() + delta.x(), 0, m_cols - 1),
                                      std::clamp(i->cell.y() + delta.y(), 0, m_rows - 1) };
                desk->setPosition(key, &cell);
                newCells.emplace_back(cell);
            }

        // Items in the way flow to the next free cells.
        for (auto &i : m_items)
            if (std::find(dragKeys.begin(), dragKeys.end(), i->key()) == dragKeys.end() &&
                std::find(newCells.begin(), newCells.end(), i->cell) != newCells.end())
                desk->setPosition(i->key(), nullptr);

        desk->saveSettings();
        desk->onIconsChanged.notify(); // Positions are shared: every screen re-lays out
    }

    // Complete the session (the data itself isn't needed).
    if (const Int32 fd { offer.receive(DESK_ITEMS_MIME_TYPE) }; fd >= 0)
        close(fd);

    offer.finish();
}

DeskItem *Background::dropTargetAt(MDNDOffer &offer, const SkPoint &pos) const noexcept
{
    if (!m_showsIcons)
        return nullptr;

    DeskItem *target { itemAt(pos) };

    if (!target || !target->acceptsDrops())
        return nullptr;

    Background *source { GetDesk()->dragSource() };

    // Desk's own icons: not into one of the dragged folders.
    if (source && !source->m_dragKeys.empty() && offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
    {
        const auto &keys { source->m_dragKeys };
        return std::find(keys.begin(), keys.end(), target->key()) == keys.end() ? target : nullptr;
    }

    return HasForeignFiles(offer) ? target : nullptr;
}

bool Background::HasForeignFiles(MDNDOffer &offer) noexcept
{
    // Desk's own icons are handled as such; the dock's apps are launchers, not files to move.
    Background *source { GetDesk()->dragSource() };

    if (source && !source->m_dragKeys.empty() && offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
        return false;

    return offer.hasMimeType("text/uri-list") && !offer.hasMimeType("application/x-cz-shelf-app");
}

void Background::setDropTarget(DeskItem *target) noexcept
{
    const std::string key { target ? target->key() : std::string() };

    if (key == m_dropTargetKey)
        return;

    if (DeskItem *prev { item(m_dropTargetKey) })
        prev->setDropTarget(false);

    m_dropTargetKey = key;

    if (target)
        target->setDropTarget(true);
}

// Decodes a file:// URI into a path ("" if it's not a local file).
static std::filesystem::path PathFromURI(std::string uri) noexcept
{
    while (!uri.empty() && (uri.back() == '\r' || uri.back() == ' '))
        uri.pop_back();

    std::string_view rest { uri };

    if (rest.starts_with("file://"))
        rest.remove_prefix(7);
    else
        return {};

    // file://host/path: only local ones.
    if (!rest.starts_with('/'))
    {
        const size_t slash { rest.find('/') };

        if (slash == std::string_view::npos || rest.substr(0, slash) != "localhost")
            return {};

        rest.remove_prefix(slash);
    }

    std::string path;

    for (size_t i = 0; i < rest.size(); i++)
    {
        if (rest[i] == '%' && i + 2 < rest.size())
        {
            const auto hex = [](char c) -> int {
                return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            };
            const int hi { hex(rest[i + 1]) }, lo { hex(rest[i + 2]) };

            if (hi >= 0 && lo >= 0)
            {
                path += char(hi * 16 + lo);
                i += 2;
                continue;
            }
        }

        path += rest[i];
    }

    return path;
}

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

std::vector<std::filesystem::path> Background::droppedInto(const std::filesystem::path &dir, bool copyByDefault, MDNDOffer &offer) noexcept
{
    Background *source { GetDesk()->dragSource() };
    std::vector<std::filesystem::path> files;

    if (source && !source->m_dragKeys.empty() && offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
    {
        // Desk's own icons: the files are known (and Desk itself is the source, so reading its data
        // here would wait for ourselves). Disks can't be moved into folders.
        for (const auto &key : source->m_dragKeys)
            if (const DeskEntry *e { GetDesk()->entry(key) }; e && e->kind == DeskEntry::Kind::File)
                files.emplace_back(e->path);

        if (const Int32 fd { offer.receive(DESK_ITEMS_MIME_TYPE) }; fd >= 0)
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

            const std::string line { list.substr(start, end - start) };
            start = end + 1;

            if (line.empty() || line.front() == '#')
                continue;

            if (auto path { PathFromURI(line) }; !path.empty())
                files.emplace_back(std::move(path));
        }
    }

    // The action chosen by the compositor (e.g. a modifier held), else the target's default.
    const bool copy { offer.action() == MDNDOffer::Copy || (offer.action() != MDNDOffer::Move && copyByDefault) };
    offer.finish();

    GetDesk()->transfer(files, dir, copy);
    return files;
}

void Background::endDrag() noexcept
{
    setDropTarget(nullptr);

    if (GetDesk()->dragSource() == this)
        GetDesk()->setDragSource(nullptr);

    m_gather.pause();
    m_flyers.clear();

    for (const auto &key : m_dragKeys)
        if (DeskItem *i { item(key) })
            i->setDimmed(false);

    m_dragKeys.clear();
    m_dragGrabKey.clear();
}

/* ------------------------------------------ Menus ------------------------------------------ */

void Background::openItemsMenu(const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };
    const auto entries { selectedEntries() };

    if (entries.empty())
        return;

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [this, entries](ContextMenu &menu){
        auto *desk { GetDesk() };
        menu.addAction("Open", [this]{ openSelection(); });

        const bool single { entries.size() == 1 };
        const DeskEntry *first { entries.front() };

        // Other apps that can open it (the default one is "Open").
        if (single && first->kind == DeskEntry::Kind::File && !first->launcher)
        {
            auto apps { desk->appsFor(*first) };
            size_t shown { 0 };

            for (size_t i = 1; i < apps.size() && shown < 4; i++)
            {
                if (apps[i]->noDisplay)
                    continue;

                menu.addAction("Open With " + apps[i]->name, [key = first->key, app = apps[i]]{
                    if (const DeskEntry *e { GetDesk()->entry(key) })
                        GetDesk()->openWith(*e, *app);
                });
                shown++;
            }
        }

        if (single && first->kind == DeskEntry::Kind::File &&
            (Assets::HasThumbnail(first->mimeType) || first->mimeType == "image/svg+xml"))
        {
            menu.addDivider();
            menu.addAction("Set Desktop Picture", [path = first->path]{ GetDesk()->setPicture(path); });
        }

        menu.addDivider();

        const bool hasFiles { std::any_of(entries.begin(), entries.end(), [](const DeskEntry *e){ return e->kind == DeskEntry::Kind::File; }) };

        if (single && first->kind == DeskEntry::Kind::File && !first->launcher)
            menu.addAction("Rename", [this, key = first->key]{
                if (DeskItem *target { item(key) })
                    startRename(*target);
            });

        if (hasFiles)
            menu.addAction("Move to Trash", [this]{ trashSelection(); });

        if (single && first->kind == DeskEntry::Kind::Volume && !first->internal)
            menu.addAction("Eject “" + first->name + "”", [key = first->key]{
                if (const DeskEntry *e { GetDesk()->entry(key) })
                    GetDesk()->eject(*e);
            });
    });
}

void Background::openDesktopMenu(const CZPointerButtonEvent &event) noexcept
{
    if (!m_menu)
        m_menu = std::make_unique<ContextMenu>();

    const SkPoint pos { AKApp::Get()->pointer().pos() };

    m_menu->open(*this, SkIPoint::Make(Int32(pos.x()), Int32(pos.y())), event, [this](ContextMenu &menu){
        auto *desk { GetDesk() };
        const Settings &s { desk->settings };

        if (m_showsIcons)
        {
            menu.addAction("New Folder", [this]{ newFolder(); });
            menu.addDivider();
        }

        menu.addAction("Show Desktop Picture", [show = !s.usePicture]{ GetDesk()->setUsePicture(show); },
            !s.picture.empty()).setChecked(s.showsPicture());

        if (s.showsPicture())
        {
            const auto fitAction = [&menu, &s](const char *text, Settings::Fit fit) {
                menu.addAction(text, [fit]{ GetDesk()->setFit(fit); }).setChecked(s.fit == fit);
            };

            fitAction("Fill Screen", Settings::Fit::Cover);
            fitAction("Fit to Screen", Settings::Fit::Contain);
            fitAction("Stretch to Fill Screen", Settings::Fit::Fill);
        }

        if (m_showsIcons)
        {
            menu.addDivider();

            const auto sortAction = [&menu, &s](const char *text, Settings::SortBy sortBy) {
                menu.addAction(text, [sortBy]{ GetDesk()->setSortBy(sortBy); }).setChecked(s.sortBy == sortBy);
            };

            sortAction("Sort by Name", Settings::SortBy::Name);
            sortAction("Sort by Kind", Settings::SortBy::Kind);
            sortAction("Sort by Size", Settings::SortBy::Size);
            sortAction("Custom Order", Settings::SortBy::Custom);
            menu.addDivider();
            menu.addAction("Larger Icons", []{ GetDesk()->setIconSize(GetDesk()->largerIconSize()); },
                           s.iconSize < DESK_ICON_SIZE_MAX);
            menu.addAction("Smaller Icons", []{ GetDesk()->setIconSize(GetDesk()->smallerIconSize()); },
                           s.iconSize > DESK_ICON_SIZE_MIN);
            menu.addDivider();
            menu.addAction("Clean Up", []{ GetDesk()->clearPositions(); },
                           s.sortBy == Settings::SortBy::Custom && !s.positions.empty());
            menu.addAction("Open Desktop Folder", []{ GetDesk()->openFolder(GetDesk()->desktopDir()); });
        }
    });
}
