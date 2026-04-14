#pragma once
// node_editor.h — Side panel showing behavior node properties.
//
// Displays the selected behavior's type, ID, effect, parameters,
// reference counts, and dynamic (repeatable) parameter add/remove controls.

#include "db_source.h"
#include "param_delegate.h"

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QListWidget>

namespace skill_editor {

class NodeEditor : public QWidget {
    Q_OBJECT
public:
    explicit NodeEditor(QWidget* parent = nullptr);

    void show_behavior(int behavior_id, const CdClientData& data, int current_skill_id = 0);
    void show_skill_objects(int skill_id, const CdClientData& data);
    void set_data(const CdClientData* data);
    void clear();
    int current_behavior_id() const { return current_behavior_id_; }

signals:
    void navigate_to_behavior(int behavior_id);
    void parameter_changed(int behavior_id, const QString& parameter_id, double value);
    void effect_changed(int behavior_id, int effect_id, const QString& effect_handle);
    // Dynamic param add: creates param(s) for a group with next available number
    // param_names contains the param names to create (e.g. ["behavior 4", "value 4"])
    // param_types contains the types (e.g. ["behavior", "float"])
    void dynamic_param_add(int behavior_id, const QStringList& param_names,
                           const QStringList& param_types);
    // Dynamic param remove: deletes specific numbered params (group members)
    void dynamic_param_remove(int behavior_id, const QStringList& param_names);

private:
    QLabel* title_label_;
    QLabel* type_label_;
    QLabel* id_label_;
    QLabel* refs_label_;
    QPushButton* refs_info_btn_;
    struct RefEntry {
        QString label;
        int behavior_id; // 0 for skill-root refs (no behavior to navigate to)
    };
    std::vector<RefEntry> ref_entries_;
    QSpinBox* effect_id_spin_;
    QLineEdit* effect_handle_edit_;
    QTableWidget* param_table_;
    QWidget* dynamic_btn_container_;
    ParamDelegate* param_delegate_ = nullptr;
    QLabel* objects_label_;
    QTableWidget* objects_table_;
    int current_behavior_id_ = 0;
};

} // namespace skill_editor
