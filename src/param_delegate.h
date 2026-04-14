#pragma once
// param_delegate.h — Custom item delegate for behavior parameter value editing.
//
// Provides type-aware editors:
//   bool     → QComboBox with "false (0)" / "true (1)"
//   behavior → QComboBox with searchable list of all behaviors (ID + type name)
//   int      → QSpinBox
//   float    → QDoubleSpinBox

#include "db_source.h"

#include <QStyledItemDelegate>

namespace skill_editor {

class ParamDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ParamDelegate(const CdClientData* data, QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;

private:
    const CdClientData* data_;
    // Get the type string from column 2 of the same row
    QString param_type(const QModelIndex& index) const;
};

} // namespace skill_editor
