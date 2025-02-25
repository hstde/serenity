/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Felix Rauch <noreply@felixrau.ch>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PaletteWidget.h"
#include "ImageEditor.h"
#include <AK/Result.h>
#include <AK/Vector.h>
#include <LibCore/File.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/ColorPicker.h>
#include <LibGUI/MessageBox.h>
#include <LibGfx/Palette.h>

REGISTER_WIDGET(PixelPaint, PaletteWidget);

namespace PixelPaint {

class ColorWidget : public GUI::Frame {
    C_OBJECT(ColorWidget);

public:
    explicit ColorWidget(Color color, PaletteWidget& palette_widget)
        : m_palette_widget(palette_widget)
        , m_color(color)
    {
        set_fixed_width(16);
    }

    virtual ~ColorWidget() override
    {
    }

    virtual Color color() { return m_color; }

    virtual void mousedown_event(GUI::MouseEvent& event) override
    {
        if (event.modifiers() & KeyModifier::Mod_Ctrl && event.button() == GUI::MouseButton::Left) {
            auto dialog = GUI::ColorPicker::construct(m_color, window());
            if (dialog->exec() == GUI::Dialog::ExecOK) {
                m_color = dialog->color();
                auto pal = palette();
                pal.set_color(ColorRole::Background, m_color);
                set_palette(pal);
                update();
            }
            return;
        }

        if (event.button() == GUI::MouseButton::Left)
            m_palette_widget.set_primary_color(m_color);
        else if (event.button() == GUI::MouseButton::Right)
            m_palette_widget.set_secondary_color(m_color);
    }

private:
    PaletteWidget& m_palette_widget;
    Color m_color;
};

PaletteWidget::PaletteWidget()
{
    set_frame_shape(Gfx::FrameShape::Panel);
    set_frame_shadow(Gfx::FrameShadow::Raised);
    set_frame_thickness(0);
    set_fill_with_background_color(true);

    set_fixed_height(33);

    m_secondary_color_widget = add<GUI::Frame>();
    m_secondary_color_widget->set_relative_rect({ 0, 2, 60, 31 });
    m_secondary_color_widget->set_fill_with_background_color(true);

    m_primary_color_widget = add<GUI::Frame>();
    Gfx::IntRect rect { 0, 0, 38, 15 };
    rect.center_within(m_secondary_color_widget->relative_rect());
    m_primary_color_widget->set_relative_rect(rect);
    m_primary_color_widget->set_fill_with_background_color(true);

    m_color_container = add<GUI::Widget>();
    m_color_container->set_relative_rect(m_secondary_color_widget->relative_rect().right() + 2, 2, 500, 32);
    m_color_container->set_layout<GUI::VerticalBoxLayout>();
    m_color_container->layout()->set_spacing(1);

    auto& top_color_container = m_color_container->add<GUI::Widget>();
    top_color_container.set_name("top_color_container");
    top_color_container.set_layout<GUI::HorizontalBoxLayout>();
    top_color_container.layout()->set_spacing(1);

    auto& bottom_color_container = m_color_container->add<GUI::Widget>();
    bottom_color_container.set_name("bottom_color_container");
    bottom_color_container.set_layout<GUI::HorizontalBoxLayout>();
    bottom_color_container.layout()->set_spacing(1);

    auto result = load_palette_path("/res/color-palettes/default.palette");
    if (result.is_error()) {
        GUI::MessageBox::show_error(window(), String::formatted("Loading default palette failed: {}", result.error()));
        display_color_list(fallback_colors());

        return;
    }

    display_color_list(result.value());
}

void PaletteWidget::set_image_editor(ImageEditor& editor)
{
    m_editor = &editor;
    set_primary_color(editor.primary_color());
    set_secondary_color(editor.secondary_color());

    editor.on_primary_color_change = [this](Color color) {
        set_primary_color(color);
    };

    editor.on_secondary_color_change = [this](Color color) {
        set_secondary_color(color);
    };
}

PaletteWidget::~PaletteWidget()
{
}

void PaletteWidget::set_primary_color(Color color)
{
    m_editor->set_primary_color(color);
    auto pal = m_primary_color_widget->palette();
    pal.set_color(ColorRole::Background, color);
    m_primary_color_widget->set_palette(pal);
    m_primary_color_widget->update();
}

void PaletteWidget::set_secondary_color(Color color)
{
    m_editor->set_secondary_color(color);
    auto pal = m_secondary_color_widget->palette();
    pal.set_color(ColorRole::Background, color);
    m_secondary_color_widget->set_palette(pal);
    m_secondary_color_widget->update();
}

void PaletteWidget::display_color_list(Vector<Color> const& colors)
{
    int colors_to_add = colors.size();
    if (colors_to_add == 0) {
        dbgln("Empty color list given. Using fallback colors.");
        display_color_list(fallback_colors());
        return;
    }

    auto& top_color_container = *m_color_container->find_descendant_of_type_named<GUI::Widget>("top_color_container");
    top_color_container.remove_all_children();

    auto& bottom_color_container = *m_color_container->find_descendant_of_type_named<GUI::Widget>("bottom_color_container");
    bottom_color_container.remove_all_children();

    auto add_color_widget = [&](GUI::Widget& container, Color color) {
        auto& color_widget = container.add<ColorWidget>(color, *this);
        color_widget.set_fill_with_background_color(true);
        auto pal = color_widget.palette();
        pal.set_color(ColorRole::Background, color);
        color_widget.set_palette(pal);
    };

    int colors_per_row = ceil(colors_to_add / 2);
    int number_of_added_colors = 0;
    for (auto& color : colors) {
        if (number_of_added_colors < colors_per_row)
            add_color_widget(top_color_container, color);
        else
            add_color_widget(bottom_color_container, color);

        ++number_of_added_colors;
    }
}

Vector<Color> PaletteWidget::colors()
{
    Vector<Color> colors;

    for (auto& color_container : m_color_container->child_widgets()) {
        color_container.for_each_child_of_type<ColorWidget>([&](auto& color_widget) {
            colors.append(color_widget.color());
            return IterationDecision::Continue;
        });
    }

    return colors;
}

Result<Vector<Color>, String> PaletteWidget::load_palette_file(Core::File& file)
{
    Vector<Color> palette;

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_whitespace())
            continue;

        auto color = Color::from_string(line);
        if (!color.has_value()) {
            dbgln("Could not parse \"{}\" as a color", line);
            continue;
        }

        palette.append(color.value());
    }

    file.close();

    if (palette.is_empty())
        return String { "The palette file did not contain any usable colors"sv };

    return palette;
}

Result<Vector<Color>, String> PaletteWidget::load_palette_fd_and_close(int fd)
{
    auto file = Core::File::construct();
    file->open(fd, Core::OpenMode::ReadOnly, Core::File::ShouldCloseFileDescriptor::Yes);
    if (file->has_error())
        return String { file->error_string() };

    return load_palette_file(file);
}

Result<Vector<Color>, String> PaletteWidget::load_palette_path(String const& file_path)
{
    auto file_or_error = Core::File::open(file_path, Core::OpenMode::ReadOnly);
    if (file_or_error.is_error())
        return file_or_error.error();

    auto& file = *file_or_error.value();
    return load_palette_file(file);
}

Result<void, String> PaletteWidget::save_palette_fd_and_close(Vector<Color> palette, int fd)
{
    auto file = Core::File::construct();
    file->open(fd, Core::OpenMode::WriteOnly, Core::File::ShouldCloseFileDescriptor::Yes);
    if (file->has_error())
        return String { file->error_string() };

    for (auto& color : palette) {
        file->write(color.to_string_without_alpha());
        file->write("\n");
    }

    file->close();

    return {};
}

Vector<Color> PaletteWidget::fallback_colors()
{
    Vector<Color> fallback_colors;

    fallback_colors.append(Color::from_rgb(0x000000));
    fallback_colors.append(Color::from_rgb(0xffffff));

    return fallback_colors;
}

}
