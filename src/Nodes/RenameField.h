#ifndef RENAMEFIELD_H
#define RENAMEFIELD_H

#include <Core/Types.h>
#include <Core/Desk.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKCoreTextEditor.h>
#include <functional>
#include <string>

/**
 * @brief Edits a file's name in place, like Finder.
 *
 * A rounded box over the name with the name selected (without its extension for files), an
 * AKCoreTextEditor. Enter or clicking elsewhere renames the file (see Desk::rename()), Escape
 * cancels. If the name can't be used (e.g. another file has it), it stays open with the name
 * selected.
 *
 * Three looks: Label, over a desktop item's label (centered, dark on white, see Background), and
 * in a folder window (see FolderWindow) Grid, over an icon's label (centered), and List, over the
 * name of a row (one line). The window ones have its background and a focus ring.
 */
class RenameField final : public AKRoundSolidColor
{
public:
    enum class Style { Label, Grid, List };

    RenameField(AKNode *parent, Style style = Style::Label) noexcept;

    /**
     * @brief Starts editing @p entry's name.
     *
     * @param box Where (coordinates of the parent): the label's box (as wide as the grid cell), or
     *            the list's name column.
     * @param editing Called with true now and false once it ended (active() is false then), e.g.
     *                to hide the name below.
     */
    void begin(const DeskEntry &entry, const SkIRect &box, std::function<void(bool editing)> editing = {}) noexcept;

    /// Renames (if the name changed) and closes. Returns false if it stays open (invalid name).
    bool commit() noexcept;

    /// Closes without renaming.
    void cancel() noexcept;

    bool active() const noexcept { return !m_entry.key.empty(); }
    const std::string &key() const noexcept { return m_entry.key; }

    /// Follows the name (it moved, e.g. the grid changed or the list scrolled): @p box as in begin().
    void place(const SkIRect &box) noexcept;

    AKCoreTextEditor editor { "", this };

    /// Renamed: its new key (e.g. to select it again).
    std::function<void(const std::string &key)> onRenamed;

protected:
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;

private:
    void end() noexcept;
    void updateColors() noexcept;
    Style m_style;
    DeskEntry m_entry; // The one being renamed (none: no key)
    std::function<void(bool)> m_editing;
    bool m_ending { false };
};

#endif // RENAMEFIELD_H
