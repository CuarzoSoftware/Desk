# Desk

<p align="left">
  <a href="https://github.com/CuarzoSoftware/Desk/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-LGPLv2.1-blue.svg" alt="Desk is released under the LGPLv2.1 license." />
  </a>
  <a href="https://github.com/CuarzoSoftware/Desk">
    <img src="https://img.shields.io/badge/version-0.1.0-brightgreen" alt="Current Desk version." />
  </a>
</p>

Desktop for Cuarzo OS, built on Marco (Wayland layer shell). It has two jobs:

- **Background:** a picture or a solid color on every screen. The surface sits on the background
  layer, covers the whole screen and is always fully opaque.
- **Desktop icons:** the files in the desktop folder (`XDG_DESKTOP_DIR`, `~/Desktop` by default)
  and the connected disks, shown on the primary screen like the macOS Finder desktop.

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
