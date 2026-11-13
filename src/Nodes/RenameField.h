#ifndef RENAMEFIELD_H
#define RENAMEFIELD_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKCoreTextEditor.h>
#include <CZ/Core/CZWeak.h>
#include <functional>
#include <string>

class DeskItem;

/**
 * @brief Edits a desktop item's name in place, like Finder.
 *
 * A white rounded box over the item's label with the name selected (without its extension for
 * files). Enter or clicking elsewhere renames the file (see Desk::rename()), Escape cancels. If the
 * name can't be used (e.g. another file has it), it stays open with the name selected.
 *
 * It lives over every item (drawn above them, see Background), positioned over the label.
 */
class RenameField final : public AKRoundSolidColor
{
public:
    RenameField(AKNode *parent) noexcept;

    /// Starts editing @p item's name (its label is hidden meanwhile). @p label: the label's box
    /// (surface coordinates, as wide as the grid cell).
    void begin(DeskItem &item, const SkIRect &label) noexcept;

    /// Renames (if the name changed) and closes. Returns false if it stays open (invalid name).
    bool commit() noexcept;

    /// Closes without renaming.
    void cancel() noexcept;

    bool active() const noexcept { return !m_key.empty(); }
    const std::string &key() const noexcept { return m_key; }

    /// Follows the item (it moved, e.g. the grid changed): @p label as in begin().
    void place(const SkIRect &label) noexcept;

    AKCoreTextEditor editor { "", this };

    /// Renamed: its new key (e.g. to select it again, its item is new).
    std::function<void(const std::string &key)> onRenamed;

private:
    void end() noexcept;
    std::string m_key;
    CZWeak<DeskItem> m_item;
    bool m_ending { false };
};

#endif // RENAMEFIELD_H
