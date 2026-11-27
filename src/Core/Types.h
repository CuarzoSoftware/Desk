#ifndef TYPES_H
#define TYPES_H

#include <CZ/Core/Cuarzo.h>
#include <CZ/Core/CZAdaptive.h>
#include <CZ/skia/core/SkColor.h>

using namespace CZ;

/*
 * Desktop metrics, colors and timings, modeled after the macOS Finder desktop.
 * All sizes are in logical units (points).
 */

// --- Background ------------------------------------------------------------------------------

constexpr SkColor DESK_DEFAULT_COLOR    { 0xFF1F4E79 }; // Solid color used when there's no picture

// --- Icon grid -------------------------------------------------------------------------------

// Size changes closer than this (e.g. the screen being resized interactively): the current picture
// is drawn stretched to the new size, and decoded at it once they stop for this long
constexpr UInt32 DESK_PICTURE_RESIZE_DEBOUNCE_MS { 250 };

constexpr Int32 DESK_ICON_SIZE_DEFAULT  { 64 };  // Finder's default desktop icon size
constexpr Int32 DESK_ICON_SIZE_MIN      { 16 };
constexpr Int32 DESK_ICON_SIZE_MAX      { 128 };
// View > Icon Size / Larger / Smaller Icons
constexpr Int32 DESK_ICON_SIZES[]       { 16, 24, 32, 40, 48, 56, 64, 72, 80, 96, 112, 128 };
constexpr Int32 DESK_ICON_PADDING       { 4 };   // Around the icon, inside its selection highlight
constexpr Int32 DESK_LABEL_FONT_SIZE    { 13 };
constexpr Int32 DESK_LABEL_LINES        { 2 };
constexpr Int32 DESK_LABEL_PADDING_X    { 4 };   // Inside the label's selection highlight
constexpr Int32 DESK_LABEL_GAP          { 3 };   // Between the icon and its label
constexpr Int32 DESK_GRID_SPACING       { 16 };  // Extra space between cells (both axes)
constexpr Int32 DESK_EDGE_GAP           { 24 };  // Visible gap between the icons and the top bar / right edge

// Firmament's top bar (Desk covers the whole screen, under it): 24 pt times the size chosen in its
// size menu (read from Firmament's settings, see Desk::topbarHeight()).
constexpr Int32 DESK_TOPBAR_HEIGHT      { 24 };
constexpr Int32 DESK_MARGIN_BOTTOM      { 104 };  // Clear of the dock
constexpr Int32 DESK_MARGIN_LEFT        { 32 };

// --- Colors ----------------------------------------------------------------------------------

constexpr SkColor DESK_ICON_SELECTION   { 0x47000000 }; // Rounded square behind a selected icon
constexpr SkColor DESK_LABEL_SELECTION  { 0xFF0A64D6 }; // Behind a selected item's label
constexpr SkColor DESK_ICON_SHADOW_COLOR { 0x66000000 }; // Soft shadow following each icon's shape
constexpr SkScalar DESK_ICON_SHADOW_RADIUS { 15.f };   // At DESK_ICON_SIZE_DEFAULT (scales with the icon)
constexpr Int32   DESK_ICON_SHADOW_OFFSET_Y { 2 };
constexpr SkColor DESK_LABEL_COLOR      { 0xFFFFFFFF };
constexpr SkColor DESK_LABEL_SHADOW     { 0xCC000000 };
constexpr SkColor DESK_DRAG_BADGE_COLOR { 0xFFFF3B30 }; // Count badge while dragging several items
constexpr size_t  DESK_DRAG_STACK_MAX   { 5 };          // Icons shown stacked while dragging several
constexpr UInt32  DESK_DRAG_GATHER_MS   { 300 };        // Icons flying to the pointer
constexpr SkColor DESK_RUBBER_FILL      { 0x33FFFFFF }; // Rubber band selection
constexpr SkColor DESK_RUBBER_BORDER    { 0x99FFFFFF };
constexpr CZAdaptiveColor DESK_WINDOW_RUBBER_FILL   { 0x26000000, 0x29FFFFFF }; // In folder windows (over their background)
constexpr CZAdaptiveColor DESK_WINDOW_RUBBER_BORDER { 0x8C000000, 0x8CFFFFFF };

// --- Folder windows ------------------------------------------------------------------------

constexpr Int32 DESK_WINDOW_WIDTH       { 920 };
constexpr Int32 DESK_WINDOW_HEIGHT      { 560 };
constexpr Int32 DESK_WINDOW_SIDEBAR_WIDTH { 200 };
constexpr Int32 DESK_WINDOW_MIN_CONTENT_WIDTH { 320 }; // Of the files area (next to the sidebar)
constexpr Int32 DESK_WINDOW_MIN_HEIGHT  { 280 };
constexpr Int32 DESK_WINDOW_ICON_SIZE   { 64 };  // Finder's default icon view size
constexpr Int32 DESK_WINDOW_CELL_WIDTH  { 104 };
constexpr Int32 DESK_WINDOW_GRID_PADDING { 14 }; // Around the grid
constexpr Int32 DESK_WINDOW_GRID_GAP    { 6 };   // Between rows
constexpr Int32 DESK_WINDOW_LABEL_FONT_SIZE { 13 };
// List view (see FolderListRow): the columns after the name, and the row content
constexpr Int32 DESK_LIST_DATE_WIDTH    { 180 };
constexpr Int32 DESK_LIST_SIZE_WIDTH    { 84 };
constexpr Int32 DESK_LIST_KIND_WIDTH    { 150 };
constexpr Int32 DESK_LIST_COLUMN_MIN    { 50 };  // Narrowest a column can be resized to (see FolderListHeader)
constexpr Int32 DESK_LIST_NAME_WIDTH    { 280 }; // Width of the name column (its icon included) until the window's is known
constexpr Int32 DESK_LIST_NAME_MIN      { 120 }; // Narrowest the name column can be resized to
constexpr Int32 DESK_LIST_GAP           { 6 };   // Between columns
constexpr Int32 DESK_LIST_TEXT_INSET    { 8 };   // Text of the columns after the name, from their left edge (after the header's divider)
constexpr Int32 DESK_LIST_ICON_SIZE     { 16 };
constexpr Int32 DESK_LIST_PADDING       { 10 };  // Inside a row, left and right
constexpr Int32 DESK_LIST_ROW_HEIGHT    { 26 };
constexpr Int32 DESK_LIST_HEADER_HEIGHT { 26 };
constexpr Int32 DESK_LIST_FONT_SIZE     { 14 };
constexpr Int32 DESK_WINDOW_LIST_INSET  { 8 };   // The list's content padding (rows inset from the edges)

// Icons in windows: a faint shadow (over a plain background, not a picture), at DESK_WINDOW_ICON_SIZE
constexpr SkColor  DESK_WINDOW_ICON_SHADOW_COLOR    { 0x40000000 };
constexpr SkScalar DESK_WINDOW_ICON_SHADOW_RADIUS   { 6.f };
constexpr SkScalar DESK_WINDOW_ICON_SHADOW_OFFSET_Y { 1.5f };
constexpr CZAdaptiveColor DESK_WINDOW_ICON_SELECTION { 0x1F000000, 0x29FFFFFF }; // Rounded square behind a selected icon

// --- Behaviour -------------------------------------------------------------------------------

constexpr UInt32  DESK_DOUBLE_CLICK_MS  { 400 };
constexpr UInt32  DESK_OPEN_ZOOM_MS     { 200 };   // An opened item's icon growing in place, fading out
constexpr float   DESK_OPEN_ZOOM_SCALE  { 4.5f };  // How much larger it gets
constexpr Float32 DESK_DRAG_THRESHOLD   { 4.f };
constexpr Int32   DESK_LIST_DRAG_HOTSPOT { 6 };     // Dragged from a list: the pointer this far into the drag icon's top-left corner
constexpr Float32 DESK_AUTOSCROLL_EDGE  { 16.f };   // A rubber band near a window's view edge (or past it) scrolls it
constexpr Float32 DESK_AUTOSCROLL_SPEED { 10.f };   // Points per second for each point into that zone
constexpr Float32 DESK_AUTOSCROLL_MAX   { 2400.f }; // Points per second, at most
constexpr UInt32  DESK_RESCAN_DELAY_MS  { 150 };  // Coalesces bursts of file system events
constexpr UInt32  DESK_STALE_VIEW_MS    { 300 };  // Longest a folder window keeps showing the previous folder while reading the new one
constexpr Int32   DESK_THUMBNAIL_MAX_MB { 64 };   // Bigger pictures get the generic image icon
constexpr Int32   DESK_ICON_LOAD_SIZE   { 256 };  // Themed icons are loaded at this size
#define DESK_ICON_THEME "WhiteSur-light"
#define DESK_ITEMS_MIME_TYPE "application/x-cz-desk-items"
#define DESK_FILES_MIME_TYPE "application/x-cz-desk-files" // Dragged from a folder window (see Desk::dragFiles())
#define DESK_FAVORITE_MIME_TYPE "application/x-cz-desk-favorite" // A sidebar favorite being reordered (see FolderSidebar)

/// Widths of the list view's columns (the name's includes its icon), see FolderListHeader.
struct DeskListColumns
{
    Int32 name { DESK_LIST_NAME_WIDTH };
    Int32 date { DESK_LIST_DATE_WIDTH };
    Int32 size { DESK_LIST_SIZE_WIDTH };
    Int32 kind { DESK_LIST_KIND_WIDTH };

    bool operator==(const DeskListColumns &) const = default;
};

#endif // TYPES_H
