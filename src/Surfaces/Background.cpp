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
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZPointerMoveEvent.h>
#include <CZ/Core/Events/CZKeyboardKeyEvent.h>
#include <CZ/Ream/RImage.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace fs = std::filesystem;

Background::Background(MScreen &screen) noexcept : MLayerSurface()
{
    screen.userData = this;
    setScreen(&screen);
    setScope("cz-desk");
    setLayer(Layer::Background);
    setAnchor(CZEdgeLeft | CZEdgeTop | CZEdgeRight | CZEdgeBottom);
    setExclusiveZone(-1); // The whole screen, under the top bar and the dock
    setKeyboardInteractivity(KeyboardInteractivity::OnDemand);
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
    m_cols = std::max(1, (m_size.width() - DESK_MARGIN_LEFT - DESK_MARGIN_RIGHT + DESK_GRID_SPACING) / stepX);
    m_rows = std::max(1, (m_size.height() - DESK_MARGIN_TOP - DESK_MARGIN_BOTTOM + DESK_GRID_SPACING) / stepY);
}

SkIPoint Background::cellOrigin(const SkIPoint &cell) const noexcept
{
    // Columns count from the right edge, like Finder.
    return {
        m_size.width() - DESK_MARGIN_RIGHT - m_cell.width() - cell.x() * (m_cell.width() + DESK_GRID_SPACING),
        DESK_MARGIN_TOP + cell.y() * (m_cell.height() + DESK_GRID_SPACING) };
}

SkIPoint Background::cellAt(const SkPoint &topLeft) const noexcept
{
    const float col { (m_size.width() - DESK_MARGIN_RIGHT - m_cell.width() - topLeft.x()) / float(m_cell.width() + DESK_GRID_SPACING) };
    const float row { (topLeft.y() - DESK_MARGIN_TOP) / float(m_cell.height() + DESK_GRID_SPACING) };
    return { std::clamp(Int32(std::lround(col)), 0, m_cols - 1), std::clamp(Int32(std::lround(row)), 0, m_rows - 1) };
}

SkIRect Background::iconRectAt(const SkIPoint &cell) const noexcept
{
    const SkIPoint origin { cellOrigin(cell) };
    const Int32 box { m_iconSize + 2 * DESK_ICON_PADDING };
    return SkIRect::MakeXYWH(origin.x() + (m_cell.width() - box) / 2, origin.y(), box, box);
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

    const auto &positions { GetDesk()->settings.positions };
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

    const SkPoint pos { AKApp::Get()->pointer().pos() };

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

        m_pressItem.reset();
        m_press.reset();
        m_selectOnRelease = false;
        return;
    }

    DeskItem *item { itemAt(pos) };
    const UInt64 now { CZTime::Ms() };

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
        m_dndIcon = std::make_unique<AKImageFrame>(m_dnd.get());
        m_dndIcon->setSizeMode(AKImageFrame::SizeMode::Contain);
        m_dndIcon->layout().setWidthPercent(100.f);
        m_dndIcon->layout().setHeightPercent(100.f);
        m_dndIcon->setOpacity(0.8f);
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

    // The icon under the pointer exactly where it was grabbed.
    const SkIRect iconRect { iconRectAt(grabbed.cell) };
    const SkIPoint origin { cellOrigin(grabbed.cell) };
    m_dragOffset = { m_pressPos.x() - origin.x(), m_pressPos.y() - origin.y() };

    m_dndIcon->setImage(grabbed.icon.image());
    m_dnd->layout().setWidth(iconRect.width());
    m_dnd->layout().setHeight(iconRect.height());
    m_dnd->setHotspot(SkIPoint::Make(
        std::clamp(Int32(m_pressPos.x()) - iconRect.x(), 0, iconRect.width()),
        std::clamp(Int32(m_pressPos.y()) - iconRect.y(), 0, iconRect.height())));

    if (!m_dnd->start(*this, *m_press, { DESK_ITEMS_MIME_TYPE, "text/uri-list" },
                      CZBitset<MDNDOffer::Action>(MDNDOffer::Copy | MDNDOffer::Move)))
    {
        m_dragKeys.clear();
        return;
    }

    for (const auto &key : m_dragKeys)
        if (DeskItem *i { item(key) })
            i->setDimmed(true);
}

void Background::dragOver(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    CZ_UNUSED(pos)

    // Only our own icons can be dropped here (moving them).
    if (!m_showsIcons || m_dragKeys.empty() || !offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
    {
        offer.accept("");
        return;
    }

    offer.accept(DESK_ITEMS_MIME_TYPE);
    offer.setActions(MDNDOffer::Move, MDNDOffer::Move);
}

void Background::dropped(MDNDOffer &offer, const SkPoint &pos) noexcept
{
    if (!m_showsIcons || m_dragKeys.empty() || !offer.hasMimeType(DESK_ITEMS_MIME_TYPE))
        return;

    DeskItem *grabbed { item(m_dragGrabKey) };

    if (grabbed)
    {
        // Move every dragged item by the same number of cells as the grabbed one.
        const SkIPoint target { cellAt({ pos.x() - m_dragOffset.x(), pos.y() - m_dragOffset.y() }) };
        const SkIPoint delta { target.x() - grabbed->cell.x(), target.y() - grabbed->cell.y() };
        auto *desk { GetDesk() };
        std::vector<SkIPoint> newCells;

        for (const auto &key : m_dragKeys)
            if (DeskItem *i { item(key) })
            {
                const SkIPoint cell { std::clamp(i->cell.x() + delta.x(), 0, m_cols - 1),
                                      std::clamp(i->cell.y() + delta.y(), 0, m_rows - 1) };
                desk->setPosition(key, &cell);
                newCells.emplace_back(cell);
            }

        // Items in the way flow to the next free cells.
        for (auto &i : m_items)
            if (std::find(m_dragKeys.begin(), m_dragKeys.end(), i->key()) == m_dragKeys.end() &&
                std::find(newCells.begin(), newCells.end(), i->cell) != newCells.end())
                desk->setPosition(i->key(), nullptr);

        desk->saveSettings();
        layoutItems();
    }

    // Complete the session (the data itself isn't needed).
    if (const Int32 fd { offer.receive(DESK_ITEMS_MIME_TYPE) }; fd >= 0)
        close(fd);

    offer.finish();
}

void Background::endDrag() noexcept
{
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
            menu.addAction("Clean Up", []{ GetDesk()->clearPositions(); }, !s.positions.empty());
            menu.addAction("Open Desktop Folder", []{ GetDesk()->openFolder(GetDesk()->desktopDir()); });
        }
    });
}
