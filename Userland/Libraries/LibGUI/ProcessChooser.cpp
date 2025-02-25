/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/ProcessChooser.h>
#include <LibGUI/RunningProcessesModel.h>
#include <LibGUI/SortingProxyModel.h>
#include <LibGUI/TableView.h>

namespace GUI {

ProcessChooser::ProcessChooser(const StringView& window_title, const StringView& button_label, const Gfx::Bitmap* window_icon, GUI::Window* parent_window)
    : Dialog(parent_window)
    , m_window_title(window_title)
    , m_button_label(button_label)
    , m_window_icon(window_icon)
{
    set_title(m_window_title);

    if (m_window_icon)
        set_icon(m_window_icon);
    else if (parent_window)
        set_icon(parent_window->icon());

    resize(300, 340);
    center_on_screen();

    auto& widget = set_main_widget<GUI::Widget>();
    widget.set_fill_with_background_color(true);
    widget.set_layout<GUI::VerticalBoxLayout>();

    m_table_view = widget.add<GUI::TableView>();
    auto sorting_model = GUI::SortingProxyModel::create(RunningProcessesModel::create());
    sorting_model->set_sort_role(GUI::ModelRole::Display);
    m_table_view->set_model(sorting_model);
    m_table_view->set_key_column_and_sort_order(RunningProcessesModel::Column::PID, GUI::SortOrder::Descending);

    m_table_view->on_activation = [this](const ModelIndex& index) { set_pid_from_index_and_close(index); };

    auto& button_container = widget.add<GUI::Widget>();
    button_container.set_fixed_height(30);
    button_container.set_layout<GUI::HorizontalBoxLayout>();
    button_container.set_content_margins({ 0, 4, 0, 4 });
    button_container.layout()->set_margins({ 0, 0, 4, 0 });
    button_container.layout()->add_spacer();

    auto& select_button = button_container.add<GUI::Button>(m_button_label);
    select_button.set_fixed_width(80);
    select_button.on_click = [this](auto) {
        if (m_table_view->selection().is_empty()) {
            GUI::MessageBox::show(this, "No process selected!", m_window_title, GUI::MessageBox::Type::Error);
            return;
        }
        auto index = m_table_view->selection().first();
        set_pid_from_index_and_close(index);
    };
    auto& cancel_button = button_container.add<GUI::Button>("Cancel");
    cancel_button.set_fixed_width(80);
    cancel_button.on_click = [this](auto) {
        done(ExecCancel);
    };

    m_table_view->model()->invalidate();

    m_refresh_timer = add<Core::Timer>();

    m_refresh_timer->start(m_refresh_interval); // Start the timer to update the processes
    m_refresh_timer->on_timeout = [this] {
        auto previous_selected_pid = -1; // Store the selection index to not to clear the selection upon update.
        if (!m_table_view->selection().is_empty()) {
            auto pid_as_variant = m_table_view->selection().first().data(GUI::ModelRole::Custom);
            previous_selected_pid = pid_as_variant.as_i32();
        }

        m_table_view->model()->invalidate();

        if (previous_selected_pid == -1) {
            return;
        }

        auto model = m_table_view->model();
        auto row_count = model->row_count();
        auto column_index = 1; // Corresponds to PID column in the m_table_view.
        for (int row_index = 0; row_index < row_count; ++row_index) {
            auto cell_index = model->index(row_index, column_index);
            auto pid_as_variant = cell_index.data(GUI::ModelRole::Custom);
            if (previous_selected_pid == pid_as_variant.as_i32()) {
                m_table_view->selection().set(cell_index); // Set only if PIDs are matched.
            }
        }
    };
}

void ProcessChooser::set_pid_from_index_and_close(const ModelIndex& index)
{
    m_pid = index.data(GUI::ModelRole::Custom).as_i32();
    done(ExecOK);
}

ProcessChooser::~ProcessChooser()
{
}

}
