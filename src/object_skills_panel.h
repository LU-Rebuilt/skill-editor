#pragma once
// object_skills_panel.h — Panel for browsing and editing skills assigned to objects.
//
// Allows searching for objects by LOT or name, viewing all skills assigned
// to a selected object, and adding/removing/editing skill assignments.
// All editing is inline — no popup dialogs.

#include "db_source.h"

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>

namespace skill_editor {

class ObjectSkillsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ObjectSkillsPanel(QWidget* parent = nullptr);

    void set_data(CdClientData* data);
    void refresh();

signals:
    // Emitted when user wants to navigate to a skill in the main editor
    void navigate_to_skill(int skill_id);
    // Emitted when user adds a skill to the selected object
    void object_skill_added(int object_template, int skill_id, int cast_on_type);
    // Emitted when user removes a skill from the selected object
    void object_skill_removed(int object_template, int skill_id);
    // Emitted when user changes the castOnType for an assignment
    void object_skill_cast_type_changed(int object_template, int skill_id,
                                        int old_cast_on_type, int new_cast_on_type);

private slots:
    void on_search_changed(const QString& text);
    void on_object_selected(QListWidgetItem* item);
    void on_add_clicked();
    void on_remove_skill();

private:
    void populate_object_list();
    void show_object_skills(int lot);

    CdClientData* data_ = nullptr;
    int current_lot_ = 0;
    bool updating_table_ = false; // guard against signal recursion

    QLineEdit* search_box_;
    QListWidget* object_list_;
    QLabel* skills_label_;
    QTableWidget* skills_table_;

    // Inline add controls
    QSpinBox* lot_spin_;
    QSpinBox* skill_id_spin_;
    QPushButton* add_btn_;
    QPushButton* remove_btn_;
};

} // namespace skill_editor
