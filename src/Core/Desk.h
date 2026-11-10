#ifndef DESK_H
#define DESK_H

#include <Core/Types.h>
#include <Core/Assets.h>
#include <Core/Settings.h>
#include <CZ/Marco/MApp.h>
#include <CZ/XDG/XDGKit.h>
#include <CZ/Core/CZObject.h>
#include <CZ/Core/CZSignal.h>
#include <CZ/Core/CZTimer.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace CZ { class CZEventSource; class CZBus; }
class Background;
class MenuBar;

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

    /// Launchers (.desktop files of type Application): shown with their own name and icon.
    std::shared_ptr<const XDGDesktopEntry> launcher;
};

/**
 * @brief The Desk app: owns the Marco app, the shared assets, the desktop model (mounted disks and
 *        the desktop folder's files) and one Background per screen.
 *
 * Every screen gets the background (picture or solid color). The icons are only shown on the
 * primary screen (the one at 0,0), like on macOS.
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

    /// The screen the icons are shown on.
    MScreen *iconsScreen() const noexcept;

    /// The desktop of iconsScreen() (the one with the icons and the selection).
    Background *iconsBackground() const noexcept;

    /// An XDG user directory (user-dirs.dirs key without the XDG_/_DIR, e.g. "DOCUMENTS"), or
    /// ~/@p fallback.
    std::filesystem::path userDir(const std::string &key, const std::string &fallback) const noexcept;

    /// Opens a folder in the file manager.
    void openFolder(const std::filesystem::path &path) noexcept;

    /// Desktop icon size (clamped, saved).
    void setIconSize(Int32 size) noexcept;

    /// The selection changed: refreshes the menus (coalesced).
    void selectionChanged() noexcept;

    /// Runs the menu bar item with the shortcut Ctrl(+Shift)+@p key, if any.
    void shortcut(UInt32 key, bool shift) noexcept;

    /// Desk's menus in the global menu bar (nullptr until Marco is ready).
    std::unique_ptr<MenuBar> menuBar;

    /// Opens entries with their default apps (launchers are launched).
    void open(const std::vector<const DeskEntry*> &entries) noexcept;

    /// Opens a file with a specific app.
    void openWith(const DeskEntry &entry, const XDGDesktopEntry &app) noexcept;

    /// Apps that can open @p entry, the default one first.
    std::vector<std::shared_ptr<const XDGDesktopEntry>> appsFor(const DeskEntry &entry) noexcept;

    /// Moves files to the trash (XDG trash spec). Volumes are skipped.
    void trash(const std::vector<const DeskEntry*> &entries) noexcept;

    /// Unmounts a volume and powers its drive off.
    void eject(const DeskEntry &volume) noexcept;

    /// Creates "untitled folder" (or "untitled folder N") in the desktop folder. Returns its key.
    std::string newFolder() noexcept;

    /// Shows @p picture as the background (saved).
    void setPicture(const std::filesystem::path &picture) noexcept;
    void setUsePicture(bool use) noexcept;
    void setFit(Settings::Fit fit) noexcept;

    /// Stores (or forgets, with nullptr) the grid cell of an item. Saved by saveSettings().
    void setPosition(const std::string &key, const SkIPoint *cell) noexcept;
    void clearPositions() noexcept;
    void saveSettings() noexcept;

    /// Runs a command (argv) detached.
    static bool Spawn(const std::vector<std::string> &argv) noexcept;

    /// Emitted after entries() change (files added/removed/renamed, disks mounted/unmounted).
    CZSignal<> onEntriesChanged;

    /// Emitted after the background settings change.
    CZSignal<> onBackgroundChanged;

    /// Emitted after the icon size or the positions change from outside (settings file edited).
    CZSignal<> onIconsChanged;

private:
    void onScreenPlugged(MScreen &screen) noexcept;
    void onScreenUnplugged(MScreen &screen) noexcept;
    void updateIconsScreen() noexcept;

    void initBus() noexcept;
    void initDesktopDir() noexcept;
    void initWatches() noexcept;
    void scheduleRescan() noexcept;
    void rescan() noexcept;
    std::vector<DeskEntry> scanVolumes() noexcept;
    std::vector<DeskEntry> scanFiles() noexcept;
    void settingsFileChanged() noexcept;

    std::vector<DeskEntry> m_entries;
    std::filesystem::path m_desktopDir;

    Int32 m_inotify { -1 };
    Int32 m_desktopWatch { -1 }, m_configWatch { -1 };
    std::shared_ptr<CZEventSource> m_inotifySource, m_mountsSource;
    std::unique_ptr<CZTimer> m_rescanTimer; // Created once the CZCore exists (MApp)
    std::unique_ptr<CZTimer> m_menuTimer;
    std::shared_ptr<CZBus> m_bus;
};

Desk *GetDesk() noexcept;

#endif // DESK_H
