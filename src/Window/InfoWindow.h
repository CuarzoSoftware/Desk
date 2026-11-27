#ifndef INFOWINDOW_H
#define INFOWINDOW_H

#include <Core/Types.h>
#include <Core/Desk.h>
#include <Nodes/IconView.h>
#include <CZ/Marco/Roles/MToplevel.h>
#include <CZ/Marco/Nodes/MPopUpButton.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKScroll.h>
#include <CZ/AK/Nodes/AKWindowButtonGroup.h>
#include <CZ/AK/Nodes/AKDisclosureSection.h>
#include <CZ/AK/Nodes/AKInfoRow.h>
#include <CZ/AK/Nodes/AKCheckBox.h>
#include <CZ/AK/Nodes/AKTextInput.h>
#include <CZ/AK/Nodes/AKButton.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKCoreTextEditor.h>
#include <CZ/AK/Nodes/AKText.h>
#include <CZ/Core/CZTimer.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief The info of a file, folder or disk in a small window, like Finder's Get Info (⌘I).
 *
 * [title bar: window buttons, "<name> Info"]
 * [header: icon, name (bold), size / "Modified: ..."]
 * [sections, each one collapsed or expanded by clicking its title (AKDisclosureSection), which is
 *  remembered for the next windows]
 *
 * - **General**: kind, size (folders: everything inside, added up in the background), where,
 *   created, modified, and Locked (no one can write it: its write permissions are removed).
 * - **More Info**: last opened, a picture's dimensions, a link's original, a launcher's command, a
 *   disk's format, capacity and free space.
 * - **Name & Extension**: the file name, renamed with Enter (see Desk::rename()).
 * - **Open with**: the app that opens it, chosen among those that can; Change All makes it the
 *   default one for every file of its type (`xdg-mime default`).
 * - **Preview**: its picture (or icon), big.
 * - **Sharing & Permissions**: what the owner, the group and everyone else can do, changed by the
 *   owner (chmod).
 *
 * It opens as wide as Finder's and as tall as its content, and grows or shrinks with the sections
 * shown. The user can resize it: the values wrap at its width, and the sections scroll when it's
 * shorter than them. The values can be selected and copied. Read again whenever it's activated; ⌘W closes it.
 */
class InfoWindow final : public MToplevel
{
public:
    InfoWindow(const DeskEntry &entry) noexcept;
    ~InfoWindow() noexcept;

    static constexpr Int32 DefaultWidth    { 300 };
    static constexpr Int32 MinWidth        { 260 };
    static constexpr Int32 MinHeight       { 160 };
    static constexpr Int32 TitleBarHeight  { 28 };
    static constexpr SkScalar Padding      { AKDisclosureSection::ContentPadding };
    static constexpr SkScalar LabelWidth   { 70.f };
    static constexpr Int32 PreviewSize     { 160 };

    /// The file shown (it changes when it's renamed here).
    const std::filesystem::path &path() const noexcept { return m_entry.path; }

    /// Reads the file again and shows it.
    void refresh() noexcept;

    /* Title bar */
    AKContainer titleBar { YGFlexDirectionRow, false, this };
    AKWindowButtonGroup windowButtons { &titleBar };
    AKContainer titleBox { YGFlexDirectionRow, false, &titleBar };
    AKText title { "", &titleBox };
    AKContainer titleSpacer { YGFlexDirectionRow, false, &titleBar }; // As wide as the buttons: the title stays centered

    /* Header */
    AKContainer header { YGFlexDirectionRow, false, this };
    IconView icon { &header, Assets::WindowShadow };
    AKContainer headerTexts { YGFlexDirectionColumn, false, &header };
    AKContainer nameRow { YGFlexDirectionRow, false, &headerTexts };
    AKText name { "", &nameRow };
    AKText size { "", &nameRow };
    AKText modified { "", &headerTexts };

    /* The sections, scrolled when the window is shorter than them */
    AKScroll scroll { this };
    AKContainer sections { YGFlexDirectionColumn, false, &scroll };

    /* General */
    AKDisclosureSection general { "General:", true, &sections };
    AKInfoRow kindRow { "Kind:", "", &general.content };
    AKInfoRow sizeRow { "Size:", "", &general.content };
    AKInfoRow whereRow { "Where:", "", &general.content };
    AKInfoRow createdRow { "Created:", "", &general.content };
    AKInfoRow modifiedRow { "Modified:", "", &general.content };
    AKContainer lockedRow { YGFlexDirectionRow, false, &general.content };
    AKCheckBox locked { "Locked", false, &lockedRow };

    /* More Info (its rows depend on the file: see refresh()) */
    AKDisclosureSection moreInfo { "More Info:", true, &sections };

    /* Name & Extension */
    AKDisclosureSection nameSection { "Name & Extension:", true, &sections };
    AKTextInput nameInput { "", &nameSection.content };

    /* Open with */
    AKDisclosureSection openWith { "Open with:", true, &sections };
    AKRoundSolidColor appBox { 0, CZBorderRadius::Make(6), &openWith.content };
    MPopUpButton appPopUp { {}, &appBox };
    AKCoreTextEditor changeAllHint { "Use this application to open all documents like this one.", &openWith.content };
    AKContainer changeAllRow { YGFlexDirectionRow, false, &openWith.content };
    AKButton changeAll { "Change All…", &changeAllRow };

    /* Preview */
    AKDisclosureSection preview { "Preview:", false, &sections };
    AKImageFrame previewImage { &preview.content };

    /* Sharing & Permissions */
    AKDisclosureSection permissions { "Sharing & Permissions:", false, &sections };
    AKCoreTextEditor access { "", &permissions.content };

    struct PermissionRow : public AKContainer
    {
        PermissionRow(AKNode *parent) noexcept;
        AKText who { "", this };
        MPopUpButton privilege { {}, this };
    };

    AKContainer permissionsHeader { YGFlexDirectionRow, false, &permissions.content };
    AKText permissionsName { "Name", &permissionsHeader };
    AKText permissionsPrivilege { "Privilege", &permissionsHeader };
    PermissionRow owner { &permissions.content };
    PermissionRow group { &permissions.content };
    PermissionRow everyone { &permissions.content };

protected:
    void windowStateEvent(const CZWindowStateEvent &e) override;
    void keyboardKeyEvent(const CZKeyboardKeyEvent &e) override;
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;
    void layoutEvent(const CZLayoutEvent &e) override;

private:
    /// As tall as its content, if its height changed since the last time (a section was toggled,
    /// the rows changed): a height chosen by the user is kept otherwise.
    void fit() noexcept;

    /// The texts wrap at the window's width @p width.
    void applyWidth(SkScalar width) noexcept;
    SkScalar m_width { DefaultWidth };
    Int32 m_fitHeight { 0 }; // The content's height at the last fit()
    void updateColors() noexcept;

    bool isFolder() const noexcept;
    bool isVolume() const noexcept { return m_entry.kind == DeskEntry::Kind::Volume; }

    void updateImages() noexcept;
    void updateOpenWith() noexcept;
    void updatePermissions() noexcept;

    /// The name typed in Name & Extension: renamed if possible, else the field shows the current one.
    void rename(const std::string &name) noexcept;

    /// The app chosen in Open with becomes the default one for the file's type.
    void applyDefaultApp() noexcept;

    /// The privilege chosen for the owner (shift 6), the group (3) or everyone (0).
    void setPrivilege(int shift, size_t choice) noexcept;
    void setLocked(bool locked) noexcept;

    /* A folder's size: everything inside added up by a thread, shown as it grows */
    struct FolderSize
    {
        std::atomic<UInt64> bytes { 0 }, onDisk { 0 }, items { 0 };
        std::atomic<bool> done { false }, cancelled { false };
    };

    void startFolderSize() noexcept;
    void showSize() noexcept;
    std::shared_ptr<FolderSize> m_folderSize;
    CZTimer m_sizeTimer;

    DeskEntry m_entry;
    UInt32 m_mode { 0 };       // Its permissions (st_mode), as last read
    bool m_owned { false };    // The user can change them (its owner, or root)
    UInt64 m_bytes { 0 }, m_onDisk { 0 };
    bool m_sizeKnown { false }; // A folder's size was added up at least once
    std::vector<std::unique_ptr<AKInfoRow>> m_infoRows;
    std::vector<std::shared_ptr<const XDGDesktopEntry>> m_apps; // Those of appPopUp, the default one first
    std::filesystem::path m_waitingThumbnail; // Shown once decoded (see Assets::thumbnail())
};

#endif // INFOWINDOW_H
