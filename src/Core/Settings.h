#ifndef SETTINGS_H
#define SETTINGS_H

#include <Core/Types.h>
#include <CZ/skia/core/SkPoint.h>
#include <filesystem>
#include <map>
#include <string>

/**
 * @brief Persistent Desk settings, stored in `$XDG_CONFIG_HOME/cz-desk/desk.conf`
 *        (`~/.config/cz-desk/desk.conf` by default).
 *
 * The file is watched: editing it by hand applies the changes right away.
 *
 * @code
 * [Background]
 * Picture=/home/user/Pictures/beach.jpg
 * UsePicture=true
 * Fit=Cover
 * Color=#1F4E79
 *
 * [Icons]
 * Size=64
 * SortBy=Custom
 *
 * [Positions]
 * Notes.txt=0,2
 * @endcode
 *
 * `Fit` is `Cover` (fills the screen, cropping), `Contain` (whole picture, the color around it) or
 * `Fill` (stretched). `SortBy` is `Custom` (the positions below), `Name`, `Kind` or `Size`.
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

    /// Picture shown as the background (empty = none).
    std::filesystem::path picture;

    /// Whether the picture is shown (false = solid color only).
    bool usePicture { true };

    Fit fit { Fit::Cover };

    /// Background color: behind the picture, or the whole background without one. Always opaque.
    SkColor color { DESK_DEFAULT_COLOR };

    Int32 iconSize { DESK_ICON_SIZE_DEFAULT };

    SortBy sortBy { SortBy::Custom };

    /// Grid cell of each item placed by the user (column from the right, row from the top).
    std::map<std::string, SkIPoint> positions;

    /// Whether the background shows the picture.
    bool showsPicture() const noexcept { return usePicture && !picture.empty(); }

    static std::filesystem::path FilePath() noexcept;
};

#endif // SETTINGS_H
