#ifndef SETTINGS_H
#define SETTINGS_H

#include <set>
#include <Core/Types.h>
#include <CZ/skia/core/SkPoint.h>
#include <filesystem>
#include <map>
#include <vector>
#include <optional>
#include <string>

/**
 * @brief Persistent Desk settings, stored in `$XDG_CONFIG_HOME/cz-desk/desk.conf`
 *        (`~/.config/cz-desk/desk.conf` by default).
 *
 * The file is watched: editing it by hand applies the changes right away.
 *
 * @code
 * [Background]
 * Picture=/home/user/.config/cz-desk/wallpaper.jpg
 * UsePicture=true
 * Fit=Cover
 * Color=#1F4E79
 *
 * [Icons]
 * Size=64
 * SortBy=Custom
 *
 * [Windows]
 * View=Grid
 * NameOffset=-420
 * DateWidth=180
 * SizeWidth=84
 * KindWidth=150
 *
 * [Favorites]
 * 0=/home/user
 * 1=/home/user/Projects
 *
 * [Positions]
 * Notes.txt=0,2
 * @endcode
 *
 * `Picture` is a copy of the chosen one (see StorePicture()). `Fit` is `Cover` (fills the screen, cropping), `Contain` (whole picture, the color around it) or
 * `Fill` (stretched). `SortBy` is `Custom` (the positions below), `Name`, `Kind` or `Size`. `View` is how folder windows show the files: `Grid` or `List`, and the `*Width` keys the widths of the list's columns.
 * Positions are grid cells, columns counted from the right edge. Their keys are percent-encoded
 * item keys (see DeskEntry::key).
 */
class Settings
{
public:
    enum class Fit { Cover, Contain, Fill };

    /// Icon order: Custom keeps the cells the user chose (positions), the others arrange every icon.
    enum class SortBy { Custom, Name, Kind, Size };

    void load() noexcept;
    void save() noexcept;

    /// Picture shown as the background (empty = none). DefaultPicture() until one is chosen.
    std::filesystem::path picture;

    /// Whether the picture is shown (false = solid color only).
    bool usePicture { true };

    Fit fit { Fit::Cover };

    /// Background color: behind the picture, or the whole background without one. Always opaque.
    SkColor color { DESK_DEFAULT_COLOR };

    Int32 iconSize { DESK_ICON_SIZE_DEFAULT };

    SortBy sortBy { SortBy::Custom };

    /// How folder windows show the files: icons (a grid) or a list. The last one chosen.
    bool listView { false };

    /**
     * @brief The list view's columns (resized from its header), shared by every window.
     *
     * Date, Size and Kind keep their width (points). The name's follows the window: chosen
     * (resized), it grows or shrinks by as much as the list's visible width does. Saved as its
     * difference from that width (listNameOffset: name = width + offset, at least
     * DESK_LIST_NAME_MIN); none (the default) fills the room the others leave (see FolderWindow).
     * listColumns.name itself isn't saved.
     */
    DeskListColumns listColumns;
    std::optional<Int32> listNameOffset;

    /// The sections collapsed in the info windows (see InfoWindow), remembered like Finder does:
    /// "General", "MoreInfo", "Name", "OpenWith", "Preview" or "Permissions".
    std::set<std::string> infoCollapsed { "Preview", "Permissions" };

    /// The folders in the Favorites of the folder windows' sidebars, in order (see Desk::favorites()).
    /// None saved yet: the defaults (home and the user folders).
    std::optional<std::vector<std::filesystem::path>> favorites;

    /// Grid cell of each item placed by the user (column from the right, row from the top).
    std::map<std::string, SkIPoint> positions;

    /// Whether the background shows the picture.
    bool showsPicture() const noexcept { return usePicture && !picture.empty(); }

    /**
     * @brief Copies @p source as the desktop picture kept with the settings
     *        (`cz-desk/wallpaper.<ext>` next to desk.conf), replacing the previous one.
     *
     * The background keeps showing even if the original is moved or deleted. Returns the copy's
     * path, or @p source if it couldn't be copied (logged).
     */
    static std::filesystem::path StorePicture(const std::filesystem::path &source) noexcept;

    /// Whether @p path is the copy made by StorePicture().
    static bool IsStoredPicture(const std::filesystem::path &path) noexcept;

    static std::filesystem::path FilePath() noexcept;

    /// The picture installed with Desk (`<prefix>/share/cz-desk/Assets/Wallpaper.jpg`), shown until
    /// one is chosen (no `Picture` key, e.g. the first launch). Empty if not installed.
    static std::filesystem::path DefaultPicture() noexcept;
};

#endif // SETTINGS_H
