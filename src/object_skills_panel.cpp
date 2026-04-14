#include "object_skills_panel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QMessageBox>
#include <QMenu>
#include <algorithm>

namespace skill_editor {

// castOnType enum — derived from CDClient ObjectSkills data analysis + DarkflameServer
// InventoryComponent::FindSkill (0), FindBuffs (1), Item::Consume (3),
// Entity::PickupItem/Powerup path (2), and CDClient object type correlations.
// NULL entries in CDClient are NPC/enemy AI-driven skills (no player trigger).
static const std::vector<std::pair<int, QString>>& cast_on_type_entries() {
    static const std::vector<std::pair<int, QString>> entries = {
        {0, "0 - On Use"},
        {1, "1 - On Equip"},
        {2, "2 - Powerup"},
        {3, "3 - On Consume"},
        {4, "4 - Deployable"},
        {5, "5 - On Pickup"},
        {6, "6 - Car Powerup"},
    };
    return entries;
}

static QString cast_on_type_label(int value) {
    for (const auto& [v, label] : cast_on_type_entries())
        if (v == value) return label;
    return QString("%1 - Unknown (%1)").arg(value);
}

ObjectSkillsPanel::ObjectSkillsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* title = new QLabel("Object Skills");
    title->setFont(QFont("sans-serif", 10, QFont::Bold));
    layout->addWidget(title);

    // Search box for objects
    search_box_ = new QLineEdit;
    search_box_->setPlaceholderText("Search objects by LOT or name...");
    layout->addWidget(search_box_);
    connect(search_box_, &QLineEdit::textChanged, this, &ObjectSkillsPanel::on_search_changed);

    // Object list
    object_list_ = new QListWidget;
    object_list_->setMaximumHeight(200);
    layout->addWidget(object_list_);
    connect(object_list_, &QListWidget::itemClicked, this, &ObjectSkillsPanel::on_object_selected);

    // Skills section
    skills_label_ = new QLabel("Skills:");
    skills_label_->setFont(QFont("sans-serif", 9, QFont::Bold));
    layout->addWidget(skills_label_);

    skills_table_ = new QTableWidget;
    skills_table_->setColumnCount(3);
    skills_table_->setHorizontalHeaderLabels({"Skill ID", "Skill Name", "Cast On Type"});
    skills_table_->horizontalHeader()->setStretchLastSection(true);
    skills_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    skills_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    skills_table_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(skills_table_);

    // Double-click skill ID or name navigates to that skill
    connect(skills_table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int col) {
        if (col == 2) return; // col 2 has the combo box
        auto* item = skills_table_->item(row, 0);
        if (!item) return;
        int skill_id = item->data(Qt::UserRole).toInt();
        emit navigate_to_skill(skill_id);
    });

    // Right-click context menu on skills table
    connect(skills_table_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        auto* item = skills_table_->itemAt(pos);
        if (!item) return;
        int row = item->row();
        auto* id_item = skills_table_->item(row, 0);
        if (!id_item) return;
        int skill_id = id_item->data(Qt::UserRole).toInt();

        QMenu menu;
        menu.addAction(QString("Go to Skill %1").arg(skill_id), [this, skill_id]() {
            emit navigate_to_skill(skill_id);
        });
        menu.addSeparator();
        menu.addAction(QString("Remove Skill %1").arg(skill_id), [this, skill_id]() {
            emit object_skill_removed(current_lot_, skill_id);
        });
        menu.exec(skills_table_->mapToGlobal(pos));
    });

    // Inline add controls: LOT + Skill ID + Add button
    auto* add_layout = new QHBoxLayout;
    add_layout->addWidget(new QLabel("LOT:"));
    lot_spin_ = new QSpinBox;
    lot_spin_->setRange(1, 999999999);
    lot_spin_->setToolTip("Object Template (LOT) to assign the skill to");
    add_layout->addWidget(lot_spin_);

    add_layout->addWidget(new QLabel("Skill:"));
    skill_id_spin_ = new QSpinBox;
    skill_id_spin_->setRange(1, 999999);
    skill_id_spin_->setToolTip("Skill ID to assign");
    add_layout->addWidget(skill_id_spin_);

    add_btn_ = new QPushButton("Add");
    add_layout->addWidget(add_btn_);
    layout->addLayout(add_layout);

    connect(add_btn_, &QPushButton::clicked, this, &ObjectSkillsPanel::on_add_clicked);

    // Remove button
    auto* rm_layout = new QHBoxLayout;
    remove_btn_ = new QPushButton("- Remove Selected");
    remove_btn_->setEnabled(false);
    rm_layout->addWidget(remove_btn_);
    rm_layout->addStretch();
    layout->addLayout(rm_layout);

    connect(remove_btn_, &QPushButton::clicked, this, &ObjectSkillsPanel::on_remove_skill);

    // Enable remove button when a row is selected
    connect(skills_table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        remove_btn_->setEnabled(skills_table_->currentRow() >= 0);
    });
}

void ObjectSkillsPanel::set_data(CdClientData* data) {
    data_ = data;
    current_lot_ = 0;
    populate_object_list();
    skills_table_->setRowCount(0);
    skills_label_->setText("Skills:");
    remove_btn_->setEnabled(false);
}

void ObjectSkillsPanel::refresh() {
    populate_object_list();
    // Re-select the current LOT in the list if it still exists
    if (current_lot_ > 0) {
        for (int i = 0; i < object_list_->count(); ++i) {
            auto* item = object_list_->item(i);
            if (item->data(Qt::UserRole).toInt() == current_lot_) {
                object_list_->setCurrentItem(item);
                break;
            }
        }
        show_object_skills(current_lot_);
    }
}

void ObjectSkillsPanel::populate_object_list() {
    object_list_->clear();
    if (!data_) return;

    struct ObjEntry {
        int lot;
        QString name;
        int skill_count;
    };
    std::vector<ObjEntry> entries;

    for (const auto& [lot, skills] : data_->object_skills) {
        auto nit = data_->object_names.find(lot);
        QString name = nit != data_->object_names.end()
            ? QString::fromStdString(nit->second) : QString();
        entries.push_back({lot, name, static_cast<int>(skills.size())});
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.lot < b.lot; });

    for (const auto& e : entries) {
        QString label = QString("%1").arg(e.lot);
        if (!e.name.isEmpty())
            label += QString(": %1").arg(e.name);
        label += QString(" [%1 skills]").arg(e.skill_count);

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, e.lot);
        item->setData(Qt::UserRole + 1,
            QString("%1 %2").arg(e.lot).arg(e.name));
        object_list_->addItem(item);
    }
}

void ObjectSkillsPanel::on_search_changed(const QString& text) {
    for (int i = 0; i < object_list_->count(); ++i) {
        auto* item = object_list_->item(i);
        QString searchable = item->data(Qt::UserRole + 1).toString();
        bool matches = text.isEmpty() ||
            item->text().contains(text, Qt::CaseInsensitive) ||
            searchable.contains(text, Qt::CaseInsensitive);
        item->setHidden(!matches);
    }
}

void ObjectSkillsPanel::on_object_selected(QListWidgetItem* item) {
    int lot = item->data(Qt::UserRole).toInt();
    current_lot_ = lot;
    lot_spin_->setValue(lot);
    show_object_skills(lot);
}

void ObjectSkillsPanel::show_object_skills(int lot) {
    if (!data_) return;
    updating_table_ = true;

    auto nit = data_->object_names.find(lot);
    QString name = nit != data_->object_names.end()
        ? QString::fromStdString(nit->second) : QString("LOT %1").arg(lot);
    skills_label_->setText(QString("Skills for %1 (LOT %2):").arg(name).arg(lot));

    skills_table_->setRowCount(0);
    auto it = data_->object_skills.find(lot);
    if (it == data_->object_skills.end()) {
        updating_table_ = false;
        return;
    }

    const auto& skills = it->second;
    skills_table_->setRowCount(static_cast<int>(skills.size()));

    for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
        const auto& os = skills[i];

        auto* id_item = new QTableWidgetItem(QString::number(os.skill_id));
        id_item->setData(Qt::UserRole, os.skill_id);
        id_item->setForeground(QColor(80, 160, 255));
        id_item->setFlags(id_item->flags() & ~Qt::ItemIsEditable);
        skills_table_->setItem(i, 0, id_item);

        // Skill name from locale or behavior type
        QString skill_name;
        auto nit2 = data_->skill_names.find(os.skill_id);
        if (nit2 != data_->skill_names.end()) {
            skill_name = QString::fromStdString(nit2->second);
        } else {
            for (const auto& s : data_->skills) {
                if (s.skill_id == os.skill_id) {
                    skill_name = QString::fromStdString(
                        data_->behavior_type_name(s.behavior_id));
                    break;
                }
            }
        }
        auto* name_item = new QTableWidgetItem(skill_name);
        name_item->setFlags(name_item->flags() & ~Qt::ItemIsEditable);
        skills_table_->setItem(i, 1, name_item);

        // Cast On Type dropdown
        auto* combo = new QComboBox;
        for (const auto& [v, label] : cast_on_type_entries())
            combo->addItem(label, v);

        // Select current value
        int cur_idx = combo->findData(os.cast_on_type);
        if (cur_idx < 0) {
            // Value not in known list — add it
            combo->addItem(cast_on_type_label(os.cast_on_type), os.cast_on_type);
            cur_idx = combo->count() - 1;
        }
        combo->setCurrentIndex(cur_idx);

        int skill_id = os.skill_id;
        int old_val = os.cast_on_type;
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, skill_id, combo, old_val]() {
            if (updating_table_) return;
            int new_val = combo->currentData().toInt();
            if (new_val == old_val) return;
            emit object_skill_cast_type_changed(current_lot_, skill_id, old_val, new_val);
        });

        skills_table_->setCellWidget(i, 2, combo);
    }

    skills_table_->resizeColumnsToContents();
    skills_table_->horizontalHeader()->setStretchLastSection(true);
    updating_table_ = false;
}

void ObjectSkillsPanel::on_add_clicked() {
    if (!data_) return;

    int lot = lot_spin_->value();
    int skill_id = skill_id_spin_->value();
    if (lot <= 0 || skill_id <= 0) return;

    // Check if skill exists
    bool skill_exists = false;
    for (const auto& s : data_->skills)
        if (s.skill_id == skill_id) { skill_exists = true; break; }
    if (!skill_exists) {
        QMessageBox::warning(this, "Error",
            QString("Skill %1 does not exist in the database.").arg(skill_id));
        return;
    }

    // Check if already assigned
    auto it = data_->object_skills.find(lot);
    if (it != data_->object_skills.end())
        for (const auto& o : it->second)
            if (o.skill_id == skill_id) {
                QMessageBox::warning(this, "Error",
                    QString("Skill %1 is already assigned to LOT %2.")
                        .arg(skill_id).arg(lot));
                return;
            }

    current_lot_ = lot;
    emit object_skill_added(lot, skill_id, 0);
}

void ObjectSkillsPanel::on_remove_skill() {
    if (!data_ || current_lot_ == 0) return;

    int row = skills_table_->currentRow();
    if (row < 0) return;

    auto* id_item = skills_table_->item(row, 0);
    if (!id_item) return;
    int skill_id = id_item->data(Qt::UserRole).toInt();

    emit object_skill_removed(current_lot_, skill_id);
}

} // namespace skill_editor
