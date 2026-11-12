#include <Core/Assets.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <CZ/Ream/RCore.h>
#include <CZ/Ream/RDevice.h>
#include <CZ/Ream/RImage.h>
#include <CZ/Marco/MScreen.h>
#include <CZ/skia/effects/SkImageFilters.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <CZ/XDG/XDGKit.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <CZ/XDG/XDGMimeDatabase.h>
#include <CZ/skia/codec/SkCodec.h>
#include <CZ/skia/codec/SkEncodedOrigin.h>
#include <CZ/skia/core/SkBitmap.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkData.h>
#include <CZ/skia/core/SkImage.h>
#include <CZ/skia/core/SkPixelRef.h>
#include <CZ/skia/core/SkSamplingOptions.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace fs = std::filesystem;

void Assets::init() noexcept
{
    m_thumbnailTimer = std::make_unique<CZTimer>([this](CZTimer *){ processThumbnailQueue(); });

    auto ream { RCore::Get() };
    const auto &formats { ream->mainDevice()->textureFormats().formats() };
    auto fmt { formats.find(DRM_FORMAT_ABGR8888) };

    if (fmt != formats.end())
        m_format = *fmt;
    else
        Log(CZError, CZLN, "ABGR8888 isn't a texture format of the main device: icons won't load");
}

/* ------------------------------------------ Icons ------------------------------------------ */

std::shared_ptr<RImage> Assets::loadIcon(const std::string &name) noexcept
{
    auto &xdgkit { GetDesk()->xdgkit };

    if (name.empty() || !xdgkit || m_format.format() == 0)
        return {};


    // Absolute paths are allowed in .desktop Icon keys.
    if (name.front() == '/')
        return loadFile(name);

    const auto *icon { xdgkit->iconThemeManager().findIcon(
        name, DESK_ICON_LOAD_SIZE, 1, XDGIcon::PNG | XDGIcon::SVG, { DESK_ICON_THEME, "" }) };

    if (!icon)
        return {};

    std::shared_ptr<RImage> image;

    if (icon->extensions() & XDGIcon::SVG)
        image = loadFile(icon->getPath(XDGIcon::SVG), name);

    if (!image && (icon->extensions() & XDGIcon::PNG))
        image = loadFile(icon->getPath(XDGIcon::PNG), name);

    return image;
}

std::shared_ptr<RImage> Assets::loadFile(const std::filesystem::path &path, const std::string &iconName) noexcept
{
    auto image { RImage::LoadFile(path, m_format, { DESK_ICON_LOAD_SIZE, DESK_ICON_LOAD_SIZE }) };

    if (image)
    {
        std::erase_if(m_sources, [](const auto &pair){ return pair.second.image.expired(); });
        m_sources[image.get()] = { image, iconName, path };
    }

    return image;
}

std::shared_ptr<RImage> Assets::sized(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale) noexcept
{
    if (!icon)
        return {};

    auto src { m_sources.find(icon.get()) };

    if (src == m_sources.end() || src->second.image.expired())
        return icon;

    const Source &source { src->second };
    scale = std::max(1, scale);
    const std::tuple<std::string, Int32, Int32> key { source.iconName.empty() ? source.path.string() : "icon:" + source.iconName, pixels, scale };

    if (auto it { m_sized.find(key) }; it != m_sized.end())
        return it->second;

    // An SVG is rendered again at that size (the same file: other sizes of a theme may be different
    // artwork, e.g. padded for panels). A raster themed icon: the theme's file for that size and
    // scale (only scaled, antialiased, if the theme doesn't have that size).
    std::filesystem::path path { source.path };
    auto &xdgkit { GetDesk()->xdgkit };

    if (!source.iconName.empty() && source.path.extension() != ".svg" && xdgkit)
        if (const auto *found { xdgkit->iconThemeManager().findIcon(source.iconName, std::max(1, (pixels + scale - 1) / scale), scale,
                                                                    XDGIcon::PNG | XDGIcon::SVG, { DESK_ICON_THEME, "" }) })
            path = found->getPath((found->extensions() & XDGIcon::SVG) ? XDGIcon::SVG : XDGIcon::PNG);

    auto image { RImage::LoadFile(path, m_format, { pixels, pixels }) };

    if (!image)
        image = icon;

    m_sized[key] = image;
    return image;
}

std::shared_ptr<RImage> Assets::themedIcon(const std::vector<std::string> &names) noexcept
{
    for (const auto &name : names)
    {
        auto it { m_icons.find(name) };

        if (it == m_icons.end())
            it = m_icons.emplace(name, loadIcon(name)).first;

        if (it->second)
            return it->second;
    }

    return {};
}

std::shared_ptr<RImage> Assets::icon(const DeskEntry &entry) noexcept
{
    std::vector<std::string> names;

    if (entry.kind == DeskEntry::Kind::Volume)
    {
        if (entry.internal)
            names = { "drive-harddisk-system", "drive-harddisk" };
        else if (entry.fsType == "iso9660" || entry.fsType == "udf")
            names = { "media-optical", "drive-optical" };
        else if (entry.usb)
            names = { "drive-harddisk-usb", "drive-removable-media-usb" };

        names.insert(names.end(), { "drive-removable-media", "drive-harddisk", "media-removable", "folder" });
        return themedIcon(names);
    }

    if (entry.launcher && !entry.launcher->icon.empty())
        names.emplace_back(entry.launcher->icon);

    if (entry.launcher)
        names.emplace_back("application-x-executable");

    const auto mimeIcons { GetDesk()->xdgkit->mimeDatabase().iconNames(entry.mimeType) };
    names.insert(names.end(), mimeIcons.begin(), mimeIcons.end());

    auto image { themedIcon(names) };

    if (!image)
        Log(CZWarning, CZLN, "No icon found for {} ({})", entry.name, entry.mimeType);

    return image;
}

std::shared_ptr<RImage> Assets::openFolderIcon() noexcept
{
    return themedIcon({ "folder-drag-accept", "folder-open" });
}

/* ---------------------------------------- Pictures ----------------------------------------- */

float Assets::ShadowMargin() noexcept
{
    // The shadow keeps the proportions it has at the default icon size.
    return float(DESK_ICON_SHADOW_RADIUS) / float(DESK_ICON_SIZE_DEFAULT);
}

std::shared_ptr<RImage> Assets::shadowedIcon(const std::shared_ptr<RImage> &icon, Int32 pixels, Int32 scale) noexcept
{
    if (!icon)
        return {};

    pixels = std::max(pixels, 8);

    if (auto it { m_shadowed.find({ icon.get(), pixels, scale }) }; it != m_shadowed.end() && !it->second.icon.expired())
        return it->second.image;

    std::erase_if(m_shadowed, [](const auto &pair){ return pair.second.icon.expired(); });

    // The image covers the icon plus the shadow margin on each side, in pixels (scale 1).
    const Int32 size { std::max(1, Int32(std::floor(SkScalar(pixels) / (1.f + 2.f * ShadowMargin())))) };
    const SkScalar margin { (pixels - size) / 2.f };
    const SkScalar ratio { SkScalar(size) / SkScalar(DESK_ICON_SIZE_DEFAULT) };
    const SkScalar sigma { DESK_ICON_SHADOW_RADIUS * ratio / 3.f };
    const SkScalar dy { DESK_ICON_SHADOW_OFFSET_Y * ratio };

    // Themed icons are loaded again at their exact size (never resampled). Thumbnails (decoded
    // pictures, at least as large) are resampled once, with a cubic filter.
    auto sizedIcon { sized(icon, size, scale) };
    const bool exact { sizedIcon != icon };
    auto source { sizedIcon->skImage() };
    auto surface { source ? RSurface::Make(SkISize::Make(pixels, pixels), 1, true) : nullptr };

    if (!surface)
        return icon;

    {
        auto pass { surface->beginPass(RPassCap_SkCanvas) };
        SkCanvas *c { pass->getCanvas() };
        c->clear(SK_ColorTRANSPARENT);

        // Contained and centered in the icon square (thumbnails keep their aspect ratio).
        const SkScalar s { std::min(SkScalar(size) / source->width(), SkScalar(size) / source->height()) };
        const SkRect dst { SkRect::MakeXYWH(margin + (size - source->width() * s) / 2.f, margin + (size - source->height() * s) / 2.f,
                                            source->width() * s, source->height() * s) };

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setImageFilter(SkImageFilters::DropShadow(0.f, dy, sigma, sigma, DESK_ICON_SHADOW_COLOR, nullptr));
        const SkSamplingOptions sampling { exact ? SkSamplingOptions(SkFilterMode::kLinear) : SkSamplingOptions(SkCubicResampler::Mitchell()) };
        c->drawImageRect(source, SkRect::MakeIWH(source->width(), source->height()), dst,
                         sampling, &paint, SkCanvas::kFast_SrcRectConstraint);
    }

    auto image { surface->image() };
    m_shadowed[{ icon.get(), pixels, scale }] = { icon, image };
    return image;
}

bool Assets::HasThumbnail(const std::string &mimeType) noexcept
{
    static const std::unordered_set<std::string> types {
        "image/png", "image/jpeg", "image/webp", "image/gif", "image/bmp", "image/x-bmp",
        "image/vnd.microsoft.icon", "image/x-icon", "image/vnd.wap.wbmp", "image/avif", "image/heif"
    };

    return types.contains(mimeType);
}

std::shared_ptr<RImage> Assets::decode(const fs::path &path, const std::function<SkISize(SkISize)> &sizeFor) noexcept
{
    if (m_format.format() == 0)
        return {};

    auto data { SkData::MakeFromFileName(path.c_str()) };

    if (!data)
        return {};

    auto codec { SkCodec::MakeFromData(data) };

    if (!codec)
        return {};

    const SkEncodedOrigin origin { codec->getOrigin() };
    const bool swapped { SkEncodedOriginSwapsWidthHeight(origin) };
    const SkISize encoded { codec->dimensions() };
    const SkISize oriented { swapped ? SkISize::Make(encoded.height(), encoded.width()) : encoded };
    const SkISize target { sizeFor(oriented) };

    if (target.isEmpty())
        return {};

    // Let the codec downscale while decoding when it can (JPEG does 1/2, 1/4, 1/8), staying at
    // least as big as the target.
    const float ratio { std::max(float(target.width()) / oriented.width(), float(target.height()) / oriented.height()) };
    SkISize decodeSize { encoded };

    if (ratio < 1.f)
    {
        const SkISize scaled { codec->getScaledDimensions(ratio) };
        const SkISize scaledOriented { swapped ? SkISize::Make(scaled.height(), scaled.width()) : scaled };

        if (scaledOriented.width() >= target.width() && scaledOriented.height() >= target.height())
            decodeSize = scaled;
    }

    SkBitmap decoded;

    if (!decoded.tryAllocPixels(codec->getInfo().makeDimensions(decodeSize).makeColorType(kRGBA_8888_SkColorType).makeAlphaType(kPremul_SkAlphaType)))
        return {};

    const auto result { codec->getPixels(decoded.pixmap()) };

    if (result != SkCodec::kSuccess && result != SkCodec::kIncompleteInput && result != SkCodec::kErrorInInput)
        return {};

    // Orient and scale with mipmapped linear filtering (smooth downscaling).
    SkBitmap final;

    if (!final.tryAllocPixels(SkImageInfo::Make(target, kRGBA_8888_SkColorType, kPremul_SkAlphaType)))
        return {};

    SkCanvas canvas { final };
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.scale(float(target.width()) / oriented.width(), float(target.height()) / oriented.height());

    if (origin != kDefault_SkEncodedOrigin)
    {
        // Maps the encoded image to the oriented space (at the encoded resolution).
        canvas.concat(SkEncodedOriginToMatrix(origin, encoded.width(), encoded.height()));
    }

    canvas.scale(float(encoded.width()) / decodeSize.width(), float(encoded.height()) / decodeSize.height());

    auto image { decoded.asImage() };
    canvas.drawImage(image.get(), 0.f, 0.f, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear));

    RPixelBufferInfo info {};
    info.pixels = static_cast<UInt8*>(final.getPixels());
    info.stride = UInt32(final.rowBytes());
    info.format = m_format.format();
    info.size = target;
    return RImage::MakeFromPixels(info, m_format);
}

std::shared_ptr<RImage> Assets::thumbnail(const fs::path &path, Int32 maxSize) noexcept
{
    std::error_code ec;
    const auto mtime { fs::last_write_time(path, ec) };

    if (auto it { m_thumbnails.find(path.string()) }; it != m_thumbnails.end() && it->second.mtime == mtime && it->second.maxSize >= maxSize)
        return it->second.image;

    if (std::none_of(m_thumbnailQueue.begin(), m_thumbnailQueue.end(), [&](const auto &q){ return q.first == path; }))
        m_thumbnailQueue.emplace_back(path, maxSize);

    if (m_thumbnailTimer)
        m_thumbnailTimer->start(0);

    return {};
}

void Assets::processThumbnailQueue() noexcept
{
    if (m_thumbnailQueue.empty())
        return;

    const auto [path, maxSize] { m_thumbnailQueue.front() };
    m_thumbnailQueue.pop_front();

    std::error_code ec;
    Thumbnail &thumb { m_thumbnails[path.string()] };
    thumb.mtime = fs::last_write_time(path, ec);
    thumb.maxSize = maxSize;
    thumb.image.reset();

    const auto bytes { fs::file_size(path, ec) };

    if (!ec && bytes <= UInt64(DESK_THUMBNAIL_MAX_MB) * 1024 * 1024)
        thumb.image = decode(path, [maxSize](SkISize src) {
            const float s { std::min(1.f, float(maxSize) / float(std::max(src.width(), src.height()))) };
            return SkISize::Make(std::max(1, Int32(std::lround(src.width() * s))), std::max(1, Int32(std::lround(src.height() * s))));
        });

    thumb.failed = !thumb.image;

    // Failed ones are kept (as nullptr) so the item shows its icon and doesn't retry.
    onThumbnailReady.notify(path);

    if (!m_thumbnailQueue.empty())
        m_thumbnailTimer->start(0);
}

void Assets::pruneThumbnails() noexcept
{
    std::erase_if(m_thumbnails, [](const auto &pair){
        std::error_code ec;
        const auto mtime { fs::last_write_time(pair.first, ec) };
        return ec || mtime != pair.second.mtime;
    });
}

std::shared_ptr<RImage> Assets::picture(const fs::path &path, SkISize screen, Settings::Fit fit) noexcept
{
    if (path.empty() || screen.isEmpty())
        return {};

    std::error_code ec;
    const auto mtime { fs::last_write_time(path, ec) };
    const std::string key { path.string() + "|" + std::to_string(screen.width()) + "x" + std::to_string(screen.height()) +
        "|" + std::to_string(int(fit)) + "|" + std::to_string(mtime.time_since_epoch().count()) };

    for (const auto &p : m_pictures)
        if (p.key == key)
            return p.image;

    auto image { decode(path, [screen, fit](SkISize src) {
        // The size it covers/fits the screen at, never upscaled (the frame scales it up).
        float s;

        if (fit == Settings::Fit::Fill)
            return SkISize::Make(std::min(src.width(), screen.width()), std::min(src.height(), screen.height()));
        else if (fit == Settings::Fit::Cover)
            s = std::max(float(screen.width()) / src.width(), float(screen.height()) / src.height());
        else
            s = std::min(float(screen.width()) / src.width(), float(screen.height()) / src.height());

        s = std::min(s, 1.f);
        return SkISize::Make(std::max(1, Int32(std::ceil(src.width() * s))), std::max(1, Int32(std::ceil(src.height() * s))));
    }) };

    // SVG (or anything the codecs don't know): Ream renders it at the screen size.
    if (!image && m_format.format() != 0)
        image = RImage::LoadFile(path, m_format, screen);

    if (!image)
        Log(CZError, CZLN, "Can't load the background picture {}", path.string());

    m_pictures.push_back({ key, image });

    if (m_pictures.size() > 4)
        m_pictures.erase(m_pictures.begin());

    return image;
}
