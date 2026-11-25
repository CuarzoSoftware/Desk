#include <Nodes/ViewPopUp.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>

ViewPopUp::ViewPopUp(AKNode *parent) noexcept :
    MPopUpButton({ "Grid", "List" }, parent)
{
    // The icon in place of the text
    label.setVisible(false);
    icon.insertBefore(&chevron); // Added last: before the chevron
    const SkRegion empty;
    icon.setInputRegion(&empty);

    onSelected.subscribe(this, [this](size_t){ updateIcon(); });
    updateIcon();
}

void ViewPopUp::setListView(bool list) noexcept
{
    setSelectedIndex(list ? 1 : 0);
    updateIcon();
}

void ViewPopUp::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    MPopUpButton::colorSchemeEvent(e);
    updateIcon();
}

void ViewPopUp::updateIcon() noexcept
{
    icon.setColor(AKTheme::SecondaryLabel.resolve(colorScheme()));
    icon.setIconFromName(selectedIndex() == 1 ? "view_list" : "grid_view");
}
