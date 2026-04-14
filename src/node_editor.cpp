#include "node_editor.h"

#include <QHeaderView>
#include <QFont>
#include <QApplication>
#include <QMessageBox>
#include <unordered_set>

namespace skill_editor {

NodeEditor::NodeEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    title_label_ = new QLabel("No selection");
    title_label_->setFont(QFont("sans-serif", 12, QFont::Bold));
    layout->addWidget(title_label_);

    type_label_ = new QLabel;
    layout->addWidget(type_label_);

    id_label_ = new QLabel;
    layout->addWidget(id_label_);

    // References section
    auto* refs_layout = new QHBoxLayout;
    refs_label_ = new QLabel;
    refs_layout->addWidget(refs_label_);
    refs_info_btn_ = new QPushButton("?");
    refs_info_btn_->setFixedSize(22, 22);
    refs_info_btn_->setToolTip("Show detailed reference list");
    refs_layout->addWidget(refs_info_btn_);
    refs_layout->addStretch();
    layout->addLayout(refs_layout);

    connect(refs_info_btn_, &QPushButton::clicked, this, [this]() {
        if (ref_entries_.empty()) return;

        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(QString("References to Behavior %1").arg(current_behavior_id_));
        dlg->resize(400, 300);
        auto* layout = new QVBoxLayout(dlg);

        auto* list = new QListWidget;
        for (const auto& entry : ref_entries_) {
            auto* item = new QListWidgetItem(entry.label);
            item->setData(Qt::UserRole, entry.behavior_id);
            if (entry.behavior_id > 0)
                item->setForeground(QColor(80, 160, 255));
            list->addItem(item);
        }
        layout->addWidget(list);

        auto* hint = new QLabel("Double-click to navigate to a behavior.");
        hint->setStyleSheet("color: #788297;");
        layout->addWidget(hint);

        connect(list, &QListWidget::itemDoubleClicked, this,
                [this, dlg](QListWidgetItem* item) {
            int beh_id = item->data(Qt::UserRole).toInt();
            if (beh_id > 0) {
                dlg->accept();
                emit navigate_to_behavior(beh_id);
            }
        });

        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    // Effect fields (editable)
    auto* effect_layout = new QHBoxLayout;
    effect_layout->addWidget(new QLabel("Effect ID:"));
    effect_id_spin_ = new QSpinBox;
    effect_id_spin_->setRange(0, 999999);
    effect_id_spin_->setSpecialValueText("None");
    effect_layout->addWidget(effect_id_spin_);
    effect_layout->addWidget(new QLabel("Handle:"));
    effect_handle_edit_ = new QLineEdit;
    effect_handle_edit_->setPlaceholderText("(empty)");
    effect_layout->addWidget(effect_handle_edit_);
    layout->addLayout(effect_layout);

    connect(effect_id_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int val) {
        if (current_behavior_id_ == 0) return;
        emit effect_changed(current_behavior_id_, val,
                            effect_handle_edit_->text());
    });
    connect(effect_handle_edit_, &QLineEdit::editingFinished, this,
            [this]() {
        if (current_behavior_id_ == 0) return;
        emit effect_changed(current_behavior_id_, effect_id_spin_->value(),
                            effect_handle_edit_->text());
    });

    // Parameters table
    param_table_ = new QTableWidget;
    param_table_->setColumnCount(2);
    param_table_->setHorizontalHeaderLabels({"Parameter", "Value"});
    param_table_->horizontalHeader()->setStretchLastSection(true);
    param_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    param_table_->setEditTriggers(QAbstractItemView::DoubleClicked);
    layout->addWidget(param_table_);

    // Container for dynamic param add buttons (rebuilt per behavior)
    dynamic_btn_container_ = new QWidget;
    dynamic_btn_container_->setLayout(new QVBoxLayout);
    dynamic_btn_container_->layout()->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(dynamic_btn_container_);

    // Ctrl+click on a behavior reference navigates to it.
    // Plain double-click edits the value (all values are editable numbers).
    connect(param_table_, &QTableWidget::cellClicked, this,
            [this](int row, int col) {
        if (col != 1) return;
        auto* val_item = param_table_->item(row, 1);
        if (!val_item) return;
        QVariant raw = val_item->data(Qt::UserRole);
        if (raw.isValid() && raw.toInt() > 0 &&
            (QApplication::keyboardModifiers() & Qt::ControlModifier)) {
            emit navigate_to_behavior(raw.toInt());
        }
    });

    // Edit handler: when user edits a value cell, emit parameter_changed
    connect(param_table_, &QTableWidget::cellChanged, this,
            [this](int row, int col) {
        if (col != 1 || current_behavior_id_ == 0) return;
        auto* name_item = param_table_->item(row, 0);
        auto* val_item = param_table_->item(row, 1);
        if (!name_item || !val_item) return;
        bool ok;
        double val = val_item->text().toDouble(&ok);
        if (!ok) return;
        emit parameter_changed(current_behavior_id_,
                               name_item->text(), val);
    });

    // Objects that use this skill
    objects_label_ = new QLabel("Used By:");
    objects_label_->setFont(QFont("sans-serif", 9, QFont::Bold));
    layout->addWidget(objects_label_);

    objects_table_ = new QTableWidget;
    objects_table_->setColumnCount(2);
    objects_table_->setHorizontalHeaderLabels({"LOT", "Object Name"});
    objects_table_->horizontalHeader()->setStretchLastSection(true);
    objects_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    objects_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objects_table_->setMaximumHeight(150);
    layout->addWidget(objects_table_);
}

void NodeEditor::set_data(const CdClientData* data) {
    if (param_delegate_) {
        param_table_->setItemDelegateForColumn(1, nullptr);
        delete param_delegate_;
    }
    param_delegate_ = new ParamDelegate(data, this);
    param_table_->setItemDelegateForColumn(1, param_delegate_);
}

void NodeEditor::show_behavior(int behavior_id, const CdClientData& data, int current_skill_id) {
    current_behavior_id_ = behavior_id;
    auto bit = data.behaviors.find(behavior_id);
    if (bit == data.behaviors.end()) {
        clear();
        return;
    }

    const auto& bt = bit->second;
    std::string type_name = data.behavior_type_name(behavior_id);

    title_label_->setText(QString::fromStdString(type_name));
    type_label_->setText(QString("Template: %1 (%2)")
        .arg(bt.template_id)
        .arg(QString::fromStdString(type_name)));
    id_label_->setText(QString("Behavior ID: %1").arg(behavior_id));

    // Build references info — count tree refs vs total refs
    {
        // Collect behaviors in current skill tree via BFS
        std::unordered_set<int> in_tree;
        if (current_skill_id > 0) {
            int root_id = 0;
            for (const auto& s : data.skills)
                if (s.skill_id == current_skill_id) { root_id = s.behavior_id; break; }
            if (root_id > 0) {
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
            }
        }

        int tree_refs = 0, total_refs = 0;
        ref_entries_.clear();

        // Skills that use this as root
        for (const auto& s : data.skills) {
            if (s.behavior_id == behavior_id) {
                ++total_refs;
                if (s.skill_id == current_skill_id) ++tree_refs;
                ref_entries_.push_back({
                    QString("Skill %1 (root)").arg(s.skill_id), 0});
            }
        }

        // Parent behaviors that reference this via parameters (count once per parent)
        for (const auto& [bid, pvec] : data.params) {
            bool counted = false;
            QStringList param_names;
            for (const auto& p : pvec) {
                if (static_cast<int>(p.value) == behavior_id &&
                    is_behavior_ref_param(p.parameter_id)) {
                    if (!counted) {
                        ++total_refs;
                        if (in_tree.count(bid)) ++tree_refs;
                        counted = true;
                    }
                    param_names << QString::fromStdString(p.parameter_id);
                }
            }
            if (counted) {
                QString parent_type = QString::fromStdString(
                    data.behavior_type_name(bid));
                ref_entries_.push_back({
                    QString("Behavior %1 (%2) via %3")
                        .arg(bid).arg(parent_type).arg(param_names.join(", ")),
                    bid});
            }
        }

        refs_label_->setText(QString("Refs: %1 tree / %2 total")
            .arg(tree_refs).arg(total_refs));
        refs_info_btn_->setVisible(total_refs > 0);
    }

    effect_id_spin_->blockSignals(true);
    effect_id_spin_->setValue(bt.effect_id);
    effect_id_spin_->blockSignals(false);
    effect_handle_edit_->blockSignals(true);
    effect_handle_edit_->setText(QString::fromStdString(bt.effect_handle));
    effect_handle_edit_->blockSignals(false);

    // Close any active editor before rebuilding the table to avoid
    // "commitData called with an editor that does not belong to this view"
    QModelIndex editing = param_table_->currentIndex();
    if (editing.isValid()) {
        auto* item = param_table_->item(editing.row(), editing.column());
        if (item) param_table_->closePersistentEditor(item);
    }

    // Build param table from schema + dynamic params
    param_table_->blockSignals(true);
    param_table_->setRowCount(0);
    param_table_->setColumnCount(3);
    param_table_->setHorizontalHeaderLabels({"Parameter", "Value", "Type"});

    const auto& schema = get_behavior_param_schema(type_name);
    const auto& dynamic_defs = get_behavior_dynamic_params(type_name);

    // Build lookup of existing params
    std::unordered_map<std::string, double> existing;
    auto pit = data.params.find(behavior_id);
    if (pit != data.params.end()) {
        for (const auto& p : pit->second)
            existing[p.parameter_id] = p.value;
    }

    struct ParamEntry {
        std::string name;
        std::string type;
        bool is_dynamic = false;
    };
    std::vector<ParamEntry> param_list;
    std::unordered_set<std::string> shown;

    // 1. Static schema params
    for (const auto& pd : schema) {
        param_list.push_back({pd.name, pd.type, false});
        shown.insert(pd.name);
    }

    // 2. Dynamic params from DB matching known group prefixes (sorted by number)
    for (const auto& dd : dynamic_defs) {
        if (dd.members.empty()) continue;
        const auto& primary = dd.members[0];
        // Find all numbered instances of the primary prefix
        struct DynMatch { int num; };
        std::vector<DynMatch> matches;
        for (const auto& [pname, pval] : existing) {
            if (pname.size() > primary.prefix.size() &&
                pname.substr(0, primary.prefix.size()) == primary.prefix &&
                !shown.count(pname)) {
                try { matches.push_back({std::stoi(pname.substr(primary.prefix.size()))}); }
                catch (...) {}
            }
        }
        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.num < b.num; });
        // Add all group members for each number
        for (const auto& match : matches) {
            for (const auto& m : dd.members) {
                std::string pname = m.prefix + std::to_string(match.num);
                param_list.push_back({pname, m.type, true});
                shown.insert(pname);
            }
        }
    }

    // 3. Any remaining DB params not in schema or dynamic (custom/unknown)
    if (pit != data.params.end()) {
        for (const auto& p : pit->second) {
            if (!shown.count(p.parameter_id)) {
                std::string ptype = is_behavior_ref_param(p.parameter_id) ? "behavior" : "float";
                param_list.push_back({p.parameter_id, ptype, false});
            }
        }
    }

    param_table_->setRowCount(static_cast<int>(param_list.size()));
    for (int i = 0; i < static_cast<int>(param_list.size()); ++i) {
        const auto& entry = param_list[i];
        const auto& pname = entry.name;
        const auto& ptype = entry.type;

        auto* name_item = new QTableWidgetItem(QString::fromStdString(pname));
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        if (entry.is_dynamic)
            name_item->setForeground(QColor(100, 180, 140)); // green tint for dynamic

        auto* type_item = new QTableWidgetItem(QString::fromStdString(ptype));
        type_item->setFlags(type_item->flags() & ~Qt::ItemIsEditable);
        type_item->setForeground(QColor(120, 130, 145));

        auto eit = existing.find(pname);
        bool has_value = (eit != existing.end());
        bool is_ref = (ptype == "behavior");

        auto* val_item = new QTableWidgetItem;
        if (has_value) {
            double val = eit->second;
            if (val == static_cast<int>(val))
                val_item->setText(QString::number(static_cast<int>(val)));
            else
                val_item->setText(QString::number(val, 'f', 4));

            if (is_ref) {
                int ref_id = static_cast<int>(val);
                val_item->setForeground(QColor(80, 160, 255));
                val_item->setToolTip(QString("-> %1 (behavior %2)")
                    .arg(QString::fromStdString(data.behavior_type_name(ref_id)))
                    .arg(ref_id));
                val_item->setData(Qt::UserRole, ref_id);
            }
        } else {
            val_item->setText("—");
            val_item->setForeground(QColor(80, 85, 95));
        }

        param_table_->setItem(i, 0, name_item);
        param_table_->setItem(i, 1, val_item);
        param_table_->setItem(i, 2, type_item);
    }

    // Fit parameter name and type columns to content, give remaining space to value
    param_table_->resizeColumnToContents(0); // Parameter name
    param_table_->resizeColumnToContents(2); // Type
    param_table_->horizontalHeader()->setStretchLastSection(false);
    param_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    param_table_->blockSignals(false);

    // Rebuild dynamic param add/remove buttons
    {
        auto* container_layout = dynamic_btn_container_->layout();
        // Clear existing buttons
        while (auto* child = container_layout->takeAt(0)) {
            if (auto* w = child->widget()) {
                w->setParent(nullptr);
                delete w;
            }
            delete child;
        }

        for (const auto& dd : dynamic_defs) {
            if (dd.members.empty()) continue;
            const auto& primary = dd.members[0];

            // Find the highest numbered instance via the primary prefix
            int max_num = 0;
            for (const auto& [pname, pval] : existing) {
                if (pname.size() > primary.prefix.size() &&
                    pname.substr(0, primary.prefix.size()) == primary.prefix) {
                    try {
                        int n = std::stoi(pname.substr(primary.prefix.size()));
                        if (n > max_num) max_num = n;
                    } catch (...) {}
                }
            }

            auto* row_widget = new QWidget;
            auto* row = new QHBoxLayout(row_widget);
            row->setContentsMargins(0, 0, 0, 0);

            // Build add label and param names for next number
            int next = max_num + 1;
            QStringList add_names, add_types, add_labels;
            for (const auto& m : dd.members) {
                add_names << QString::fromStdString(m.prefix + std::to_string(next));
                add_types << QString::fromStdString(m.type);
                add_labels << QString::fromStdString(m.prefix + std::to_string(next));
            }

            auto* add_btn = new QPushButton("+ Add " + add_labels.join(" / "));
            add_btn->setStyleSheet("color: #64b48c; text-align: left;");
            connect(add_btn, &QPushButton::clicked, this,
                    [this, add_names, add_types]() {
                if (current_behavior_id_ == 0) return;
                emit dynamic_param_add(current_behavior_id_, add_names, add_types);
            });
            row->addWidget(add_btn);

            // Remove button (removes last numbered group instance)
            if (max_num > 0) {
                QStringList rm_names, rm_labels;
                for (const auto& m : dd.members) {
                    rm_names << QString::fromStdString(m.prefix + std::to_string(max_num));
                    rm_labels << QString::fromStdString(m.prefix + std::to_string(max_num));
                }
                auto* rm_btn = new QPushButton("- Remove " + rm_labels.join(" / "));
                rm_btn->setStyleSheet("color: #cc6666; text-align: left;");
                connect(rm_btn, &QPushButton::clicked, this,
                        [this, rm_names]() {
                    if (current_behavior_id_ == 0) return;
                    emit dynamic_param_remove(current_behavior_id_, rm_names);
                });
                row->addWidget(rm_btn);
            }
            row->addStretch();
            container_layout->addWidget(row_widget);
        }
    }
}

void NodeEditor::show_skill_objects(int skill_id, const CdClientData& data) {
    objects_table_->setRowCount(0);
    auto it = data.skill_objects.find(skill_id);
    if (it == data.skill_objects.end()) {
        objects_label_->setText("Used By: (none)");
        return;
    }
    objects_label_->setText(QString("Used By: (%1 objects)").arg(it->second.size()));
    objects_table_->setRowCount(static_cast<int>(it->second.size()));
    for (int i = 0; i < static_cast<int>(it->second.size()); ++i) {
        const auto& os = it->second[i];
        objects_table_->setItem(i, 0,
            new QTableWidgetItem(QString::number(os.object_template)));
        auto nit = data.object_names.find(os.object_template);
        QString name = nit != data.object_names.end()
            ? QString::fromStdString(nit->second)
            : QString("LOT %1").arg(os.object_template);
        objects_table_->setItem(i, 1, new QTableWidgetItem(name));
    }
    objects_table_->resizeColumnsToContents();
}

void NodeEditor::clear() {
    title_label_->setText("No selection");
    type_label_->clear();
    id_label_->clear();
    refs_label_->clear();
    ref_entries_.clear();
    refs_info_btn_->setVisible(false);
    effect_id_spin_->blockSignals(true);
    effect_id_spin_->setValue(0);
    effect_id_spin_->blockSignals(false);
    effect_handle_edit_->blockSignals(true);
    effect_handle_edit_->clear();
    effect_handle_edit_->blockSignals(false);
    param_table_->setRowCount(0);
    // Clear dynamic buttons
    {
        auto* lay = dynamic_btn_container_->layout();
        while (auto* child = lay->takeAt(0)) {
            if (auto* w = child->widget()) {
                w->setParent(nullptr);
                delete w;
            }
            delete child;
        }
    }
    objects_table_->setRowCount(0);
    objects_label_->setText("Used By:");
}

} // namespace skill_editor
