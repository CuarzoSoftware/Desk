#ifndef VIEWPOPUP_H
#define VIEWPOPUP_H

#include <Core/Types.h>
#include <CZ/Marco/Nodes/MPopUpButton.h>

/**
 * @brief The drop-down of a folder window's toolbar that switches how the files are shown.
 *
 * [grid or list icon][up/down chevrons]
 *
 * A pop-up button like the settings' ones (e.g. Sound's Alert Sound), whose menu lists "Grid" and
 * "List", but it shows the chosen one's icon instead of its name. onSelected: 0 grid, 1 list.
 */
class ViewPopUp final : public MPopUpButton
{
public:
    ViewPopUp(AKNode *parent = nullptr) noexcept;

    /// Shows the list (true) or grid one as chosen, without emitting onSelected.
    void setListView(bool list) noexcept;

    AKFontIcon icon { "grid_view", 16, this };

protected:
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;

private:
    void updateIcon() noexcept;
};

#endif // VIEWPOPUP_H
