#include "behavior_model.h"

#include <unordered_set>

namespace skill_editor {

// ---------------------------------------------------------------------------
// Tree builder
// ---------------------------------------------------------------------------

static std::unique_ptr<BehaviorNode> build_node(
    const CdClientData& data, int behavior_id,
    const std::string& edge_label,
    std::unordered_set<int>& visited)
{
    auto node = std::make_unique<BehaviorNode>();
    node->behavior_id = behavior_id;
    node->edge_label = edge_label;

    auto bit = data.behaviors.find(behavior_id);
    if (bit != data.behaviors.end()) {
        node->template_id = bit->second.template_id;
    }
    node->type_name = data.behavior_type_name(behavior_id);

    // Copy all parameters
    auto pit = data.params.find(behavior_id);
    if (pit != data.params.end()) {
        node->params = pit->second;
    }

    // Prevent cycles
    if (visited.count(behavior_id)) return node;
    visited.insert(behavior_id);

    // Build children from parameters that reference other behaviors
    auto children = data.child_behaviors(behavior_id);
    int row = 0;
    for (const auto& [param_name, child_id] : children) {
        auto child = build_node(data, child_id, param_name, visited);
        child->parent = node.get();
        child->row_in_parent = row++;
        node->children.push_back(std::move(child));
    }

    visited.erase(behavior_id); // allow same behavior in parallel branches
    return node;
}

std::unique_ptr<BehaviorNode> build_tree(const CdClientData& data, int root_behavior_id) {
    std::unordered_set<int> visited;
    return build_node(data, root_behavior_id, "root", visited);
}

// ---------------------------------------------------------------------------
// BehaviorTreeModel
// ---------------------------------------------------------------------------

BehaviorTreeModel::BehaviorTreeModel(QObject* parent)
    : QAbstractItemModel(parent) {}

void BehaviorTreeModel::set_root(std::unique_ptr<BehaviorNode> root) {
    beginResetModel();
    root_ = std::move(root);
    endResetModel();
}

BehaviorNode* BehaviorTreeModel::node_at(const QModelIndex& index) const {
    if (!index.isValid()) return root_.get();
    return static_cast<BehaviorNode*>(index.internalPointer());
}

QModelIndex BehaviorTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!root_) return {};
    BehaviorNode* parent_node = parent.isValid()
        ? static_cast<BehaviorNode*>(parent.internalPointer())
        : root_.get();
    if (!parent_node || row < 0 || row >= static_cast<int>(parent_node->children.size()))
        return {};
    return createIndex(row, column, parent_node->children[row].get());
}

QModelIndex BehaviorTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    auto* node = static_cast<BehaviorNode*>(child.internalPointer());
    if (!node || !node->parent || node->parent == root_.get()) return {};
    return createIndex(node->parent->row_in_parent, 0, node->parent);
}

int BehaviorTreeModel::rowCount(const QModelIndex& parent) const {
    if (!root_) return 0;
    BehaviorNode* node = parent.isValid()
        ? static_cast<BehaviorNode*>(parent.internalPointer())
        : root_.get();
    return node ? static_cast<int>(node->children.size()) : 0;
}

int BehaviorTreeModel::columnCount(const QModelIndex&) const {
    return 3; // Edge label, Type name, Behavior ID
}

QVariant BehaviorTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) return {};
    auto* node = static_cast<BehaviorNode*>(index.internalPointer());
    if (!node) return {};

    switch (index.column()) {
    case 0: return QString::fromStdString(node->edge_label);
    case 1: return QString::fromStdString(node->type_name);
    case 2: return node->behavior_id;
    }
    return {};
}

QVariant BehaviorTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case 0: return "Edge";
    case 1: return "Type";
    case 2: return "ID";
    }
    return {};
}

} // namespace skill_editor
