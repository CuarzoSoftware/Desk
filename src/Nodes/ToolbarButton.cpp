#include <Nodes/ToolbarButton.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/Core/Events/CZPointerButtonEvent.h>
#include <CZ/Core/Events/CZPointerEnterEvent.h>
#include <CZ/Core/Events/CZPointerLeaveEvent.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>
#include <linux/input-event-codes.h>

ToolbarButton::ToolbarButton(const std::string &iconName, AKNode *parent) noexcept :
    AKRoundSolidColor(0, CZBorderRadius::Make(6), parent)
{
    layout().setWidth(Width);
    layout().setHeight(Height);
    layout().setFlexShrink(0.f);
    layout().setJustifyContent(YGJustifyCenter);
    layout().setAlignItems(YGAlignCenter);
    setCursor(CZCursorShape::Default);

    const SkRegion empty;
    icon.setInputRegion(&empty);
    icon.setIconFromName(iconName);
    updateColors();
}

void ToolbarButton::setEnabled(bool enabled) noexcept
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    m_pressed = false;

    // Enabled under the pointer (e.g. forward after going back): hovered right away
    m_hover = enabled && isPointerOver();
    updateColors();
}

void ToolbarButton::pointerEnterEvent(const CZPointerEnterEvent &e)
{
    AKRoundSolidColor::pointerEnterEvent(e);
    m_hover = m_enabled;
    updateColors();
}

void ToolbarButton::pointerLeaveEvent(const CZPointerLeaveEvent &e)
{
    AKRoundSolidColor::pointerLeaveEvent(e);
    m_hover = m_pressed = false;
    updateColors();
}

void ToolbarButton::pointerButtonEvent(const CZPointerButtonEvent &e)
{
    AKRoundSolidColor::pointerButtonEvent(e);

    if (!m_enabled || e.button != BTN_LEFT)
        return;

    if (e.pressed)
        m_pressed = true;
    else if (m_pressed)
    {
        m_pressed = false;
        updateColors();
        onClick.notify(e); // May disable it (no more history that way)
        return;
    }

    updateColors();
}

void ToolbarButton::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKRoundSolidColor::colorSchemeEvent(e);
    updateColors();
}

void ToolbarButton::updateColors() noexcept
{
    const CZColorScheme scheme { colorScheme() };
    icon.setColor((m_enabled ? AKTheme::SecondaryLabel : AKTheme::TertiaryLabel).resolve(scheme));

    if (m_enabled && m_pressed)
        setBackgroundColor(AKTheme::ControlPressedFill.resolve(scheme));
    else if (m_enabled && m_hover)
        setBackgroundColor(AKTheme::ControlHoverFill.resolve(scheme));
    else
        setBackgroundColor(SK_ColorTRANSPARENT);
}
