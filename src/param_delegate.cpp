#include "param_delegate.h"

#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCompleter>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

namespace skill_editor {

ParamDelegate::ParamDelegate(const CdClientData* data, QObject* parent)
    : QStyledItemDelegate(parent), data_(data) {}

QString ParamDelegate::param_type(const QModelIndex& index) const {
    // Type is in column 2 of the same row
    auto type_idx = index.sibling(index.row(), 2);
    return type_idx.data().toString();
}

QWidget* ParamDelegate::createEditor(QWidget* parent,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    // Only edit column 1 (value)
    if (index.column() != 1)
        return QStyledItemDelegate::createEditor(parent, option, index);

    QString type = param_type(index);

    if (type == "bool") {
        auto* combo = new QComboBox(parent);
        combo->addItem("0  (false)", 0.0);
        combo->addItem("1  (true)", 1.0);
        return combo;
    }

    if (type == "behavior") {
        auto* spin = new QSpinBox(parent);
        spin->setRange(0, 999999);
        return spin;
    }

    if (type == "int") {
        auto* spin = new QSpinBox(parent);
        spin->setRange(-999999, 999999);
        return spin;
    }

    // float or unknown — double spinbox
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(-999999.0, 999999.0);
    spin->setDecimals(4);
    return spin;
}

void ParamDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString type = param_type(index);
    QString val_text = index.data().toString();

    if (type == "bool") {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (combo) {
            int v = val_text.toInt();
            combo->setCurrentIndex(v ? 1 : 0);
        }
        return;
    }

    if (type == "behavior") {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (spin) spin->setValue(val_text.toInt());
        return;
    }

    if (type == "int") {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (spin) spin->setValue(val_text.toInt());
        return;
    }

    auto* spin = qobject_cast<QDoubleSpinBox*>(editor);
    if (spin) spin->setValue(val_text.toDouble());
}

void ParamDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                  const QModelIndex& index) const {
    QString type = param_type(index);

    if (type == "bool") {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (combo) {
            double val = combo->currentData().toDouble();
            model->setData(index, QString::number(static_cast<int>(val)));
        }
        return;
    }

    if (type == "behavior") {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (spin) model->setData(index, QString::number(spin->value()));
        return;
    }

    if (type == "int") {
        auto* spin = qobject_cast<QSpinBox*>(editor);
        if (spin) model->setData(index, QString::number(spin->value()));
        return;
    }

    auto* spin = qobject_cast<QDoubleSpinBox*>(editor);
    if (spin) {
        double v = spin->value();
        if (v == static_cast<int>(v))
            model->setData(index, QString::number(static_cast<int>(v)));
        else
            model->setData(index, QString::number(v, 'f', 4));
    }
}

} // namespace skill_editor
