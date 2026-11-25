#ifndef DESK_H
#define DESK_H

#include <Core/Types.h>
#include <Core/Assets.h>
#include <Core/Settings.h>
#include <CZ/Marco/MApp.h>
#include <CZ/Marco/Roles/MLayerSurface.h>
#include <CZ/XDG/XDGKit.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <CZ/Core/CZWeak.h>
#include <filesystem>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <vector>

namespace CZ { class CZEventSource; class CZBus; }
class Background;
namespace CZ { class MDNDOffer; }
class MenuBar;
class FolderWindow;
class FolderScanner;
/// Where something was opened from on the desktop: an icon's rect, in global coordinates.
struct IconOrigin
{
    SkIRect rect {};
};

/// Something shown on the desktop: a mounted disk or a file in the desktop folder.
struct DeskEntry
{
    enum class Kind { Volume, File };
    Kind kind { Kind::File };

    /// Unique and stable: the file name, or "volume:" + the mount point.
    std::string key;

    std::filesystem::path path;  ///< The file, or the volume's mount point
    std::string name;            ///< Display name
    std::string mimeType;        ///< Of the file (inode/directory for volumes)

    // Volumes
    std::string device;          ///< e.g. /dev/sdb1
    std::string fsType;          ///< e.g. vfat
    bool internal { false };     ///< The system disk (mounted at /): can't be ejected
    bool usb { false };          ///< Connected through USB

    UInt64 size { 0 };           ///< Bytes (regular files)
    std::time_t modified { 0 };  ///< Last modification (0: unknown)
    std::string kindName;        ///< e.g. "PNG image" (sorting by kind)

    /// Launchers (.desktop files of type Application): shown with their own name and icon.
    std::shared_ptr<const XDGDesktopEntry> launcher;
};

/**
 * @brief The Desk app: owns the Marco app, the shared assets, the desktop model (mounted disks and
 *        the desktop folder's files) and one Background per screen.
 *
 * Every screen gets the background (picture or solid color) and the icons. Each screen has its own
 * selection; the menu bar acts on the one used last (see iconsBackground()).
 */
class Desk final : public CZObject
{
public:
    Desk() noexcept;
    ~Desk() noexcept;

    std::shared_ptr<MApp> marco;
    std::shared_ptr<XDGKit> xdgkit;
    Assets assets;
    Settings settings;

    /// The system disk, then the mounted external disks, then the desktop folder's files (sorted by name).
    const std::vector<DeskEntry> &entries() const noexcept { return m_entries; }
    const DeskEntry *entry(const std::string &key) const noexcept;

    /// The desktop folder (XDG_DESKTOP_DIR, ~/Desktop by default).
    const std::filesystem::path &desktopDir() const noexcept { return m_desktopDir; }

    /**
     * @brief The desktop the menu bar and the keyboard act on: the one used last (clicked, typed in
     *        or dragged from), else the primary screen's (the one at 0,0).
     */
    Background *iconsBackground() const noexcept;

    /// Makes @p background the one used last (the selection on the others is cleared).
    void setActiveBackground(Background *background) noexcept;

    /// A folder window isn't active anymore: the desktop's menus are shown again unless another is.
    void windowDeactivated() noexcept;

    /// The desktop an icon drag started from (nullptr when not dragging).
    Background *dragSource() const noexcept;
    void setDragSource(Background *background) noexcept;

    /**
     * @brief The files dragged from a desktop or a folder window (empty when not dragging).
     *
     * Drops of Desk's own drags (DESK_ITEMS_MIME_TYPE, DESK_FILES_MIME_TYPE) take them from here:
     * reading the offer's data would wait for Desk itself (see dropInto()).
     */
    const std::vector<std::filesystem::path> &dragFiles() const noexcept { return m_dragFiles; }
    void setDragFiles(std::vector<std::filesystem::path> files) noexcept { m_dragFiles = std::move(files); }

    /// Whether a drag offers files to drop into a folder: from other apps, or Desk's own.
    bool offersFiles(CZ::MDNDOffer &offer) const noexcept;

    /**
     * @brief Completes a drop of files into the folder @p dir (see transfer()): moved, or copied if
     *        the user chose it (or @p copyByDefault and no action was chosen). Returns the files.
     */
    std::vector<std::filesystem::path> dropInto(const std::filesystem::path &dir, bool copyByDefault, CZ::MDNDOffer &offer) noexcept;

    /// The files of a drop (Desk's own or another app's), completing it without moving them (e.g.
    /// folders added to the favorites).
    std::vector<std::filesystem::path> receiveDrop(CZ::MDNDOffer &offer) noexcept;

    /* Favorites (the folder windows' sidebars) */

    /// The favorite folders, in order: the saved ones (see Settings::favorites), else the defaults
    /// (home, Desktop, /usr/share/applications and the user folders that exist).
    std::vector<std::filesystem::path> favorites() const noexcept;

    /// Places the folders @p dirs at @p index of the favorites (those already there move), saved.
    void placeFavorites(const std::vector<std::filesystem::path> &dirs, size_t index) noexcept;

    /// Removes the folder @p dir from the favorites, saved.
    void removeFavorite(const std::filesystem::path &dir) noexcept;

    /// Emitted after the favorites change (see placeFavorites()).
    CZSignal<> onFavoritesChanged;

    /// "file://..." lines (text/uri-list) for @p files, percent-encoded.
    static std::string URIList(const std::vector<std::filesystem::path> &files) noexcept;

    /// An XDG user directory (user-dirs.dirs key without the XDG_/_DIR, e.g. "DOCUMENTS"), or
    /// ~/@p fallback.
    std::filesystem::path userDir(const std::string &key, const std::string &fallback) const noexcept;

    /// Height of Firmament's top bar (its size setting is followed live).
    Int32 topbarHeight() const noexcept;

    /**
     * @brief Shows a folder (or disk) in a window (see FolderWindow).
     *
     * If a window already shows it, that window is activated instead (unless @p newWindow).
     * Folders that can't be read (or aren't folders, e.g. trash:///) are handed to xdg-open.
     *
     * @param select A file of the folder to select (its name), if any.
     * @param token  An activation token from the app asking (see MToplevel::activate()), if any.
     * @param origin The desktop icon it's opened from, if any: a new window grows from it (the
     *               compositor's animation, see announceFromIcon()).
     *
     * @return Whether a new window grows from @p origin (the compositor accepted it).
     */
    bool openFolder(const std::filesystem::path &path, bool newWindow = false, const std::string &select = {},
                    const std::string &token = {}, const IconOrigin *origin = nullptr) noexcept;

    /// The open folder windows.
    const std::vector<std::unique_ptr<FolderWindow>> &windows() const noexcept { return m_windows; }

    /// The mounted disks (the system disk first), as shown on the desktop.
    std::vector<const DeskEntry*> volumes() const noexcept;

    /**
     * @brief The files of a folder (see DeskEntry), sorted by name (case-insensitive).
     *
     * Hidden ones (starting with ".") and backups (ending with "~") only with @p hidden. Keys are
     * the file names.
     */
    std::vector<DeskEntry> scanFolder(const std::filesystem::path &dir, bool hidden = false) const noexcept;

    /// scanFolder() with the XDGKit @p kit (e.g. from another thread, see FolderScanner).
    static std::vector<DeskEntry> ScanFolder(XDGKit &kit, const std::filesystem::path &dir, bool hidden) noexcept;

    /// Reads folders in the background (folder windows).
    std::unique_ptr<FolderScanner> scanner;

    /// The name a folder is shown with: the disk's name for mount points, the file name otherwise.
    std::string displayName(const std::filesystem::path &dir) const noexcept;

    /// Desktop icon size (clamped, saved).
    void setIconSize(Int32 size) noexcept;

    /// The next / previous size in DESK_ICON_SIZES.
    Int32 largerIconSize() const noexcept;
    Int32 smallerIconSize() const noexcept;

    /// Icon order (saved). Arranged orders ignore the custom positions (kept for Custom).
    void setSortBy(Settings::SortBy sortBy) noexcept;

    /// The selection changed: refreshes the menus (coalesced).
    void selectionChanged() noexcept;

    /// Runs the menu bar item with the shortcut Ctrl(+Shift)+@p key, if any.
    void shortcut(UInt32 key, bool shift) noexcept;

    /// Desk's menus in the global menu bar (nullptr until Marco is ready).
    std::unique_ptr<MenuBar> menuBar;

    /**
     * @brief Opens entries with their default apps (launchers are launched). Folders open in windows
     *        growing from their icon in @p origins (by path), if there.
     *
     * @return The paths whose new window grows from its icon (no other opening animation needed).
     */
    std::unordered_set<std::string> open(const std::vector<const DeskEntry*> &entries,
              const std::unordered_map<std::string, IconOrigin> *origins = nullptr) noexcept;

    /// Opens a file with a specific app.
    void openWith(const DeskEntry &entry, const XDGDesktopEntry &app) noexcept;

    /// Tells Crystals the user launched @p app (AppLaunched on org.cuarzo.Desk): its first window
    /// is shown wherever it opens, like apps launched from the dock.
    void announceLaunch(const XDGDesktopEntry &app) noexcept;

    /// Apps that can open @p entry, the default one first.
    std::vector<std::shared_ptr<const XDGDesktopEntry>> appsFor(const DeskEntry &entry) noexcept;

    /// Moves files to the trash (XDG trash spec). Volumes are skipped.
    void trash(const std::vector<const DeskEntry*> &entries) noexcept;

    /**
     * @brief Moves or copies files into the folder @p dir (files dropped on a folder or disk).
     *
     * Runs in the background (never overwriting files already there). Like Finder, moving files
     * from another disk copies them. Files already in @p dir (moving) and @p dir itself or its
     * parents are skipped.
     */
    void transfer(const std::vector<std::filesystem::path> &files, const std::filesystem::path &dir, bool copy) noexcept;

    /* Clipboard (files, like other file managers: see copyFiles()) */

    /**
     * @brief Puts @p files on the clipboard: copied (or moved if @p cut) where they're pasted.
     *
     * As "x-special/gnome-copied-files" (what file managers use), "text/uri-list" and their paths
     * as text. Uses the latest input event of the app (the clipboard needs one).
     */
    void copyFiles(const std::vector<std::filesystem::path> &files, bool cut) noexcept;

    /// Whether the clipboard has files (copied here or in another file manager).
    bool canPaste() const noexcept;

    /// The clipboard's files into the folder @p dir: copied, or moved if they were cut (see transfer()).
    void pasteInto(const std::filesystem::path &dir) noexcept;

    /// Unmounts a volume and powers its drive off.
    void eject(const DeskEntry &volume) noexcept;

    /// Creates "untitled folder" (or "untitled folder N") in the desktop folder. Returns its key.
    std::string newFolder() noexcept;

    /// Creates "untitled folder" (or "untitled folder N") in @p dir. Returns its name (empty on failure).
    std::string newFolder(const std::filesystem::path &dir) noexcept;

    /**
     * @brief Renames a file or folder (a desktop one keeps its place in the grid).
     *
     * Fails (logged) for volumes, invalid names (empty, ".", "..", with "/") and names already
     * taken. Returns its new key, or an empty string on failure.
     */
    std::string rename(const DeskEntry &entry, const std::string &name) noexcept;

    /// Shows @p picture as the background (saved, as a copy: see Settings::StorePicture()).
    void setPicture(const std::filesystem::path &picture) noexcept;
    void setUsePicture(bool use) noexcept;
    void setFit(Settings::Fit fit) noexcept;

    /// Stores (or forgets, with nullptr) the grid cell of an item. Saved by saveSettings().
    void setPosition(const std::string &key, const SkIPoint *cell) noexcept;
    void clearPositions() noexcept;
    void saveSettings() noexcept;

    /// Runs a command (argv) detached.
    static bool Spawn(const std::vector<std::string> &argv) noexcept;

    /// E.g. "1.2 GB" (decimal units, like Finder).
    static std::string FormatBytes(UInt64 bytes) noexcept;

    /// E.g. "Today at 10:32", "Yesterday at 18:05", "3 Nov 2026 at 09:14" (like Finder's lists).
    static std::string FormatDate(std::time_t time) noexcept;

    /// Emitted after entries() change (files added/removed/renamed, disks mounted/unmounted).
    CZSignal<> onEntriesChanged;

    /// Emitted after the background settings change.
    CZSignal<> onBackgroundChanged;

    /// Emitted while the list view's columns are resized (see Settings::listColumns), in any window.
    CZSignal<> onListColumnsChanged;

    /// Emitted after the icon size or the positions change from outside (settings file edited).
    CZSignal<> onIconsChanged;

private:
    void onScreenPlugged(MScreen &screen) noexcept;
    void onScreenUnplugged(MScreen &screen) noexcept;
    // Backgrounds, held through their base class (Background needs this header).
    CZWeak<MLayerSurface> m_activeBackground, m_dragSource;
    std::vector<std::filesystem::path> m_dragFiles;

    void initBus() noexcept;
    void initDesktopDir() noexcept;
    void initWatches() noexcept;
    void scheduleRescan() noexcept;
    void rescan() noexcept;
    std::vector<DeskEntry> scanVolumes() noexcept;
    std::vector<DeskEntry> scanFiles() noexcept;
    void settingsFileChanged() noexcept;
    void sortEntries(std::vector<DeskEntry> &entries) const noexcept;
    void loadTopbarScale() noexcept;
    float m_topbarScale { 1.f };
    Int32 m_firmamentWatch { -1 };

    std::vector<DeskEntry> m_entries;
    std::filesystem::path m_desktopDir;

    /**
     * @brief Tells Crystals a window titled @p title is about to be mapped, opened from the icon at
     *        @p origin: it grows from there (org.cuarzo.Crystals OpenFromIcon on the session bus).
     *
     * Waits for the reply (briefly), so Crystals knows before the window maps.
     */
    bool announceFromIcon(const std::string &title, const IconOrigin &origin) noexcept;

    // Closed windows are destroyed later (not from their own handlers)
    std::vector<std::unique_ptr<FolderWindow>> m_windows;
    std::unique_ptr<CZTimer> m_windowsTimer;
    std::unique_ptr<CZTimer> m_deactivatedTimer; // See windowDeactivated()
    void destroyClosedWindows() noexcept;

    Int32 m_inotify { -1 };
    Int32 m_desktopWatch { -1 }, m_configWatch { -1 };
    std::shared_ptr<CZEventSource> m_inotifySource, m_mountsSource;
    std::unique_ptr<CZTimer> m_rescanTimer; // Created once the CZCore exists (MApp)
    std::unique_ptr<CZTimer> m_menuTimer;
    std::shared_ptr<CZBus> m_bus;
};

Desk *GetDesk() noexcept;

#endif // DESK_H
