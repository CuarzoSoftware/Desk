#ifndef ITEMLABEL_H
#define ITEMLABEL_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKBakeable.h>
#include <CZ/skia/modules/skparagraph/include/Paragraph.h>
#include <memory>
#include <string>

/**
 * @brief The name under a desktop icon, like Finder's.
 *
 * Centered, up to DESK_LABEL_LINES lines (ellipsized), white with a soft shadow so it reads over
 * any picture. Selected, each line gets a blue rounded highlight instead of the shadow.
 *
 * Its width is fixed (setWidth()), its height follows the text.
 */
class ItemLabel final : public AKBakeable
{
public:
    ItemLabel(AKNode *parent = nullptr) noexcept;

    void setText(const std::string &text) noexcept;
    const std::string &text() const noexcept { return m_text; }

    void setWidth(Int32 width) noexcept;

    void setSelected(bool selected) noexcept;
    bool selected() const noexcept { return m_selected; }

    /// Bounds of the visible text (the highlighted area), in node coordinates.
    SkRect textBounds() const noexcept;

protected:
    void bakeEvent(const AKBakeEvent &event) override;

private:
    void update() noexcept;
    std::unique_ptr<skia::textlayout::Paragraph> m_paragraph;
    std::string m_text;
    Int32 m_width { 96 };
    bool m_selected { false };
};

#endif // ITEMLABEL_H
