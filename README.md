# Desk

<p align="left">
  <a href="https://github.com/CuarzoSoftware/Desk/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-LGPLv2.1-blue.svg" alt="Desk is released under the LGPLv2.1 license." />
  </a>
  <a href="https://github.com/CuarzoSoftware/Desk">
    <img src="https://img.shields.io/badge/version-0.1.0-brightgreen" alt="Current Desk version." />
  </a>
</p>

> This project is part of the [Cuarzo Framework](https://github.com/CuarzoSoftware/Cuarzo). Follow the instructions there to build and install it.

Desktop for Cuarzo OS, built on Marco (Wayland layer shell). It has three jobs:

- **Background:** a picture or a solid color on every screen. The surface sits on the background
  layer, covers the whole screen and is always fully opaque.
- **Desktop icons:** the files in the desktop folder (`XDG_DESKTOP_DIR`, `~/Desktop` by default)
  and the connected disks, shown on the primary screen like the macOS Finder desktop.
- **Folder windows:** opening a folder or a disk shows its files in a window, like a Finder window.

## Desktop icons

- Icons fill a grid column by column from the top-right corner. Disks come first, then the files
  sorted by name.
- Pictures show thumbnails. Launchers (`.desktop` files) show their app's name and icon.
- Select icons with a click, Ctrl/Shift+click or a rubber band. Ctrl+A selects all and Escape
  clears the selection.
- Double click (or Enter) opens the selection with its default apps. Defaults come from
  `mimeapps.list` and the installed apps' MIME types. For example, a photo opens in the photo viewer.
- Drag icons to another cell to move them. Positions are remembered. Drag them onto other apps to
  hand them the files.
- Right click an icon for Open, Open With, Set Desktop Picture, Move to Trash or Eject.
- Right click the desktop for New Folder, the background options, Clean Up or Open Desktop Folder.
- The desktop refreshes when the folder changes or a disk is mounted or unmounted.

## Folder windows

Folders and disks opened from the desktop, the dock's Desk tile or the Go menu open in a window:

- A sidebar with **Favorites** (home, Desktop, Documents, Downloads, Pictures, Music, Videos) and
  **Locations** (the system disk and the mounted disks; removable ones have an eject button).
- The files as icons (pictures show thumbnails), with the number of items and the free space below.
- Double click a folder to open it in the same window (Ctrl+double click: a new window). Other files
  open with their default apps. Back and forward go through the folders shown before.
- Right click for Open, Open With, Set Desktop Picture, Move to Trash, New Folder or Show Hidden Files.
- Keyboard (⌘ = Ctrl): arrows select, Enter or ⌘↓ opens, ⌘↑ or Backspace goes to the enclosing
  folder, ⌘[ / ⌘] (or Alt+←/→) go back and forward, ⌘A selects all, Delete or ⌘⌫ moves to the trash,
  ⇧⌘N creates a folder, ⌘H shows hidden files, ⌘N opens another window, ⌘W closes it. ⇧⌘H, ⇧⌘D,
  ⇧⌘O, ⇧⌘L and ⇧⌘C go to Home, Desktop, Documents, Downloads and the system disk.
- The window follows the folder's changes. If the folder goes away (deleted, its disk ejected), it
  shows the closest folder that still exists.

## Get Info

⌘I (Ctrl+I), **File > Get Info** or the right click menu shows the info of the selected files,
folders or disks, each in its own window, like Finder's. With nothing selected it's the folder
shown (or the desktop folder). Other apps can ask for it too
(`org.freedesktop.FileManager1.ShowItemProperties`).

- **General:** kind, size (folders are added up in the background), where, created and modified.
  **Locked** removes its write permissions.
- **More Info:** last opened, a picture's dimensions, a link's original, a disk's format and space.
- **Name & Extension:** type another name and press Enter to rename it.
- **Open with:** choose an app and click **Change All…** to make it the default for every file of
  that type.
- **Preview** and **Sharing & Permissions** (what the owner, the group and everyone can do).

Click a section's title to collapse or expand it: the window remembers them (`[Info] Collapsed` in
the settings).

## Settings

Settings are stored in `$XDG_CONFIG_HOME/cz-desk/desk.conf` (`~/.config/cz-desk/desk.conf` by
default). The file is watched, so edits apply right away.

```ini
[Background]
Picture=/home/user/Pictures/beach.jpg
UsePicture=true
Fit=Cover
Color=#1F4E79

[Icons]
Size=64
```

- `Fit` sets how the picture fills the screen:
  - `Cover` fills the screen and crops what doesn't fit.
  - `Contain` shows the whole picture, with the color around it.
  - `Fill` stretches the picture.
- `Color` is the background color. It also shows around a `Contain` picture.

The `[Positions]` section holds the cells of the icons you moved. Desk writes it.

## Requirements

- A compositor with `wlr-layer-shell`.
- `shared-mime-info`, for file types.
- `udisksctl`, to eject disks.

## License

Released under the GNU Lesser General Public License v2.1. See [LICENSE](LICENSE).
