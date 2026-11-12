#ifndef ITEMLABEL_H
#define ITEMLABEL_H

#include <Core/Types.h>
#include <CZ/AK/Nodes/AKContainer.h>
#include <CZ/AK/Nodes/AKImageFrame.h>
#include <CZ/skia/modules/skparagraph/include/Paragraph.h>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief The name under a desktop icon, like Finder's.
 *
 * Centered, up to DESK_LABEL_LINES lines (ellipsized), white with a soft shadow so it reads over
 * any picture. Selected, each line gets a blue rounded highlight instead of the shadow.
 *
 * Both looks are pre-rendered into images (the selected one the first time it's needed), at the
 * scale of the screen it's shown on (never a 2x image scaled down on a 1x screen), and kept
 * until the text, width or scale changes: selecting just swaps the image shown, without laying the text
 * out or blurring its shadow again (which is costly with many items, e.g. a rubber band selection).
 *
 * Its width is fixed (setWidth()), its height follows the text.
 */
class ItemLabel final : public AKContainer
{
public:
    ItemLabel(AKNode *parent = nullptr) noexcept;

    void setText(const std::string &text) noexcept;
    const std::string &text() const noexcept { return m_text; }

    void setWidth(Int32 width) noexcept;

    void setSelected(bool selected) noexcept;
    bool selected() const noexcept { return m_selected; }

    void setOpacity(SkScalar opacity) noexcept { m_frame.setOpacity(opacity); }

    /// Bounds of the visible text (the highlighted area), in node coordinates.
    SkRect textBounds() const noexcept;

private:
    /// Lays the text out again (text or width changed) and drops the rendered looks.
    void update() noexcept;

    /// Shows the look for selected(), rendering it if needed.
    void showLook() noexcept;
    std::shared_ptr<RImage> renderLook(bool selected) noexcept;
    std::unique_ptr<skia::textlayout::Paragraph> buildParagraph(bool selected) const noexcept;

    /// The highlight rect of each line (node coordinates), including the ellipsis.
    std::vector<SkRect> lineRects() const noexcept;

    // Shows the looks; checks the screen's scale right before its image is laid out each frame.
    class LookFrame final : public AKImageFrame
    {
    public:
        LookFrame(ItemLabel &label) noexcept : AKImageFrame(&label), label(label) {}
        ItemLabel &label;
    protected:
        void onSceneBegin() override;
    };

    /// Re-renders the looks if the screen's scale (@p scale) isn't the one they're for.
    void checkScale(Int32 scale) noexcept;

    LookFrame m_frame { *this };
    std::shared_ptr<RImage> m_looks[2];  // Normal, selected
    Int32 m_looksScale { 0 };
    SkScalar m_ellipsisWidth { 0.f };
    std::unique_ptr<skia::textlayout::Paragraph> m_paragraph; // Layout (line metrics)
    SkISize m_size {};
    std::string m_text;
    Int32 m_width { 96 };
    bool m_selected { false };
};

#endif // ITEMLABEL_H
