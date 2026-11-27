#include <Window/InfoWindow.h>
#include <Core/Log.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/CZCore.h>
#include <CZ/Core/CZKeymap.h>
#include <CZ/Core/Events/CZKeyboardKeyEvent.h>
#include <CZ/Core/Events/CZWindowStateEvent.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <CZ/Core/Events/CZLayoutEvent.h>
#include <CZ/Ream/RImage.h>
#include <CZ/XDG/XDGDesktopEntry.h>
#include <CZ/skia/codec/SkCodec.h>
#include <CZ/skia/codec/SkEncodedOrigin.h>
#include <CZ/skia/core/SkStream.h>
#include <linux/input-event-codes.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <spawn.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <thread>

namespace fs = std::filesystem;

extern char **environ;

/// E.g. 102245719 → "102,245,719"
static std::string GroupDigits(UInt64 value) noexcept
{
    std::string digits { std::to_string(value) };

    for (Int64 i = Int64(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(size_t(i), ",");

    return digits;
}

/// E.g. "Wednesday, 20 December 2017 at 20:11"
static std::string FullDate(std::time_t time) noexcept
{
    if (time == 0)
        return "--";

    std::tm when {};
    localtime_r(&time, &when);
    char buf[128];
    std::strftime(buf, sizeof(buf), "%A, %-e %B %Y at %H:%M", &when);
    return buf;
}

static std::string SizeText(UInt64 bytes, UInt64 onDisk) noexcept
{
    return GroupDigits(bytes) + (bytes == 1 ? " byte (" : " bytes (") + Desk::FormatBytes(onDisk) + " on disk)";
}

static bool CtrlHeld() noexcept
{
    auto keymap { CZCore::Get()->keymap() };
    return keymap && (keymap->pressedKeys().contains(KEY_LEFTCTRL) || keymap->pressedKeys().contains(KEY_RIGHTCTRL));
}

/// Read & Write, Read only, Write only, No Access (the choices of a privilege pop-up) for rwx bits.
static size_t PrivilegeIndex(UInt32 bits) noexcept
{
    const bool r { (bits & 4) != 0 }, w { (bits & 2) != 0 };
    return r && w ? 0 : r ? 1 : w ? 2 : 3;
}

InfoWindow::PermissionRow::PermissionRow(AKNode *parent) noexcept : AKContainer(YGFlexDirectionRow, false, parent)
{
    layout().setAlignItems(YGAlignCenter);
    layout().setJustifyContent(YGJustifySpaceBetween);
    layout().setHeight(24.f);
    layout().setFlexShrink(0.f);
    who.setFontSize(AKInfoRow::FontSize);
    who.setTextColor(AKTheme::Label);
    who.setTruncates(true);
    who.layout().setFlexShrink(1.f);
    privilege.label.setFontSize(AKInfoRow::FontSize);
    privilege.layout().setFlexShrink(0.f);
}

InfoWindow::InfoWindow(const DeskEntry &entry) noexcept : m_entry(entry)
{
    layout().setFlexDirection(YGFlexDirectionColumn);
    layout().setPadding(YGEdgeBottom, 4.f);
    setKeyboardFocusable(true);

    const SkRegion empty;
    const SkFontStyle bold { SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant };

    /* Title bar: the window is dragged by it */

    titleBar.userCaps.add(UCWindowMove);
    titleBar.layout().setHeight(TitleBarHeight);
    titleBar.layout().setFlexShrink(0.f);
    titleBar.layout().setAlignItems(YGAlignCenter);
    titleBar.layout().setPadding(YGEdgeHorizontal, 10.f);
    titleBar.layout().setGap(YGGutterColumn, 8.f);
    windowButtons.layout().setFlexShrink(0.f);
    titleBox.setInputRegion(&empty);
    titleBox.layout().setFlex(1.f);
    titleBox.layout().setMinWidth(0.f);
    titleBox.layout().setJustifyContent(YGJustifyCenter);
    title.setFontSize(13.f);
    title.setFontStyle(bold);
    title.setTextColor(AKTheme::WindowFrameText);
    title.setTruncates(true);
    title.layout().setFlexShrink(1.f);
    titleSpacer.setInputRegion(&empty);
    titleSpacer.layout().setFlexShrink(0.f);
    titleSpacer.layout().setWidth(3 * AKTheme::WindowButtonSize.width() + 2 * AKTheme::WindowButtonGap);

    /* Header */

    header.layout().setFlexShrink(0.f);
    header.layout().setAlignItems(YGAlignCenter);
    header.layout().setPadding(YGEdgeHorizontal, Padding);
    header.layout().setPadding(YGEdgeTop, 4.f);
    header.layout().setPadding(YGEdgeBottom, 10.f);
    header.layout().setGap(YGGutterColumn, 8.f);
    icon.layout().setWidth(32.f);
    icon.layout().setHeight(32.f);
    icon.layout().setFlexShrink(0.f);
    headerTexts.layout().setFlex(1.f);
    headerTexts.layout().setMinWidth(0.f);
    headerTexts.layout().setGap(YGGutterRow, 2.f);
    nameRow.layout().setAlignItems(YGAlignCenter);
    nameRow.layout().setJustifyContent(YGJustifySpaceBetween);
    nameRow.layout().setGap(YGGutterColumn, 8.f);
    name.setFontSize(13.f);
    name.setFontStyle(bold);
    name.setTextColor(AKTheme::Label);
    name.setTruncates(true);
    name.layout().setFlexShrink(1.f);
    size.setFontSize(13.f);
    size.setTextColor(AKTheme::SecondaryLabel);
    size.layout().setFlexShrink(0.f);
    modified.setFontSize(11.f);
    modified.setTextColor(AKTheme::SecondaryLabel);

    /* General */

    // Resized by the user: narrower, the values wrap; shorter, the sections scroll
    layout().setWidth(DefaultWidth);
    setMinSize(SkISize::Make(MinWidth, MinHeight));
    scroll.layout().setFlex(1.f);
    scroll.layout().setMinHeight(0.f);
    sections.layout().setFlexShrink(0.f);

    for (AKInfoRow *row : { &kindRow, &sizeRow, &whereRow, &createdRow, &modifiedRow })
        row->setLabelWidth(LabelWidth);

    // Under the values
    lockedRow.layout().setFlexShrink(0.f);
    lockedRow.layout().setPadding(YGEdgeLeft, LabelWidth + AKInfoRow::Gap);
    lockedRow.layout().setPadding(YGEdgeTop, 5.f);
    locked.label.setFontSize(AKInfoRow::FontSize);
    locked.onToggled.subscribe(this, [this](bool on){ setLocked(on); });

    /* Name & Extension: Enter renames, Escape (or leaving it) shows the current name again */

    nameInput.layout().setMargin(YGEdgeTop, 2.f);
    nameInput.onSubmit.subscribe(this, [this](const std::string &text){ rename(text); });
    nameInput.onCancel.subscribe(this, [this]{ nameInput.setText(m_entry.path.filename().string()); });

    /* Open with */

    appBox.layout().setFlexShrink(0.f);
    appBox.layout().setMargin(YGEdgeTop, 2.f);
    appPopUp.layout().setJustifyContent(YGJustifySpaceBetween);
    appPopUp.label.setFontSize(AKInfoRow::FontSize);
    appPopUp.onSelected.subscribe(this, [this](size_t index){ changeAll.setEnabled(index != 0); });

    skia::textlayout::TextStyle hint { theme()->DefaultTextStyle };
    hint.setFontSize(11.f);
    hint.resetShadows();

    for (AKCoreTextEditor *text : { &changeAllHint, &access })
    {
        text->setTextStyle(hint);
        text->setTextColor(AKTheme::SecondaryLabel);
        text->setSelectable(false);
        text->setInputRegion(&empty);
    }

    changeAllHint.layout().setMargin(YGEdgeTop, 6.f);
    changeAllRow.layout().setFlexShrink(0.f);
    changeAllRow.layout().setMargin(YGEdgeTop, 6.f);
    changeAll.layout().setHeight(24.f);
    changeAll.layout().setMinWidth(96.f);
    changeAll.textNode().setFontSize(12.f);
    changeAll.setBackgroundColor(AKTheme::ControlFill);
    changeAll.setContentColor(AKTheme::ControlText);
    changeAll.setAutoContentColor(false);
    changeAll.onClick.subscribe(this, [this](const auto &){ applyDefaultApp(); });

    /* Preview */

    previewImage.setInputRegion(&empty);
    previewImage.layout().setHeight(PreviewSize);
    previewImage.layout().setFlexShrink(0.f);
    previewImage.layout().setMargin(YGEdgeTop, 4.f);
    previewImage.setSizeMode(AKImageFrame::SizeMode::Contain);

    /* Sharing & Permissions */

    permissionsHeader.layout().setFlexShrink(0.f);
    permissionsHeader.layout().setJustifyContent(YGJustifySpaceBetween);
    permissionsHeader.layout().setMargin(YGEdgeTop, 8.f);
    permissionsHeader.layout().setPadding(YGEdgeRight, 8.f);

    for (AKText *text : { &permissionsName, &permissionsPrivilege })
    {
        text->setFontSize(11.f);
        text->setTextColor(AKTheme::SecondaryLabel);
    }

    owner.privilege.onSelected.subscribe(this, [this](size_t choice){ setPrivilege(6, choice); });
    group.privilege.onSelected.subscribe(this, [this](size_t choice){ setPrivilege(3, choice); });
    everyone.privilege.onSelected.subscribe(this, [this](size_t choice){ setPrivilege(0, choice); });

    // Collapsed or expanded as they were left, like Finder (see Settings::infoCollapsed). The
    // window follows the sections shown
    for (const auto &[section, id] : { std::pair { &general, "General" }, std::pair { &moreInfo, "MoreInfo" },
                                       std::pair { &nameSection, "Name" }, std::pair { &openWith, "OpenWith" },
                                       std::pair { &preview, "Preview" }, std::pair { &permissions, "Permissions" } })
    {
        section->setExpanded(!GetDesk()->settings.infoCollapsed.contains(id));
        section->onToggled.subscribe(this, [this, id = std::string(id)](bool expanded){
            auto *desk { GetDesk() };

            if (expanded)
                desk->settings.infoCollapsed.erase(id);
            else
                desk->settings.infoCollapsed.insert(id);

            desk->saveSettings();
            fit();
        });
    }

    // A picture's thumbnail, decoded in the background (see updateImages())
    GetDesk()->assets.onThumbnailReady.subscribe(this, [this](const fs::path &path){
        if (m_waitingThumbnail.empty() || path != m_waitingThumbnail)
            return;

        m_waitingThumbnail.clear();
        updateImages();
    });

    m_sizeTimer.setCallback([this](CZTimer*){ showSize(); });

    updateColors();
    applyWidth(m_width);
    refresh();
}

InfoWindow::~InfoWindow() noexcept
{
    if (m_folderSize)
        m_folderSize->cancelled = true;

    if (!m_waitingThumbnail.empty() && GetDesk())
        GetDesk()->assets.cancelThumbnail(m_waitingThumbnail);
}

bool InfoWindow::isFolder() const noexcept
{
    return isVolume() || (!m_entry.launcher && m_entry.mimeType == "inode/directory");
}

void InfoWindow::refresh() noexcept
{
    auto *desk { GetDesk() };
    const fs::path &path { m_entry.path };

    // The file a link points to, or the link itself if it's broken
    struct statx st {};
    const UInt32 mask { STATX_BASIC_STATS | STATX_BTIME };

    if (statx(AT_FDCWD, path.c_str(), 0, mask, &st) != 0 && statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, mask, &st) != 0)
    {
        Log(CZInfo, CZLN, "{} is gone ({}): closing its info window", path.string(), strerror(errno));
        setMapped(false); // Destroyed by Desk
        return;
    }

    m_mode = st.stx_mode;
    m_owned = geteuid() == 0 || st.stx_uid == geteuid();

    const std::string fileName { isVolume() || path.filename().empty() ? m_entry.name : path.filename().string() };
    const std::string shownName { m_entry.name.empty() ? fileName : m_entry.name };
    setTitle(shownName + " Info");
    title.setText(shownName + " Info");
    name.setText(shownName);
    modified.setText("Modified: " + Desk::FormatDate(st.stx_mtime.tv_sec));

    /* General */

    std::string kind { m_entry.kindName };

    if (isVolume())
        kind = "Volume";
    else if (m_entry.launcher)
        kind = "Application";
    else if (kind.empty())
        kind = m_entry.mimeType == "inode/directory" ? "Folder" : desk->xdgkit->mimeDatabase().description(m_entry.mimeType);

    // Like the list's Kind column
    if (!kind.empty() && kind.front() >= 'a' && kind.front() <= 'z')
        kind.front() = char(kind.front() - 'a' + 'A');

    kindRow.setValue(kind.empty() ? m_entry.mimeType : kind);
    whereRow.setValue(isVolume() ? path.string() : path.parent_path().string());
    createdRow.setValue(FullDate((st.stx_mask & STATX_BTIME) ? st.stx_btime.tv_sec : 0));
    modifiedRow.setValue(FullDate(st.stx_mtime.tv_sec));
    locked.setChecked((m_mode & 0222) == 0);
    locked.setEnabled(m_owned);
    lockedRow.setVisible(!isVolume());

    /* More Info */

    size_t infoCount { 0 };
    const auto addInfo = [this, &infoCount](const std::string &label, const std::string &value){
        if (value.empty())
            return;

        if (infoCount == m_infoRows.size())
        {
            auto &row { m_infoRows.emplace_back(std::make_unique<AKInfoRow>("", "", &moreInfo.content)) };
            row->setLabelWidth(LabelWidth);
            row->setValueWidth(m_width - 2.f * Padding - LabelWidth - AKInfoRow::Gap);
        }

        m_infoRows[infoCount]->setLabel(label);
        m_infoRows[infoCount]->setValue(value);
        infoCount++;
    };

    if (isVolume())
    {
        struct statvfs vfs {};

        if (statvfs(path.c_str(), &vfs) == 0)
        {
            const UInt64 capacity { UInt64(vfs.f_blocks) * vfs.f_frsize };
            const UInt64 available { UInt64(vfs.f_bavail) * vfs.f_frsize };
            const UInt64 free { UInt64(vfs.f_bfree) * vfs.f_frsize };
            m_bytes = m_onDisk = capacity;
            size.setText(Desk::FormatBytes(capacity));
            sizeRow.setLabel("Capacity:");
            sizeRow.setValue(GroupDigits(capacity) + " bytes (" + Desk::FormatBytes(capacity) + ")");
            addInfo("Available:", Desk::FormatBytes(available));
            addInfo("Used:", Desk::FormatBytes(capacity > free ? capacity - free : 0));
        }

        addInfo("Format:", m_entry.fsType);
        addInfo("Device:", m_entry.device);
    }
    else if (isFolder())
    {
        // Added up in the background (again if it was done: it may have changed), see showSize()
        if (!m_folderSize || m_folderSize->done)
            startFolderSize();

        showSize();
    }
    else
    {
        if (m_folderSize)
            m_folderSize->cancelled = true;

        m_folderSize.reset();
        m_sizeTimer.stop();
        m_bytes = st.stx_size;
        m_onDisk = UInt64(st.stx_blocks) * 512;
        sizeRow.setLabel("Size:");
        showSize();
    }

    if (!isVolume())
    {
        addInfo("Last opened:", FullDate(st.stx_atime.tv_sec));

        // Pictures: read from their header
        if (!isFolder() && (Assets::HasThumbnail(m_entry.mimeType)))
            if (auto stream { SkStream::MakeFromFile(path.c_str()) })
                if (auto codec { SkCodec::MakeFromStream(std::move(stream)) })
                {
                    SkISize dimensions { codec->dimensions() };

                    if (SkEncodedOriginSwapsWidthHeight(codec->getOrigin()))
                        dimensions = SkISize::Make(dimensions.height(), dimensions.width());

                    addInfo("Dimensions:", std::to_string(dimensions.width()) + " × " + std::to_string(dimensions.height()));
                }

        std::error_code ec;

        if (fs::is_symlink(path, ec))
            addInfo("Original:", fs::read_symlink(path, ec).string());

        if (m_entry.launcher)
            addInfo("Command:", m_entry.launcher->exec);
    }

    m_infoRows.resize(infoCount);
    moreInfo.setVisible(infoCount > 0);

    /* Name & Extension */

    nameSection.setVisible(m_entry.kind == DeskEntry::Kind::File);

    if (!nameInput.editor.editing())
        nameInput.setText(fileName);

    updateOpenWith();
    updateImages();
    updatePermissions();
    fit();
}

/* ------------------------------------------ Size ------------------------------------------ */

void InfoWindow::startFolderSize() noexcept
{
    if (m_folderSize)
        m_folderSize->cancelled = true;

    m_folderSize = std::make_shared<FolderSize>();

    // Detached: a slow disk never keeps the window (or the app) from closing
    std::thread([state = m_folderSize, dir = m_entry.path]{
        std::error_code ec;
        fs::recursive_directory_iterator it { dir, fs::directory_options::skip_permission_denied, ec };
        const fs::recursive_directory_iterator end;

        while (!ec && it != end && !state->cancelled)
        {
            struct stat st {};

            // Links aren't followed: they count as themselves
            if (lstat(it->path().c_str(), &st) == 0)
            {
                state->items++;
                state->onDisk += UInt64(st.st_blocks) * 512;

                if (!S_ISDIR(st.st_mode))
                    state->bytes += UInt64(st.st_size);
            }

            it.increment(ec);
        }

        state->done = true;
    }).detach();

    m_sizeTimer.start(150);
}

void InfoWindow::showSize() noexcept
{
    if (!m_folderSize)
    {
        size.setText(Desk::FormatBytes(m_bytes));
        sizeRow.setValue(SizeText(m_bytes, m_onDisk));
        return;
    }

    const UInt64 bytes { m_folderSize->bytes }, onDisk { m_folderSize->onDisk }, items { m_folderSize->items };
    sizeRow.setLabel("Size:");

    if (m_folderSize->done)
    {
        m_sizeKnown = true;
        m_bytes = bytes;
        m_onDisk = onDisk;
        size.setText(Desk::FormatBytes(bytes));
        sizeRow.setValue(SizeText(bytes, onDisk) + " for " + GroupDigits(items) + (items == 1 ? " item" : " items"));
        fit(); // The value may wrap now
        return;
    }

    m_sizeTimer.start(150);

    // Added up again (the window was activated): the previous total stays meanwhile
    if (m_sizeKnown)
        return;

    // Growing meanwhile, like Finder
    size.setText(bytes == 0 ? "--" : Desk::FormatBytes(bytes));
    sizeRow.setValue(bytes == 0 ? "Calculating…" : "Calculating… (" + Desk::FormatBytes(bytes) + " so far)");
}

/* ----------------------------------------- Images ----------------------------------------- */

void InfoWindow::updateImages() noexcept
{
    auto &assets { GetDesk()->assets };
    std::shared_ptr<RImage> thumbnail;

    if (m_entry.kind == DeskEntry::Kind::File && !m_entry.launcher && Assets::HasThumbnail(m_entry.mimeType))
    {
        // Another file now (renamed): the previous one isn't needed
        if (!m_waitingThumbnail.empty() && m_waitingThumbnail != m_entry.path)
        {
            assets.cancelThumbnail(m_waitingThumbnail);
            m_waitingThumbnail.clear();
        }

        // Big enough for the preview on HiDPI screens. Not ready: shown once decoded (see the constructor)
        if (m_waitingThumbnail.empty() && !(thumbnail = assets.thumbnail(m_entry.path, PreviewSize * 2)))
            m_waitingThumbnail = m_entry.path;
    }

    const auto themed { assets.icon(m_entry) };
    icon.setImage(thumbnail ? thumbnail : themed);

    if (thumbnail)
        previewImage.setImage(thumbnail);
    else if (themed)
        // Rendered for the size it's drawn at
        previewImage.setImageProvider([themed](Int32 pixels, Int32 scale){ return GetDesk()->assets.sized(themed, pixels, scale); });
    else
        previewImage.setImage(nullptr);

    preview.setVisible(thumbnail || themed);
}

/* ---------------------------------------- Open with ---------------------------------------- */

void InfoWindow::updateOpenWith() noexcept
{
    std::vector<std::shared_ptr<const XDGDesktopEntry>> apps;

    if (m_entry.kind == DeskEntry::Kind::File && !isFolder() && !m_entry.launcher)
    {
        const auto all { GetDesk()->appsFor(m_entry) };

        // The default one first. Those hidden from menus can still be it
        for (size_t i = 0; i < all.size(); i++)
            if (i == 0 || !all[i]->noDisplay)
                apps.emplace_back(all[i]);
    }

    openWith.setVisible(!apps.empty());

    // The same ones (e.g. just activated again): the app chosen stays chosen
    const bool same { apps.size() == m_apps.size() &&
        std::equal(apps.begin(), apps.end(), m_apps.begin(), [](const auto &a, const auto &b){ return a->id == b->id; }) };

    if (same)
        return;

    m_apps = std::move(apps);
    std::vector<std::string> names;

    for (size_t i = 0; i < m_apps.size(); i++)
        names.emplace_back(i == 0 ? m_apps[i]->name + " (default)" : m_apps[i]->name);

    appPopUp.setItems(names, 0);
    changeAll.setEnabled(false);
}

void InfoWindow::applyDefaultApp() noexcept
{
    const size_t index { appPopUp.selectedIndex() };

    if (index == 0 || index >= m_apps.size())
        return;

    const std::string desktopFile { m_apps[index]->id + ".desktop" };
    const char *argv[] { "xdg-mime", "default", desktopFile.c_str(), m_entry.mimeType.c_str(), nullptr };
    pid_t pid { 0 };
    int status { -1 };

    // Waited for (it's quick): the apps are read again right after
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, const_cast<char *const *>(argv), environ) != 0 ||
        waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        Log(CZError, CZLN, "Can't make {} the default app for {} (xdg-mime failed)", desktopFile, m_entry.mimeType);
        return;
    }

    Log(CZInfo, CZLN, "{} is now the default app for {}", desktopFile, m_entry.mimeType);
    GetDesk()->xdgkit->mimeDatabase().reload();
    m_apps.clear();
    updateOpenWith();
    GetDesk()->selectionChanged(); // The menus' Open With lists
}

/* --------------------------------------- Permissions --------------------------------------- */

void InfoWindow::updatePermissions() noexcept
{
    const bool r { ::access(m_entry.path.c_str(), R_OK) == 0 }, w { ::access(m_entry.path.c_str(), W_OK) == 0 };
    access.setText(r && w ? "You can read and write" : r ? "You can only read" : w ? "You can only write" : "You have no access");

    struct stat st {};

    if (stat(m_entry.path.c_str(), &st) != 0 && lstat(m_entry.path.c_str(), &st) != 0)
        return;

    const passwd *user { getpwuid(st.st_uid) };
    const struct group *grp { getgrgid(st.st_gid) };
    owner.who.setText((user ? std::string(user->pw_name) : std::to_string(st.st_uid)) + (st.st_uid == geteuid() ? " (Me)" : ""));
    group.who.setText(grp ? std::string(grp->gr_name) : std::to_string(st.st_gid));
    everyone.who.setText("everyone");

    const std::vector<std::string> choices { "Read & Write", "Read only", isFolder() ? "Write only (Drop Box)" : "Write only", "No Access" };

    for (const auto &[row, shift] : { std::pair { &owner, 6 }, std::pair { &group, 3 }, std::pair { &everyone, 0 } })
    {
        row->privilege.setItems(choices, PrivilegeIndex((m_mode >> shift) & 7));
        row->privilege.setEnabled(m_owned);
    }
}

void InfoWindow::setPrivilege(int shift, size_t choice) noexcept
{
    const bool r { choice == 0 || choice == 1 }, w { choice == 0 || choice == 2 };
    const UInt32 current { (m_mode >> shift) & 7 };

    // Folders are entered (x) by whoever can read or write them. Files keep theirs
    const UInt32 x { S_ISDIR(m_mode) ? UInt32(r || w) : (current & 1) };
    const UInt32 bits { (r ? 4u : 0u) | (w ? 2u : 0u) | x };
    const mode_t mode { mode_t(((m_mode & 07777) & ~(7u << shift)) | (bits << shift)) };

    if (chmod(m_entry.path.c_str(), mode) != 0)
        Log(CZError, CZLN, "Can't change the permissions of {}: {}", m_entry.path.string(), strerror(errno));

    refresh();
}

void InfoWindow::setLocked(bool on) noexcept
{
    const mode_t mode { mode_t(on ? (m_mode & 07777 & ~0222u) : ((m_mode & 07777) | 0200u)) };

    if (chmod(m_entry.path.c_str(), mode) != 0)
        Log(CZError, CZLN, "Can't {} {}: {}", on ? "lock" : "unlock", m_entry.path.string(), strerror(errno));

    refresh();
}

/* ------------------------------------------ Name ------------------------------------------ */

void InfoWindow::rename(const std::string &newName) noexcept
{
    auto *desk { GetDesk() };
    const std::string current { m_entry.path.filename().string() };

    if (newName != current)
    {
        // Desktop files keep their place (see Desk::rename()): keyed by their file names
        DeskEntry file { m_entry };
        file.key = current;

        if (desk->rename(file, newName).empty())
        {
            nameInput.setText(current);
            return;
        }

        m_entry = Desk::EntryFor(*desk->xdgkit, fs::directory_entry(m_entry.path.parent_path() / newName));

        // A folder: added up again at its new path
        if (m_folderSize)
            m_folderSize->cancelled = true;

        m_folderSize.reset();
        m_sizeKnown = false;
    }

    refresh();
}

/* ----------------------------------------- Window ----------------------------------------- */

void InfoWindow::fit() noexcept
{
    // Maximized or fullscreen: the compositor chose its size (fitted again once it's restored
    // and its content changes)
    if (maximized() || fullscreen())
        return;

    // The sections' own height (in the scroll, they're never squeezed)
    rootNode()->layout().calculate();
    const Int32 content { Int32(std::ceil(TitleBarHeight + header.layout().calculatedHeight() +
                                          sections.layout().calculatedHeight() + 4.f)) };

    if (content == m_fitHeight)
        return;

    m_fitHeight = content;

    // Not taller than the room the compositor gives windows (scrolled then)
    const Int32 room { suggestedBounds().height() };
    layout().setHeight(std::max(MinHeight, room > 0 ? std::min(content, room) : content));
}

void InfoWindow::applyWidth(SkScalar width) noexcept
{
    m_width = width;
    const SkScalar valueWidth { width - 2.f * Padding - LabelWidth - AKInfoRow::Gap };

    for (AKInfoRow *row : { &kindRow, &sizeRow, &whereRow, &createdRow, &modifiedRow })
        row->setValueWidth(valueWidth);

    for (auto &row : m_infoRows)
        row->setValueWidth(valueWidth);

    changeAllHint.setWrapWidth(width - 2.f * Padding);
    access.setWrapWidth(width - 2.f * Padding);
}

void InfoWindow::layoutEvent(const CZLayoutEvent &e)
{
    MToplevel::layoutEvent(e);

    // Resized: the texts wrap at the new width (the height stays as the user left it)
    if (const SkScalar width { layout().calculatedWidth() }; e.changes.has(CZLayoutChangeSize) && width >= MinWidth && width != m_width)
        applyWidth(width);
}

void InfoWindow::updateColors() noexcept
{
    appBox.setBackgroundColor(AKTheme::ControlFill.resolve(colorScheme()));
}

void InfoWindow::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    MToplevel::colorSchemeEvent(e);
    updateColors();
}

void InfoWindow::windowStateEvent(const CZWindowStateEvent &e)
{
    MToplevel::windowStateEvent(e);

    // Back to it: the file may have changed meanwhile
    if (e.changes.has(CZWinActivated) && e.newState.has(CZWinActivated))
        refresh();
}

void InfoWindow::keyboardKeyEvent(const CZKeyboardKeyEvent &e)
{
    MToplevel::keyboardKeyEvent(e);

    if (e.isPressed && e.code == KEY_W && CtrlHeld())
        setMapped(false); // Destroyed by Desk
}
