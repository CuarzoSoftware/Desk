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
constexpr SkScalar DESK_ICON_SHADOW_RADIUS { 15.f };
constexpr Int32   DESK_ICON_SHADOW_OFFSET_Y { 2 };
constexpr SkColor DESK_LABEL_COLOR      { 0xFFFFFFFF };
constexpr SkColor DESK_LABEL_SHADOW     { 0xCC000000 };
constexpr SkColor DESK_DRAG_BADGE_COLOR { 0xFFFF3B30 }; // Count badge while dragging several items
constexpr size_t  DESK_DRAG_STACK_MAX   { 5 };          // Icons shown stacked while dragging several
constexpr UInt32  DESK_DRAG_GATHER_MS   { 300 };        // Icons flying to the pointer
constexpr SkColor DESK_RUBBER_FILL      { 0x33FFFFFF }; // Rubber band selection
constexpr SkColor DESK_RUBBER_BORDER    { 0x99FFFFFF };

// --- Behaviour -------------------------------------------------------------------------------

constexpr UInt32  DESK_DOUBLE_CLICK_MS  { 400 };
constexpr Float32 DESK_DRAG_THRESHOLD   { 4.f };
constexpr UInt32  DESK_RESCAN_DELAY_MS  { 150 };  // Coalesces bursts of file system events
constexpr Int32   DESK_THUMBNAIL_MAX_MB { 64 };   // Bigger pictures get the generic image icon
constexpr Int32   DESK_ICON_LOAD_SIZE   { 256 };  // Themed icons are loaded at this size
#define DESK_ICON_THEME "WhiteSur-light"
#define DESK_ITEMS_MIME_TYPE "application/x-cz-desk-items"

#endif // TYPES_H
