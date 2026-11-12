#ifndef ASSETS_H
#define ASSETS_H

#include <Core/Types.h>
#include <Core/Settings.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Ream/DRM/RDRMFormat.h>
#include <CZ/skia/core/SkSize.h>
#include <deque>
#include <map>
#include <tuple>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CZ { class RImage; }
struct DeskEntry;

/**
 * @brief Icons, thumbnails and background pictures (cached), shared by every screen.
 */
class Assets
{
public:
    /// Call once Marco (Ream) and XDGKit are ready.
    void init() noexcept;

    /// The (themed) icon of an entry. Never nullptr unless no generic icon could be loaded either.
    std::shared_ptr<RImage> icon(const DeskEntry &entry) noexcept;

    /// The open folder icon (a folder something is dragged over), nullptr if the theme has none.
    std::shared_ptr<RImage> openFolderIcon() noexcept;

    /**
     * @brief @p icon with its drop shadow, pre-rendered @p pixels wide/tall (cached per icon and size).
     *
     * For the size it's drawn at on each screen (see IconView): the icon (loaded again at its exact
     * size, see sized()) fills the middle, with a ShadowMargin() border (fraction of the icon size)
     * on every side for the shadow.
     */
    std::shared_ptr<RImage> shadowedIcon(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale) noexcept;

    /**
     * @brief @p icon (loaded by this class) again, at @p pixels for a screen with scale @p scale.
     *
     * SVGs are rendered again at that size. Raster themed icons are looked up again for that size
     * and scale (scaled, antialiased, only if the theme has no such size). Other files
     * are loaded again at that size. Cached. Returns @p icon itself if it's not known (e.g.
     * thumbnails).
     */
    std::shared_ptr<RImage> sized(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale) noexcept;

    /// The shadow border of shadowedIcon(), as a fraction of the icon size.
    static float ShadowMargin() noexcept;

    /// Whether pictures of this type get a thumbnail instead of an icon.
    static bool HasThumbnail(const std::string &mimeType) noexcept;

    /**
     * @brief The thumbnail of a picture, fitting @p maxSize pixels.
     *
     * Returns nullptr while it isn't ready: it's then queued and decoded in the background of the
     * event loop (one per iteration), and onThumbnailReady is emitted with its path.
     */
    std::shared_ptr<RImage> thumbnail(const std::filesystem::path &path, Int32 maxSize) noexcept;

    /// Drops thumbnails of files that changed or no longer exist.
    void pruneThumbnails() noexcept;

    CZSignal<const std::filesystem::path &> onThumbnailReady;

    /**
     * @brief The background picture, decoded for a screen of @p screenSize pixels.
     *
     * Decoded at the size it's shown at (never upscaled). nullptr if it can't be read.
     */
    std::shared_ptr<RImage> picture(const std::filesystem::path &path, SkISize screenSize, Settings::Fit fit) noexcept;

private:
    std::shared_ptr<RImage> themedIcon(const std::vector<std::string> &names) noexcept;
    std::shared_ptr<RImage> loadIcon(const std::string &name) noexcept;
    void processThumbnailQueue() noexcept;

    /// Decodes a raster picture (honoring its EXIF orientation) and scales it to @p sizeFor(source size).
    std::shared_ptr<RImage> decode(const std::filesystem::path &path, const std::function<SkISize(SkISize)> &sizeFor) noexcept;

    RDRMFormat m_format { 0, {} };
    std::unordered_map<std::string, std::shared_ptr<RImage>> m_icons; // By icon name

    struct Thumbnail
    {
        std::shared_ptr<RImage> image;
        std::filesystem::file_time_type mtime;
        Int32 maxSize { 0 };
        bool failed { false };
    };

    std::unordered_map<std::string, Thumbnail> m_thumbnails; // By path
    std::deque<std::pair<std::filesystem::path, Int32>> m_thumbnailQueue;
    std::unique_ptr<CZTimer> m_thumbnailTimer; // Created by init() (timers need the CZCore)

    struct Picture
    {
        std::string key;
        std::shared_ptr<RImage> image;
    };

    std::vector<Picture> m_pictures; // A few recent ones (one per screen size, usually)

    struct Shadowed
    {
        std::weak_ptr<RImage> icon;
        std::shared_ptr<RImage> image;
    };

    std::map<std::tuple<const RImage*, Int32, Int32>, Shadowed> m_shadowed; // By icon, size and scale

    // Where each loaded icon comes from (to load it at other sizes).
    std::shared_ptr<RImage> loadFile(const std::filesystem::path &path, const std::string &iconName = {}) noexcept;
    struct Source
    {
        std::weak_ptr<RImage> image;
        std::string iconName;       ///< Themed icon (looked up again per size and scale)
        std::filesystem::path path; ///< Otherwise, the file
    };
    std::unordered_map<const RImage*, Source> m_sources;
    std::map<std::tuple<std::string, Int32, Int32>, std::shared_ptr<RImage>> m_sized; // By source, size and scale
};

#endif // ASSETS_H
