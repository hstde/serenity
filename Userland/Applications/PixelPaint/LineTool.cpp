/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "LineTool.h"
#include "ImageEditor.h"
#include "Layer.h"
#include <AK/Math.h>
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Label.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Painter.h>
#include <LibGUI/ValueSlider.h>

namespace PixelPaint {

static Gfx::IntPoint constrain_line_angle(Gfx::IntPoint const& start_pos, Gfx::IntPoint const& end_pos, float angle_increment)
{
    float current_angle = AK::atan2<float>(end_pos.y() - start_pos.y(), end_pos.x() - start_pos.x()) + float { M_PI * 2 };

    float constrained_angle = ((int)((current_angle + angle_increment / 2) / angle_increment)) * angle_increment;

    auto diff = end_pos - start_pos;
    float line_length = AK::hypot<float>(diff.x(), diff.y());

    return { start_pos.x() + (int)(AK::cos(constrained_angle) * line_length),
        start_pos.y() + (int)(AK::sin(constrained_angle) * line_length) };
}

LineTool::LineTool()
{
}

LineTool::~LineTool()
{
}

void LineTool::on_mousedown(Layer&, GUI::MouseEvent& layer_event, GUI::MouseEvent&)
{
    if (layer_event.button() != GUI::MouseButton::Left && layer_event.button() != GUI::MouseButton::Right)
        return;

    if (m_drawing_button != GUI::MouseButton::None)
        return;

    m_drawing_button = layer_event.button();

    m_line_start_position = layer_event.position();
    m_line_end_position = layer_event.position();

    m_editor->update();
}

void LineTool::on_mouseup(Layer& layer, GUI::MouseEvent& event, GUI::MouseEvent&)
{
    if (event.button() == m_drawing_button) {
        GUI::Painter painter(layer.bitmap());
        painter.draw_line(m_line_start_position, m_line_end_position, m_editor->color_for(m_drawing_button), m_thickness);
        m_drawing_button = GUI::MouseButton::None;
        layer.did_modify_bitmap();
        m_editor->did_complete_action();
    }
}

void LineTool::on_mousemove(Layer&, GUI::MouseEvent& layer_event, GUI::MouseEvent&)
{
    if (m_drawing_button == GUI::MouseButton::None)
        return;

    if (layer_event.shift()) {
        constexpr auto ANGLE_STEP = M_PI / 8;
        m_line_end_position = constrain_line_angle(m_line_start_position, layer_event.position(), ANGLE_STEP);
    } else {
        m_line_end_position = layer_event.position();
    }
    m_editor->update();
}

void LineTool::on_second_paint(Layer const& layer, GUI::PaintEvent& event)
{
    if (m_drawing_button == GUI::MouseButton::None)
        return;

    GUI::Painter painter(*m_editor);
    painter.add_clip_rect(event.rect());
    auto preview_start = m_editor->layer_position_to_editor_position(layer, m_line_start_position).to_type<int>();
    auto preview_end = m_editor->layer_position_to_editor_position(layer, m_line_end_position).to_type<int>();
    painter.draw_line(preview_start, preview_end, m_editor->color_for(m_drawing_button), m_thickness);
}

void LineTool::on_keydown(GUI::KeyEvent& event)
{
    if (event.key() == Key_Escape && m_drawing_button != GUI::MouseButton::None) {
        m_drawing_button = GUI::MouseButton::None;
        m_editor->update();
        event.accept();
    }
}

GUI::Widget* LineTool::get_properties_widget()
{
    if (!m_properties_widget) {
        m_properties_widget = GUI::Widget::construct();
        m_properties_widget->set_layout<GUI::VerticalBoxLayout>();

        auto& thickness_container = m_properties_widget->add<GUI::Widget>();
        thickness_container.set_fixed_height(20);
        thickness_container.set_layout<GUI::HorizontalBoxLayout>();

        auto& thickness_label = thickness_container.add<GUI::Label>("Thickness:");
        thickness_label.set_text_alignment(Gfx::TextAlignment::CenterLeft);
        thickness_label.set_fixed_size(80, 20);

        auto& thickness_slider = thickness_container.add<GUI::ValueSlider>(Orientation::Horizontal, "px");
        thickness_slider.set_range(1, 10);
        thickness_slider.set_value(m_thickness);

        thickness_slider.on_change = [&](int value) {
            m_thickness = value;
        };
    }

    return m_properties_widget.ptr();
}

}
