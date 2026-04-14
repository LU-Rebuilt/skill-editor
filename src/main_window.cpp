#include "main_window.h"
#include "undo_commands.h"

#include "file_browser.h"

#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QFile>
#include <QPushButton>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QUndoView>
#include <sqlite3.h>
#include <unordered_set>

namespace skill_editor {

// Collect all behavior IDs reachable from a skill's root via BFS.
static std::unordered_set<int> collect_tree_behaviors(
        const CdClientData& data, int skill_id) {
    int root_id = 0;
    for (const auto& s : data.skills)
        if (s.skill_id == skill_id) { root_id = s.behavior_id; break; }

    std::unordered_set<int> in_tree;
    if (root_id == 0) return in_tree;

    std::vector<int> queue = {root_id};
    while (!queue.empty()) {
        int bid = queue.back();
        queue.pop_back();
        if (!in_tree.insert(bid).second) continue;
        auto pit = data.params.find(bid);
        if (pit == data.params.end()) continue;
        for (const auto& p : pit->second) {
            int ref = static_cast<int>(p.value);
            if (ref > 0 && data.behaviors.count(ref) &&
                is_behavior_ref_param(p.parameter_id))
                queue.push_back(ref);
        }
    }
    return in_tree;
}

// Check whether a behavior has any references outside the current skill tree.
// Returns true if COW is needed (i.e., external references exist).
static bool needs_cow(const CdClientData& data, int skill_id, int behavior_id) {
    auto in_tree = collect_tree_behaviors(data, skill_id);

    // Count skill roots that reference this behavior
    for (const auto& s : data.skills) {
        if (s.behavior_id == behavior_id && s.skill_id != skill_id)
            return true; // another skill uses this as root
    }

    // Count parent behavior params that reference this behavior
    for (const auto& [bid, pvec] : data.params) {
        for (const auto& p : pvec) {
            if (static_cast<int>(p.value) == behavior_id &&
                is_behavior_ref_param(p.parameter_id) &&
                !in_tree.count(bid)) {
                return true; // a behavior outside this tree references it
            }
        }
    }

    return false;
}

// After a COW clone, rewire the current skill tree so old_id references
// become new_id. Updates skill root if applicable, and any parent behavior
// parameters in the tree that pointed to old_id.
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LU Skill Editor");
    resize(1400, 800);

    // Menu bar
    auto* file_menu = menuBar()->addMenu("&File");
    file_menu->addAction("&Open CDClient...", QKeySequence::Open,
                         this, &MainWindow::on_open_file);
    file_menu->addAction("Set &Locale XML...", this, &MainWindow::on_set_locale);
    file_menu->addSeparator();
    file_menu->addAction("&Quit", QKeySequence::Quit,
                         qApp, &QApplication::quit);

    // Undo stack + history dock
    undo_stack_ = new QUndoStack(this);
    undo_view_ = new QUndoView(undo_stack_);
    undo_dock_ = new QDockWidget("Edit History", this);
    undo_dock_->setWidget(undo_view_);
    undo_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, undo_dock_);
    undo_dock_->hide();

    object_skills_panel_ = new ObjectSkillsPanel;

    auto* edit_menu = menuBar()->addMenu("&Edit");
    QAction* undo_action = undo_stack_->createUndoAction(this, "&Undo");
    undo_action->setShortcut(QKeySequence::Undo);
    edit_menu->addAction(undo_action);
    QAction* redo_action = undo_stack_->createRedoAction(this, "&Redo");
    redo_action->setShortcut(QKeySequence("Ctrl+Shift+Z"));
    edit_menu->addAction(redo_action);
    edit_menu->addSeparator();
    edit_menu->addAction("&New Skill...", this, &MainWindow::on_create_skill);
    edit_menu->addAction("C&lone Skill", this, &MainWindow::on_clone_skill);
    edit_menu->addSeparator();
    edit_menu->addAction("Clone &Subtree", this, &MainWindow::on_clone_subtree);
    edit_menu->addAction("Save Subtree as &Template...", this, &MainWindow::on_save_template);
    edit_menu->addAction("&Load Template...", this, &MainWindow::on_load_template);
    edit_menu->addSeparator();
    edit_menu->addAction("&History", [this]() {
        undo_dock_->setVisible(!undo_dock_->isVisible());
    });
    edit_menu->addSeparator();
    auto* force_cow_action = edit_menu->addAction("&Force Copy-on-Write");
    force_cow_action->setCheckable(true);
    {
        QSettings settings;
        force_cow_ = settings.value("force_cow", false).toBool();
        force_cow_action->setChecked(force_cow_);
    }
    connect(force_cow_action, &QAction::toggled, this, [this](bool checked) {
        force_cow_ = checked;
        QSettings settings;
        settings.setValue("force_cow", checked);
    });

    auto* help_menu = menuBar()->addMenu("&Help");
    help_menu->addAction("&User Guide", [this]() {
        QMessageBox::information(this, "LU Skill Editor — User Guide",
            "<h3>Navigation</h3>"
            "<ul>"
            "<li><b>Left-click empty space + drag</b> — Pan the canvas</li>"
            "<li><b>Scroll wheel</b> — Zoom in/out</li>"
            "<li><b>Ctrl+0</b> — Fit tree to view</li>"
            "<li><b>Ctrl++/Ctrl+-</b> — Zoom in/out</li>"
            "<li><b>Left-click a node</b> — Select it, highlight subtree in red</li>"
            "<li><b>Left-drag a node</b> — Move it</li>"
            "<li><b>Click empty space</b> — Clear selection highlight</li>"
            "</ul>"
            "<h3>Editing</h3>"
            "<ul>"
            "<li><b>Double-click a value</b> — Edit it (saves to DB immediately)</li>"
            "<li><b>Ctrl+click a blue behavior value</b> — Navigate to that behavior</li>"
            "<li><b>bool params</b> — Dropdown: 0 (false) / 1 (true)</li>"
            "<li><b>behavior params</b> — Enter a behavior ID number</li>"
            "<li><b>Copy-on-write</b> — If a behavior is referenced outside the current skill tree, "
            "editing it will clone it first so the original is preserved. If all references are "
            "within the current tree, edits are applied directly.</li>"
            "<li><b>View stays in place</b> — Editing a parameter preserves your current zoom and pan</li>"
            "</ul>"
            "<h3>Right-click Menus</h3>"
            "<ul>"
            "<li><b>Right-click a skill</b> — Clone Skill, Save as Template, Delete Skill</li>"
            "<li><b>Right-click a graph node</b> — Clone Subtree, Save as Template, View Details, Delete</li>"
            "<li><b>Right-click empty graph space</b> — Create New Node, Load Template</li>"
            "</ul>"
            "<h3>Graph Legend</h3>"
            "<ul>"
            "<li><b>Node border color</b> — Behavior type category (red=combat, green=heal, blue=control, etc.)</li>"
            "<li><b>Solid grey edges</b> — Forward behavior references (tree structure)</li>"
            "<li><b>Dashed orange edges</b> — Back-references (End→Start, AlterChainDelay→Chain)</li>"
            "<li><b>Red highlight</b> — Selected node's subtree</li>"
            "</ul>"
            "<h3>Data</h3>"
            "<ul>"
            "<li><b>File &gt; Open CDClient</b> — Load a .sqlite database</li>"
            "<li><b>File &gt; Set Locale XML</b> — Load locale.xml for skill display names</li>"
            "<li><b>Behavior ID goto box</b> — Jump to any behavior by ID</li>"
            "<li><b>Dynamic parameters</b> — Repeatable params (behavior N, include_factionN, etc.) "
            "shown in green with <b>+ Add</b> / <b>- Remove</b> buttons</li>"
            "<li><b>\"—\" values</b> — Parameter exists in schema but not set in DB (uses default)</li>"
            "<li><b>Refs: N tree / M total</b> — References within current skill tree vs entire CDClient. "
            "Click <b>?</b> for the detailed list.</li>"
            "<li><b>Used By</b> — Shows which game objects (LOTs) use the selected skill</li>"
            "</ul>"
        );
    });
    help_menu->addAction("&About", [this]() {
        QMessageBox::about(this, "About LU Skill Editor",
            "<b>LU Skill Editor</b><br>"
            "Visualize and edit LEGO Universe behavior trees.<br>"
            "Reads/writes CDClient SQLite databases.<br><br>"
            "Behavior schemas from DarkflameServer.<br>"
            "Skill names from client locale.xml.");
    });

    // Main splitter: [skill list | graph view | tree + editor]
    auto* main_splitter = new QSplitter(Qt::Horizontal);
    setCentralWidget(main_splitter);

    // Left panel: skill list (top) + object skills (bottom) in a vertical splitter
    auto* left_splitter = new QSplitter(Qt::Vertical);

    auto* skill_list_widget = new QWidget;
    auto* left_layout = new QVBoxLayout(skill_list_widget);
    left_layout->setContentsMargins(4, 4, 4, 4);

    auto* left_label = new QLabel("Skills");
    left_label->setFont(QFont("sans-serif", 10, QFont::Bold));
    left_layout->addWidget(left_label);

    search_box_ = new QLineEdit;
    search_box_->setPlaceholderText("Search skills...");
    left_layout->addWidget(search_box_);
    connect(search_box_, &QLineEdit::textChanged, this, &MainWindow::on_search_changed);

    // Behavior ID goto
    auto* goto_layout = new QHBoxLayout;
    goto_box_ = new QLineEdit;
    goto_box_->setPlaceholderText("Behavior ID...");
    goto_box_->setMaximumWidth(120);
    goto_layout->addWidget(goto_box_);
    auto* goto_btn = new QPushButton("Go");
    goto_btn->setMaximumWidth(40);
    goto_layout->addWidget(goto_btn);
    left_layout->addLayout(goto_layout);
    connect(goto_btn, &QPushButton::clicked, this, &MainWindow::on_goto_behavior);
    connect(goto_box_, &QLineEdit::returnPressed, this, &MainWindow::on_goto_behavior);

    skill_list_ = new QListWidget;
    skill_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    left_layout->addWidget(skill_list_);
    connect(skill_list_, &QListWidget::itemClicked, this, &MainWindow::on_skill_selected);
    connect(skill_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        auto* item = skill_list_->itemAt(pos);
        if (!item) return;
        int skill_id = item->data(Qt::UserRole).toInt();

        QMenu menu;
        menu.addAction("Clone Skill", [this, skill_id]() {
            if (db_path_.empty()) return;
            try {
                auto refresh_cb = [this](int sel) { refresh_ui(sel); };
                auto* cmd = new CloneSkillCommand(data_, db_path_, skill_id, refresh_cb);
                undo_stack_->push(cmd);
                int new_id = cmd->created_skill_id();
                current_skill_id_ = new_id;
                refresh_ui(0);
                statusBar()->showMessage(
                    QString("Cloned skill %1 -> %2").arg(skill_id).arg(new_id));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.addAction("Save as Template...", [this, skill_id]() {
            int root_beh = 0;
            for (const auto& s : data_.skills)
                if (s.skill_id == skill_id) { root_beh = s.behavior_id; break; }
            if (root_beh == 0) return;
            QString path = qt_common::FileBrowserDialog::getSaveFileName(this, "Save Template", {},
                "JSON Template (*.json);;All Files (*)");
            if (path.isEmpty()) return;
            try {
                save_template(data_, root_beh, path.toStdString());
                statusBar()->showMessage(QString("Saved template for skill %1").arg(skill_id));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.addSeparator();
        menu.addAction("Delete Skill", [this, skill_id]() {
            if (db_path_.empty()) return;
            auto ans = QMessageBox::question(this, "Delete Skill",
                QString("Delete skill %1?").arg(skill_id));
            if (ans != QMessageBox::Yes) return;
            try {
                auto refresh_cb = [this](int sel) { refresh_ui(sel); };
                auto* cmd = new DeleteSkillCommand(data_, db_path_, skill_id, refresh_cb);
                undo_stack_->push(cmd);
                current_skill_id_ = 0;
                refresh_ui(0);
                statusBar()->showMessage(QString("Deleted skill %1").arg(skill_id));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.exec(skill_list_->mapToGlobal(pos));
    });

    left_splitter->addWidget(skill_list_widget);
    left_splitter->addWidget(object_skills_panel_);
    left_splitter->setSizes({400, 400});
    main_splitter->addWidget(left_splitter);

    // Center: graph view
    tree_scene_ = new TreeScene(this);
    graph_view_ = new ZoomableView(tree_scene_);
    graph_view_->setRenderHint(QPainter::Antialiasing);
    graph_view_->setDragMode(QGraphicsView::NoDrag);
    graph_view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    graph_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    graph_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    main_splitter->addWidget(graph_view_);

    connect(tree_scene_, &TreeScene::node_clicked, this, &MainWindow::on_scene_node_clicked);
    connect(tree_scene_, &TreeScene::empty_context_menu, this,
            [this](const QPointF& screen_pos) {
        if (db_path_.empty()) return;
        QMenu menu;
        menu.addAction("Create New Node...", [this]() {
            QStringList types;
            for (const auto& [id, tn] : data_.template_names)
                types << QString("%1 (%2)").arg(QString::fromStdString(tn.name)).arg(id);
            types.sort();
            bool ok;
            QString choice = QInputDialog::getItem(this, "Create Node",
                "Behavior type:", types, 0, false, &ok);
            if (!ok || choice.isEmpty()) return;
            // Extract ID from "Name (ID)" format
            int tmpl_id = choice.mid(choice.lastIndexOf('(') + 1).chopped(1).toInt();
            try {
                auto refresh_cb = [this](int sel) { refresh_ui(sel); };
                auto* cmd = new CreateBehaviorCommand(data_, db_path_, tmpl_id, refresh_cb);
                undo_stack_->push(cmd);
                int new_id = cmd->created_id();
                statusBar()->showMessage(
                    QString("Created behavior %1 — set it as a parameter value to wire it in")
                        .arg(new_id));
                goto_box_->setText(QString::number(new_id));
                on_goto_behavior();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.addAction("Load Template...", this, &MainWindow::on_load_template);
        menu.exec(screen_pos.toPoint());
    });
    connect(tree_scene_, &TreeScene::node_context_menu, this,
            [this](int behavior_id, const QPointF& screen_pos) {
        QMenu menu;
        menu.addAction(QString("Clone Subtree (beh %1)").arg(behavior_id),
                       [this, behavior_id]() {
            if (db_path_.empty()) return;
            try {
                auto refresh_cb = [this](int sel) { refresh_ui(sel); };
                auto* cmd = new CloneSubtreeCommand(data_, db_path_, behavior_id, refresh_cb);
                undo_stack_->push(cmd);
                int new_id = cmd->cloned_root_id();
                statusBar()->showMessage(
                    QString("Cloned subtree %1 -> %2").arg(behavior_id).arg(new_id));
                goto_box_->setText(QString::number(new_id));
                on_goto_behavior();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.addAction("Save Subtree as Template...", [this, behavior_id]() {
            QString path = qt_common::FileBrowserDialog::getSaveFileName(this, "Save Template", {},
                "JSON Template (*.json);;All Files (*)");
            if (path.isEmpty()) return;
            try {
                save_template(data_, behavior_id, path.toStdString());
                statusBar()->showMessage(
                    QString("Saved template for behavior %1").arg(behavior_id));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.addSeparator();
        menu.addAction(QString("View Details (beh %1)").arg(behavior_id),
                       [this, behavior_id]() {
            node_editor_->show_behavior(behavior_id, data_, current_skill_id_);
            tree_scene_->highlight_subtree(behavior_id);
        });
        menu.addSeparator();
        menu.addAction(QString("Delete Behavior %1").arg(behavior_id),
                       [this, behavior_id]() {
            if (db_path_.empty()) return;
            auto ans = QMessageBox::question(this, "Delete Behavior",
                QString("Delete behavior %1 and all its parameters?")
                    .arg(behavior_id));
            if (ans != QMessageBox::Yes) return;
            try {
                auto refresh_cb = [this](int sel) { refresh_ui(sel); };
                auto* cmd = new DeleteBehaviorCommand(data_, db_path_, behavior_id, refresh_cb);
                undo_stack_->push(cmd);
                refresh_ui(0);
                statusBar()->showMessage(QString("Deleted behavior %1").arg(behavior_id));
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Error", e.what());
            }
        });
        menu.exec(screen_pos.toPoint());
    });

    // Right panel: tree view + node editor
    auto* right_splitter = new QSplitter(Qt::Vertical);

    tree_model_ = new BehaviorTreeModel(this);
    tree_view_ = new QTreeView;
    tree_view_->setModel(tree_model_);
    tree_view_->setHeaderHidden(false);
    tree_view_->setAlternatingRowColors(true);
    right_splitter->addWidget(tree_view_);

    connect(tree_view_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        auto* node = tree_model_->node_at(current);
        if (node) {
            node_editor_->show_behavior(node->behavior_id, data_, current_skill_id_);
        }
    });

    node_editor_ = new NodeEditor;
    right_splitter->addWidget(node_editor_);
    connect(node_editor_, &NodeEditor::navigate_to_behavior,
            this, &MainWindow::on_navigate_to_behavior);
    // Refresh callback for undo commands
    auto refresh_cb = [this](int sel_beh) { refresh_ui(sel_beh); };

    connect(node_editor_, &NodeEditor::parameter_changed,
            this, [this, refresh_cb](int behavior_id, const QString& param_id, double value) {
        if (db_path_.empty() || refreshing_) return;
        try {
            if (force_cow_ || needs_cow(data_, current_skill_id_, behavior_id)) {
                auto* cmd = new CowParameterEditCommand(
                    data_, db_path_, current_skill_id_, behavior_id,
                    param_id.toStdString(), value, refresh_cb);
                undo_stack_->push(cmd);
                // Reload tree with cloned behavior selected, preserving view
                load_skill(current_skill_id_, cmd->cloned_root_id());
            } else {
                // Capture old value
                double old_val = 0;
                bool was_insert = true;
                auto pit = data_.params.find(behavior_id);
                if (pit != data_.params.end())
                    for (const auto& p : pit->second)
                        if (p.parameter_id == param_id.toStdString()) {
                            old_val = p.value;
                            was_insert = false;
                            break;
                        }

                save_parameter(data_, db_path_, behavior_id,
                               param_id.toStdString(), value);

                auto* cmd = new SaveParameterCommand(
                    data_, db_path_, behavior_id, param_id.toStdString(),
                    old_val, value, was_insert, refresh_cb);
                undo_stack_->push(cmd);
                refresh_ui(behavior_id);
            }
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString("Save failed: %1").arg(e.what()));
        }
    });

    connect(node_editor_, &NodeEditor::effect_changed,
            this, [this, refresh_cb](int behavior_id, int effect_id, const QString& effect_handle) {
        if (db_path_.empty() || refreshing_) return;
        try {
            if (force_cow_ || needs_cow(data_, current_skill_id_, behavior_id)) {
                auto* cmd = new CowEffectEditCommand(
                    data_, db_path_, current_skill_id_, behavior_id,
                    effect_id, effect_handle.toStdString(), refresh_cb);
                undo_stack_->push(cmd);
                load_skill(current_skill_id_, cmd->cloned_root_id());
            } else {
                auto bit = data_.behaviors.find(behavior_id);
                int old_eid = bit != data_.behaviors.end() ? bit->second.effect_id : 0;
                std::string old_eh = bit != data_.behaviors.end() ? bit->second.effect_handle : "";

                save_effect(data_, db_path_, behavior_id,
                            effect_id, effect_handle.toStdString());

                auto* cmd = new SaveEffectCommand(
                    data_, db_path_, behavior_id,
                    old_eid, old_eh, effect_id, effect_handle.toStdString(),
                    refresh_cb);
                undo_stack_->push(cmd);
                refresh_ui(behavior_id);
            }
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString("Save failed: %1").arg(e.what()));
        }
    });

    // Dynamic param add: creates new group of numbered params with value 0
    connect(node_editor_, &NodeEditor::dynamic_param_add,
            this, [this, refresh_cb](int behavior_id, const QStringList& param_names,
                                     const QStringList& param_types) {
        if (db_path_.empty() || refreshing_) return;
        try {
            std::vector<std::string> names;
            for (const auto& n : param_names) names.push_back(n.toStdString());

            // Apply the change
            for (const auto& name : names)
                save_parameter(data_, db_path_, behavior_id, name, 0.0);

            auto* cmd = new AddDynamicParamCommand(
                data_, db_path_, behavior_id, names, refresh_cb);
            undo_stack_->push(cmd);
            refresh_ui(behavior_id);
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString("Add param failed: %1").arg(e.what()));
        }
    });

    // Dynamic param remove: deletes a group of numbered params
    connect(node_editor_, &NodeEditor::dynamic_param_remove,
            this, [this, refresh_cb](int behavior_id, const QStringList& param_names) {
        if (db_path_.empty() || refreshing_) return;
        try {
            std::vector<std::string> names;
            for (const auto& n : param_names) names.push_back(n.toStdString());

            auto* cmd = new RemoveDynamicParamCommand(
                data_, db_path_, behavior_id, names, refresh_cb);
            undo_stack_->push(cmd);
            refresh_ui(behavior_id);
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString("Remove param failed: %1").arg(e.what()));
        }
    });

    // Object skills panel signals
    connect(object_skills_panel_, &ObjectSkillsPanel::navigate_to_skill,
            this, [this](int skill_id) {
        // Select and load the skill in the main editor
        for (int i = 0; i < skill_list_->count(); ++i) {
            auto* item = skill_list_->item(i);
            if (item->data(Qt::UserRole).toInt() == skill_id) {
                skill_list_->setCurrentItem(item);
                load_skill(skill_id);
                break;
            }
        }
    });

    connect(object_skills_panel_, &ObjectSkillsPanel::object_skill_added,
            this, [this, refresh_cb](int object_template, int skill_id, int cast_on_type) {
        if (db_path_.empty()) return;
        try {
            auto* cmd = new AddObjectSkillCommand(
                data_, db_path_, object_template, skill_id, cast_on_type, refresh_cb);
            undo_stack_->push(cmd);
            object_skills_panel_->refresh();
            // Also refresh the node editor's "Used By" section if viewing this skill
            if (current_skill_id_ == skill_id)
                node_editor_->show_skill_objects(skill_id, data_);
            statusBar()->showMessage(
                QString("Added skill %1 to LOT %2").arg(skill_id).arg(object_template));
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Error", e.what());
        }
    });

    connect(object_skills_panel_, &ObjectSkillsPanel::object_skill_removed,
            this, [this, refresh_cb](int object_template, int skill_id) {
        if (db_path_.empty()) return;
        try {
            auto* cmd = new RemoveObjectSkillCommand(
                data_, db_path_, object_template, skill_id, refresh_cb);
            undo_stack_->push(cmd);
            object_skills_panel_->refresh();
            if (current_skill_id_ == skill_id)
                node_editor_->show_skill_objects(skill_id, data_);
            statusBar()->showMessage(
                QString("Removed skill %1 from LOT %2").arg(skill_id).arg(object_template));
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Error", e.what());
        }
    });

    connect(object_skills_panel_, &ObjectSkillsPanel::object_skill_cast_type_changed,
            this, [this, refresh_cb](int object_template, int skill_id,
                                      int old_cast_on_type, int new_cast_on_type) {
        if (db_path_.empty()) return;
        try {
            update_object_skill_cast_type(data_, db_path_,
                                          object_template, skill_id, new_cast_on_type);
            auto* cmd = new EditObjectSkillCastTypeCommand(
                data_, db_path_, object_template, skill_id,
                old_cast_on_type, new_cast_on_type, refresh_cb);
            undo_stack_->push(cmd);
            object_skills_panel_->refresh();
            statusBar()->showMessage(
                QString("Changed castOnType for skill %1 on LOT %2 to %3")
                    .arg(skill_id).arg(object_template).arg(new_cast_on_type));
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "Error", e.what());
        }
    });

    main_splitter->addWidget(right_splitter);

    // Splitter proportions: 200, 700, 500
    main_splitter->setSizes({200, 700, 500});

    statusBar()->showMessage("Open a CDClient database to begin (File > Open)");
}

void MainWindow::refresh_ui(int select_behavior_id) {
    refreshing_ = true;
    populate_skill_list();
    object_skills_panel_->refresh();
    if (current_skill_id_ > 0) {
        bool skill_exists = false;
        for (const auto& s : data_.skills)
            if (s.skill_id == current_skill_id_) { skill_exists = true; break; }
        if (skill_exists)
            load_skill(current_skill_id_, select_behavior_id);
        else {
            current_skill_id_ = 0;
            tree_scene_->set_tree(nullptr);
            tree_model_->set_root(nullptr);
            node_editor_->clear();
        }
    }
    refreshing_ = false;
}

void MainWindow::load_database(const std::string& path) {
    db_path_ = path;
    undo_stack_->clear();
    try {
        data_ = load_cdclient(path);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", QString("Failed to load: %1").arg(e.what()));
        return;
    }

    // Persist for next launch
    QSettings settings;
    settings.setValue("last_cdclient", QString::fromStdString(path));

    // Load locale skill names if path is set
    QString locale_path = settings.value("locale_xml").toString();
    if (!locale_path.isEmpty() && QFile::exists(locale_path)) {
        data_.skill_names = load_locale_skill_names(locale_path.toStdString());
    }

    node_editor_->set_data(&data_);
    object_skills_panel_->set_data(&data_);
    populate_skill_list();
    statusBar()->showMessage(
        QString("Loaded %1 skills, %2 behaviors, %3 parameters, %4 types")
            .arg(data_.skills.size())
            .arg(data_.behaviors.size())
            .arg(data_.params.size())
            .arg(data_.template_names.size()));
}

void MainWindow::populate_skill_list() {
    skill_list_->clear();
    for (const auto& skill : data_.skills) {
        std::string root_type = data_.behavior_type_name(skill.behavior_id);

        // Try locale display name first
        auto nit = data_.skill_names.find(skill.skill_id);
        QString display_name = nit != data_.skill_names.end()
            ? QString::fromStdString(nit->second)
            : QString::fromStdString(root_type);

        QString label = QString("%1: %2").arg(skill.skill_id).arg(display_name);
        if (nit != data_.skill_names.end())
            label += QString(" (%1)").arg(QString::fromStdString(root_type));
        if (skill.imagination_cost > 0)
            label += QString(" [%1 imag]").arg(skill.imagination_cost);
        if (skill.cooldown > 0)
            label += QString(" [%1s cd]").arg(skill.cooldown, 0, 'g', 3);

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, skill.skill_id);
        item->setData(Qt::UserRole + 1,
            QString("%1 %2 %3").arg(skill.skill_id)
                .arg(display_name)
                .arg(QString::fromStdString(root_type)));
        skill_list_->addItem(item);
    }
}

void MainWindow::on_skill_selected(QListWidgetItem* item) {
    int skill_id = item->data(Qt::UserRole).toInt();
    load_skill(skill_id);
}

void MainWindow::load_skill(int skill_id, int select_behavior_id) {
    current_skill_id_ = skill_id;

    // Find the skill's root behavior
    int root_behavior_id = 0;
    for (const auto& s : data_.skills) {
        if (s.skill_id == skill_id) {
            root_behavior_id = s.behavior_id;
            break;
        }
    }

    // If root behavior doesn't exist (deleted), show empty
    if (root_behavior_id == 0 || !data_.behaviors.count(root_behavior_id)) {
        tree_scene_->set_tree(nullptr);
        tree_model_->set_root(nullptr);
        node_editor_->clear();
        statusBar()->showMessage(
            QString("Skill %1 — no root behavior").arg(skill_id));
        return;
    }

    // Build tree
    auto root = build_tree(data_, root_behavior_id);

    // Update graph view — preserve zoom/pan when reloading after an edit
    bool preserve_view = (select_behavior_id > 0);
    QTransform saved_transform;
    QPointF saved_center;
    if (preserve_view) {
        saved_transform = graph_view_->transform();
        saved_center = graph_view_->mapToScene(graph_view_->viewport()->rect().center());
    }
    tree_scene_->set_tree(root.get(), &data_);
    if (preserve_view) {
        graph_view_->setTransform(saved_transform);
        graph_view_->centerOn(saved_center);
    } else {
        graph_view_->fitInView(tree_scene_->sceneRect(), Qt::KeepAspectRatio);
    }

    // Update tree view
    tree_model_->set_root(std::move(root));
    tree_view_->expandAll();

    // Restore selection to the requested behavior, or fall back to root
    int show_id = (select_behavior_id > 0 && data_.behaviors.count(select_behavior_id))
        ? select_behavior_id : root_behavior_id;
    node_editor_->show_behavior(show_id, data_, current_skill_id_);
    node_editor_->show_skill_objects(skill_id, data_);
    tree_scene_->highlight_subtree(show_id);

    statusBar()->showMessage(
        QString("Skill %1 — root behavior %2").arg(skill_id).arg(root_behavior_id));
}

void MainWindow::on_scene_node_clicked(int behavior_id) {
    node_editor_->show_behavior(behavior_id, data_, current_skill_id_);
}

void MainWindow::on_navigate_to_behavior(int behavior_id) {
    if (!data_.behaviors.count(behavior_id)) return;

    // Build a tree from this behavior and display it
    auto root = build_tree(data_, behavior_id);
    tree_scene_->set_tree(root.get(), &data_);
    graph_view_->fitInView(tree_scene_->sceneRect(), Qt::KeepAspectRatio);
    tree_model_->set_root(std::move(root));
    tree_view_->expandAll();
    node_editor_->show_behavior(behavior_id, data_, current_skill_id_);
    tree_scene_->highlight_subtree(behavior_id);
    statusBar()->showMessage(QString("Behavior %1 — %2")
        .arg(behavior_id)
        .arg(QString::fromStdString(data_.behavior_type_name(behavior_id))));
}

void MainWindow::on_goto_behavior() {
    bool ok;
    int behavior_id = goto_box_->text().toInt(&ok);
    if (!ok || behavior_id <= 0) return;

    if (!data_.behaviors.count(behavior_id)) {
        statusBar()->showMessage(QString("Behavior %1 not found").arg(behavior_id));
        return;
    }

    // Build a standalone tree from this behavior
    auto root = build_tree(data_, behavior_id);
    tree_scene_->set_tree(root.get(), &data_);
    graph_view_->fitInView(tree_scene_->sceneRect(), Qt::KeepAspectRatio);
    tree_model_->set_root(std::move(root));
    tree_view_->expandAll();
    node_editor_->show_behavior(behavior_id, data_, current_skill_id_);
    tree_scene_->highlight_subtree(behavior_id);
    statusBar()->showMessage(QString("Behavior %1 — %2")
        .arg(behavior_id)
        .arg(QString::fromStdString(data_.behavior_type_name(behavior_id))));
}

void MainWindow::on_create_skill() {
    if (db_path_.empty()) return;

    QStringList types;
    for (const auto& [id, tn] : data_.template_names)
        types << QString("%1 (%2)").arg(QString::fromStdString(tn.name)).arg(id);
    types.sort();

    bool ok;
    QString choice = QInputDialog::getItem(this, "New Skill",
        "Root behavior type:", types, 0, false, &ok);
    if (!ok || choice.isEmpty()) return;

    int tmpl_id = choice.mid(choice.lastIndexOf('(') + 1).chopped(1).toInt();

    try {
        auto refresh_cb = [this](int sel) { refresh_ui(sel); };
        undo_stack_->beginMacro("Create skill");
        auto* beh_cmd = new CreateBehaviorCommand(data_, db_path_, tmpl_id, refresh_cb);
        undo_stack_->push(beh_cmd);
        int beh_id = beh_cmd->created_id();
        auto* skill_cmd = new CreateSkillCommand(data_, db_path_, beh_id, 0, 0.0, refresh_cb);
        undo_stack_->push(skill_cmd);
        undo_stack_->endMacro();
        int skill_id = skill_cmd->created_id();
        current_skill_id_ = skill_id;
        refresh_ui(beh_id);
        statusBar()->showMessage(
            QString("Created skill %1 with root %2 (behavior %3)")
                .arg(skill_id)
                .arg(QString::fromStdString(data_.template_names[tmpl_id].name))
                .arg(beh_id));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void MainWindow::on_clone_skill() {
    if (db_path_.empty() || current_skill_id_ == 0) return;

    try {
        auto refresh_cb = [this](int sel) { refresh_ui(sel); };
        auto* cmd = new CloneSkillCommand(data_, db_path_, current_skill_id_, refresh_cb);
        undo_stack_->push(cmd);
        int new_id = cmd->created_skill_id();
        current_skill_id_ = new_id;
        refresh_ui(0);
        statusBar()->showMessage(
            QString("Cloned skill %1 -> %2").arg(current_skill_id_).arg(new_id));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void MainWindow::on_clone_subtree() {
    if (db_path_.empty()) return;

    bool ok;
    int src_id = QInputDialog::getInt(this, "Clone Subtree",
        "Source behavior ID:", 0, 1, 999999, 1, &ok);
    if (!ok) return;

    try {
        auto refresh_cb = [this](int sel) { refresh_ui(sel); };
        auto* cmd = new CloneSubtreeCommand(data_, db_path_, src_id, refresh_cb);
        undo_stack_->push(cmd);
        int new_id = cmd->cloned_root_id();
        statusBar()->showMessage(
            QString("Cloned subtree %1 -> %2").arg(src_id).arg(new_id));
        goto_box_->setText(QString::number(new_id));
        on_goto_behavior();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void MainWindow::on_save_template() {
    if (db_path_.empty()) return;

    bool ok;
    int beh_id = QInputDialog::getInt(this, "Save Template",
        "Behavior ID to save as template:", 0, 1, 999999, 1, &ok);
    if (!ok) return;

    QString path = qt_common::FileBrowserDialog::getSaveFileName(
        this, "Save Behavior Template", {},
        "JSON Template (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        save_template(data_, beh_id, path.toStdString());
        statusBar()->showMessage(
            QString("Saved template for behavior %1 to %2").arg(beh_id).arg(path));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void MainWindow::on_load_template() {
    if (db_path_.empty()) return;

    QString path = qt_common::FileBrowserDialog::getOpenFileName(
        this, "Load Behavior Template", {},
        "JSON Template (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        auto refresh_cb = [this](int sel) { refresh_ui(sel); };
        auto* cmd = new LoadTemplateCommand(data_, db_path_,
                                            path.toStdString(), refresh_cb);
        undo_stack_->push(cmd);
        int new_id = cmd->created_root_id();
        statusBar()->showMessage(
            QString("Loaded template -> behavior %1").arg(new_id));
        goto_box_->setText(QString::number(new_id));
        on_goto_behavior();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", e.what());
    }
}

void MainWindow::on_set_locale() {
    QSettings settings;
    QString start_dir = settings.value("locale_xml").toString();
    if (start_dir.isEmpty()) start_dir = {};

    QString path = qt_common::FileBrowserDialog::getOpenFileName(
        this, "Select locale.xml", start_dir,
        "Locale XML (locale.xml);;All Files (*)");
    if (path.isEmpty()) return;

    settings.setValue("locale_xml", path);
    data_.skill_names = load_locale_skill_names(path.toStdString());
    populate_skill_list();
    statusBar()->showMessage(
        QString("Loaded %1 skill names from locale.xml").arg(data_.skill_names.size()));
}

void MainWindow::on_search_changed(const QString& text) {
    for (int i = 0; i < skill_list_->count(); ++i) {
        auto* item = skill_list_->item(i);
        QString searchable = item->data(Qt::UserRole + 1).toString();
        bool matches = text.isEmpty() ||
            item->text().contains(text, Qt::CaseInsensitive) ||
            searchable.contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

void MainWindow::on_open_file() {
    QString path = qt_common::FileBrowserDialog::getOpenFileName(
        this, "Open CDClient Database", {},
        "SQLite Database (*.sqlite *.db);;All Files (*)");
    if (path.isEmpty()) return;
    load_database(path.toStdString());
}

} // namespace skill_editor
