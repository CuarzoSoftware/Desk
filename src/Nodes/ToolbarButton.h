#ifndef TOOLBARBUTTON_H
#define TOOLBARBUTTON_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKRoundSolidColor.h>
#include <CZ/AK/Nodes/AKFontIcon.h>
#include <CZ/Core/CZSignal.h>

/**
 * @brief A borderless icon button of a folder window's toolbar (back, forward), like Finder's.
 *
 * Hovered it shows a rounded background (AKTheme::ControlHoverFill, darker pressed). Disabled
 * (e.g. back with nothing to go back to) its icon is dimmed and it shows nothing when hovered.
 */
class ToolbarButton final : public AKRoundSolidColor
{
public:
    ToolbarButton(const std::string &iconName, AKNode *parent = nullptr) noexcept;

    static constexpr SkScalar Width  { 28.f };
    static constexpr SkScalar Height { 24.f };

    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept { return m_enabled; }

    /// Clicked (released over it).
    CZSignal<const CZPointerButtonEvent&> onClick;

    AKFontIcon icon { "", 22, this };

protected:
    void pointerEnterEvent(const CZPointerEnterEvent &e) override;
    void pointerLeaveEvent(const CZPointerLeaveEvent &e) override;
    void pointerButtonEvent(const CZPointerButtonEvent &e) override;
    void colorSchemeEvent(const CZColorSchemeEvent &e) override;

private:
    void updateColors() noexcept;
    bool m_enabled { true }, m_hover { false }, m_pressed { false };
};

#endif // TOOLBARBUTTON_H
