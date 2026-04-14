#pragma once
// main_window.h — Main application window for the skill/behavior tree editor.

#include "db_source.h"
#include "behavior_model.h"
#include "tree_scene.h"
#include "node_editor.h"
#include "object_skills_panel.h"

#include <QMainWindow>
#include <QTreeView>
#include "zoomable_view.h"
#include <QListWidget>
#include <QLineEdit>
#include <QSplitter>
#include <QSettings>
#include <QUndoStack>
#include <QDockWidget>

class QUndoView;

namespace skill_editor {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void load_database(const std::string& path);

private slots:
    void on_skill_selected(QListWidgetItem* item);
    void on_scene_node_clicked(int behavior_id);
    void on_navigate_to_behavior(int behavior_id);
    void on_search_changed(const QString& text);
    void on_open_file();
    void on_set_locale();
    void on_goto_behavior();
    void on_create_skill();
    void on_clone_skill();
    void on_clone_subtree();
    void on_save_template();
    void on_load_template();

private:
    void load_skill(int skill_id, int select_behavior_id = 0);
    void populate_skill_list();
    void refresh_ui(int select_behavior_id = 0);

    CdClientData data_;

    // Left panel: skill list + search
    QLineEdit* search_box_;
    QLineEdit* goto_box_;
    QListWidget* skill_list_;

    // Center: graph view
    TreeScene* tree_scene_;
    ZoomableView* graph_view_;

    // Right: tree view + node editor
    BehaviorTreeModel* tree_model_;
    QTreeView* tree_view_;
    NodeEditor* node_editor_;

    int current_skill_id_ = 0;
    std::string db_path_;
    bool refreshing_ = false; // guard against signal recursion during undo/redo
    bool force_cow_ = false;  // always copy-on-write, even for unique behaviors

    // Undo/redo
    QUndoStack* undo_stack_;
    QDockWidget* undo_dock_;
    QUndoView* undo_view_;

    // Object skills panel
    ObjectSkillsPanel* object_skills_panel_;
};

} // namespace skill_editor
