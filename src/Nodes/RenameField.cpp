#include <Nodes/RenameField.h>
#include <Core/Log.h>
#include <CZ/AK/AKTheme.h>
#include <CZ/AK/AKScene.h>
#include <CZ/Core/Events/CZColorSchemeEvent.h>

using namespace skia::textlayout;

static constexpr Int32 PadY { 1 };

RenameField::RenameField(AKNode *parent, Style style) noexcept :
    AKRoundSolidColor(SK_ColorWHITE, CZBorderRadius::Make(4), parent),
    m_style(style)
{
    layout().setPositionType(YGPositionTypeAbsolute);
    layout().setFlexDirection(YGFlexDirectionColumn);
    layout().setPadding(YGEdgeVertical, PadY);
    enableChildrenClipping(true); // Long names don't spill out
    setVisible(false);

    TextStyle text { theme()->DefaultTextStyle };
    text.resetShadows();

    if (style == Style::Label)
    {
        // Like the label (see ItemLabel), dark on white
        layout().setAlignItems(YGAlignCenter);
        layout().setPadding(YGEdgeHorizontal, DESK_LABEL_PADDING_X);
        text.setFontSize(DESK_LABEL_FONT_SIZE);
        text.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
        editor.setTextStyle(text);
        editor.setTextColor(CZAdaptiveColor(0xFF000000));
        editor.setSelectionColor(CZAdaptiveColor(0xFFB3D7FF));
        editor.setTextAlign(TextAlign::kCenter);
    }
    else if (style == Style::Grid)
    {
        // Like a window icon's label (see ItemLabel::Style::Window), in a field with a focus ring
        layout().setAlignItems(YGAlignCenter);
        layout().setPadding(YGEdgeHorizontal, DESK_LABEL_PADDING_X);
        setStrokeWidth(2);
        updateColors();
        text.setFontSize(DESK_WINDOW_LABEL_FONT_SIZE);
        editor.setTextStyle(text);
        editor.setTextColor(AKTheme::Label);
        editor.setSelectionColor(CZAdaptiveColor(0xFFB3D7FF, 0xFF3F638B));
        editor.setTextAlign(TextAlign::kCenter);
    }
    else
    {
        // Like the row's name (see FolderListRow), in a field with a focus ring
        layout().setAlignItems(YGAlignFlexStart);
        layout().setJustifyContent(YGJustifyCenter);
        layout().setPadding(YGEdgeHorizontal, 3.f);
        setStrokeWidth(2);
        updateColors();
        text.setFontSize(DESK_LIST_FONT_SIZE);
        editor.setTextStyle(text);
        editor.setTextColor(AKTheme::Label);
        editor.setSelectionColor(CZAdaptiveColor(0xFFB3D7FF, 0xFF3F638B));
        editor.setTextAlign(TextAlign::kLeft);
    }

    editor.setEditable(true);

    editor.onSubmit.subscribe(this, [this]{ commit(); });
    editor.onCancel.subscribe(this, [this]{ cancel(); });
    editor.onEditingFinished.subscribe(this, [this]{
        // Clicked elsewhere (or another surface got the keyboard): renamed if possible
        if (active() && !m_ending && !commit())
            cancel();
    });
}

void RenameField::begin(const DeskEntry &entry, const SkIRect &box, std::function<void(bool editing)> editing) noexcept
{
    if (active())
        cancel();

    if (entry.kind != DeskEntry::Kind::File || entry.launcher)
        return;

    m_entry = entry;
    m_editing = std::move(editing);

    const std::string name { entry.path.filename().string() };

    // The label's lines wrap at its width (a list's name is a single line)
    if (m_style != Style::List)
        editor.setWrapWidth(SkScalar(std::max(1, box.width() - 2 * DESK_LABEL_PADDING_X)));

    editor.setText(name);

    // The name without its extension (folders and dot files: all of it)
    size_t end { name.size() };

    if (!std::filesystem::is_directory(entry.path))
        if (const size_t dot { name.rfind('.') }; dot != std::string::npos && dot > 0)
            end = dot;

    editor.setSelection(0, end);

    if (m_editing)
        m_editing(true);

    place(box);
    setVisible(true);
    editor.setKeyboardFocus(true);
}

void RenameField::colorSchemeEvent(const CZColorSchemeEvent &e)
{
    AKRoundSolidColor::colorSchemeEvent(e);
    updateColors();
}

void RenameField::updateColors() noexcept
{
    // The desktop label's look is always dark on white (over the wallpaper)
    if (m_style == Style::Label)
        return;

    setBackgroundColor(AKTheme::WindowBackground.resolve(colorScheme()));
    setStrokeColor(AKTheme::KeyboardFocusIndicator.resolve(colorScheme()));
}

void RenameField::place(const SkIRect &box) noexcept
{
    layout().setPosition(YGEdgeLeft, box.x());
    layout().setPosition(YGEdgeTop, box.y());
    layout().setWidth(box.width());

    // A row's height (a label grows with its lines)
    if (m_style == Style::List)
        layout().setHeight(box.height());
}

bool RenameField::commit() noexcept
{
    if (!active())
        return true;

    const std::string name { editor.text() };

    if (name.empty() || name == m_entry.path.filename().string())
    {
        end();
        return true;
    }

    const std::string key { GetDesk()->rename(m_entry, name) };

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

    // Ended first (e.g. the name below shown again if it's rebound meanwhile)
    const auto editing { std::move(m_editing) };
    m_entry = {};
    m_editing = nullptr;
    setVisible(false);

    if (editing)
        editing(false);

    // Back to the desktop or window (its keys: Enter, Delete, shortcuts)
    if (scene() && scene()->root())
        scene()->root()->setKeyboardFocus(true);

    m_ending = false;
}
