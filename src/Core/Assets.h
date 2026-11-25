#ifndef ASSETS_H
#define ASSETS_H

#include <Core/Types.h>
#include <Core/Settings.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Ream/DRM/RDRMFormat.h>
#include <CZ/AssetBroker/ABIconKey.h>
#include <CZ/AssetBroker/ABShadow.h>
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

namespace CZ { class RImage; class ABCore; }
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

    /// Shadow of the desktop icons: strong, so they stand out on any picture.
    static const ABShadow DesktopShadow;

    /// Shadow of the icons in folder windows: faint, over the window's plain background.
    static const ABShadow WindowShadow;

    /**
     * @brief @p icon over its drop shadow @p shadow, @p pixels wide/tall (for the size it's drawn
     *        at on each screen, see IconView).
     *
     * The icon fills the middle, with a ShadowMargin() border (fraction of the icon size) on every
     * side for the shadow. Themed icons are drawn with their shadow by the AssetBroker (loaded
     * again at their exact size from their file, and shared with other apps if the service is
     * running). Thumbnails are drawn here (cached per thumbnail, size and shadow).
     */
    std::shared_ptr<RImage> shadowedIcon(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale, const ABShadow &shadow) noexcept;

    /**
     * @brief @p icon (loaded by this class) again, at @p pixels for a screen with scale @p scale.
     *
     * Rendered again from the same file at that size (through the AssetBroker: shared with other
     * apps if the service is running). Returns @p icon itself if it's not known (e.g. thumbnails).
     */
    std::shared_ptr<RImage> sized(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale) noexcept;

    /// The shadow border of shadowedIcon(), as a fraction of the icon size.
    static float ShadowMargin(const ABShadow &shadow) noexcept { return shadow.radius; }

    /// Whether pictures of this type get a thumbnail instead of an icon.
    static bool HasThumbnail(const std::string &mimeType) noexcept;

    /**
     * @brief The thumbnail of a picture, fitting @p maxSize pixels.
     *
     * Returns nullptr while it isn't ready: it's then queued and decoded in the background of the
     * event loop (one per iteration), and onThumbnailReady is emitted with its path. The latest
     * requests are decoded first (e.g. what scrolled into view). Each request counts until
     * cancelThumbnail(): a picture nobody waits for anymore isn't decoded.
     */
    std::shared_ptr<RImage> thumbnail(const std::filesystem::path &path, Int32 maxSize) noexcept;

    /// A request of thumbnail() not needed anymore (e.g. its cell scrolled out of view and shows
    /// another file). Decoded anyway if others still wait for it.
    void cancelThumbnail(const std::filesystem::path &path) noexcept;

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
    /// First themed icon found among @p names (cached, including misses).
    std::shared_ptr<RImage> themedIcon(const std::vector<std::string> &names) noexcept;

    /// Desk icon lookup (DESK_ICON_THEME first, at DESK_ICON_LOAD_SIZE).
    static ABIconKey iconKey(std::vector<std::string> names) noexcept;
    void processThumbnailQueue() noexcept;

    /// Decodes a raster picture (honoring its EXIF orientation) and scales it to @p sizeFor(source size).
    std::shared_ptr<RImage> decode(const std::filesystem::path &path, const std::function<SkISize(SkISize)> &sizeFor) noexcept;

    RDRMFormat m_format { 0, {} }; // Thumbnails and pictures (decoded here)
    std::shared_ptr<ABCore> m_ab;  // Icons
    std::unordered_map<std::string, std::shared_ptr<RImage>> m_icons; // By icon names

    struct Thumbnail
    {
        std::shared_ptr<RImage> image;
        std::filesystem::file_time_type mtime;
        Int32 maxSize { 0 };
        bool failed { false };
    };

    std::unordered_map<std::string, Thumbnail> m_thumbnails; // By path
    struct ThumbnailRequest
    {
        std::filesystem::path path;
        Int32 maxSize;
        UInt32 waiting; // Requests not cancelled (see cancelThumbnail())
    };
    std::deque<ThumbnailRequest> m_thumbnailQueue; // The latest at the back (decoded first)
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

    std::map<std::tuple<const RImage*, Int32, Int32, const ABShadow*>, Shadowed> m_shadowed; // Thumbnails, by image, size, scale and shadow

    // Where each loaded icon comes from (to load it at other sizes)
    struct Source
    {
        std::weak_ptr<RImage> image;
        ABIconKey key;
    };

    std::unordered_map<const RImage*, Source> m_sources;
};

#endif // ASSETS_H
