#pragma once
// behavior_model.h — In-memory behavior tree model for the editor.
//
// Wraps CdClientData into a tree structure suitable for Qt model/view.
// Each BehaviorNode represents one behavior in the tree, with children
// determined by parameters that reference other behavior IDs.

#include "db_source.h"

#include <QAbstractItemModel>
#include <memory>
#include <vector>

namespace skill_editor {

struct BehaviorNode {
    int behavior_id = 0;
    std::string type_name;       // e.g. "BasicAttack"
    std::string edge_label;      // e.g. "on_success" (parameter name that linked here)
    int template_id = 0;
    std::vector<BehaviorParameter> params; // all parameters (including child refs)
    std::vector<std::unique_ptr<BehaviorNode>> children;
    BehaviorNode* parent = nullptr;
    int row_in_parent = 0;
};

// Build a tree starting from a root behavior_id.
// Handles cycles by tracking visited IDs.
std::unique_ptr<BehaviorNode> build_tree(const CdClientData& data, int root_behavior_id);

// Qt model wrapping a BehaviorNode tree
class BehaviorTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit BehaviorTreeModel(QObject* parent = nullptr);

    void set_root(std::unique_ptr<BehaviorNode> root);
    BehaviorNode* node_at(const QModelIndex& index) const;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::unique_ptr<BehaviorNode> root_;
};

} // namespace skill_editor
