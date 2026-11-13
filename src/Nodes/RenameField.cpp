#include <Nodes/RenameField.h>
#include <Nodes/DeskItem.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <CZ/AK/AKTheme.h>

using namespace skia::textlayout;

static constexpr Int32 PadY { 1 };

RenameField::RenameField(AKNode *parent) noexcept :
    AKRoundSolidColor(SK_ColorWHITE, CZBorderRadius::Make(4), parent)
{
    layout().setPositionType(YGPositionTypeAbsolute);
    layout().setFlexDirection(YGFlexDirectionColumn);
    layout().setAlignItems(YGAlignCenter);
    layout().setPadding(YGEdgeHorizontal, DESK_LABEL_PADDING_X);
    layout().setPadding(YGEdgeVertical, PadY);
    setVisible(false);

    // Like the label (see ItemLabel), dark on white
    TextStyle style { theme()->DefaultTextStyle };
    style.setFontSize(DESK_LABEL_FONT_SIZE);
    style.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    style.resetShadows();
    editor.setTextStyle(style);
    editor.setTextColor(CZAdaptiveColor(0xFF000000));
    editor.setSelectionColor(CZAdaptiveColor(0xFFB3D7FF));
    editor.setTextAlign(TextAlign::kCenter);
    editor.setEditable(true);

    editor.onSubmit.subscribe(this, [this]{ commit(); });
    editor.onCancel.subscribe(this, [this]{ cancel(); });
    editor.onEditingFinished.subscribe(this, [this]{
        // Clicked elsewhere (or another surface got the keyboard): renamed if possible
        if (active() && !m_ending && !commit())
            cancel();
    });
}

void RenameField::begin(DeskItem &item, const SkIRect &label) noexcept
{
    if (active())
        cancel();

    const DeskEntry &entry { item.entry() };

    if (entry.kind != DeskEntry::Kind::File || entry.launcher)
        return;

    m_key = entry.key;
    m_item.reset(&item);

    const std::string name { entry.path.filename().string() };
    editor.setWrapWidth(SkScalar(std::max(1, label.width() - 2 * DESK_LABEL_PADDING_X)));
    editor.setText(name);

    // The name without its extension (folders and dot files: all of it)
    size_t end { name.size() };

    if (!std::filesystem::is_directory(entry.path))
        if (const size_t dot { name.rfind('.') }; dot != std::string::npos && dot > 0)
            end = dot;

    editor.setSelection(0, end);
    item.label.setOpacity(0.f);
    place(label);
    setVisible(true);
    editor.setKeyboardFocus(true);
}

void RenameField::place(const SkIRect &label) noexcept
{
    layout().setPosition(YGEdgeLeft, label.x());
    layout().setPosition(YGEdgeTop, label.y());
    layout().setWidth(label.width());
}

bool RenameField::commit() noexcept
{
    if (!active())
        return true;

    const DeskEntry *entry { GetDesk()->entry(m_key) };
    const std::string name { editor.text() };

    if (!entry || name.empty() || name == entry->path.filename().string())
    {
        end();
        return true;
    }

    const std::string key { GetDesk()->rename(*entry, name) };

    if (key.empty())
    {
        // Keep editing: e.g. pick another name
        editor.selectAll();
        return false;
    }

    end();

    if (onRenamed)
        onRenamed(key);

    return true;
}

void RenameField::cancel() noexcept
{
    end();
}

void RenameField::end() noexcept
{
    if (!active() || m_ending)
        return;

    m_ending = true;

    if (m_item)
        m_item->label.setOpacity(1.f);

    m_key.clear();
    m_item.reset();
    setVisible(false);

    // Back to the desktop (its keys: Enter, Delete, shortcuts)
    if (parent())
        parent()->setKeyboardFocus(true);

    m_ending = false;
}
