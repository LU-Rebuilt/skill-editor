#include "tree_scene.h"

#include <QBrush>
#include <QPen>
#include <QFont>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

namespace skill_editor {

static QColor type_color(const std::string& type_name) {
    if (type_name == "And" || type_name == "Switch" || type_name == "SwitchMultiple" ||
        type_name == "Chain" || type_name == "Start" || type_name == "End" || type_name == "Verify")
        return QColor(100, 149, 237);
    if (type_name == "BasicAttack" || type_name == "TacArc" || type_name == "ProjectileAttack" ||
        type_name == "AttackDelay" || type_name == "NPC Combat Skill" || type_name == "Grab" ||
        type_name == "Block" || type_name == "Taunt")
        return QColor(220, 80, 80);
    if (type_name == "Heal" || type_name == "RepairArmor")
        return QColor(80, 200, 80);
    if (type_name == "Buff" || type_name == "ApplyBuff" || type_name == "RemoveBuff" ||
        type_name == "DamageBuff" || type_name == "Damage Absorption" || type_name == "DamageReduction" ||
        type_name == "Immunity" || type_name == "Shield" || type_name == "LootBuff" ||
        type_name == "DarkInspiration" || type_name == "VentureVision")
        return QColor(200, 160, 60);
    if (type_name == "Movement Switch" || type_name == "Jetpack" || type_name == "ForceMovement" ||
        type_name == "AirMovement" || type_name == "FallSpeed" || type_name == "Speed" ||
        type_name == "Car Boost" || type_name == "Knockback" || type_name == "Mount" ||
        type_name == "PullToPoint")
        return QColor(60, 180, 200);
    if (type_name == "AoE" || type_name == "PlayEffect" || type_name == "Skill Event" ||
        type_name == "Spawn Object" || type_name == "SpawnQuickbuild" || type_name == "Stun")
        return QColor(180, 100, 220);
    if (type_name == "Over Time" || type_name == "Duration" || type_name == "ChargeUp" ||
        type_name == "Interrupt" || type_name == "AlterCooldown" || type_name == "AlterChainDelay")
        return QColor(160, 160, 160);
    return QColor(180, 180, 180);
}

// ---------------------------------------------------------------------------
// NodeItem
// ---------------------------------------------------------------------------

NodeItem::NodeItem(int behavior_id, const std::string& type_name,
                   QColor base_color, qreal w, qreal h, QGraphicsItem* parent)
    : QGraphicsRectItem(parent),
      behavior_id_(behavior_id), base_color_(base_color)
{
    setFlag(ItemIsMovable);
    setFlag(ItemIsSelectable);
    setFlag(ItemSendsGeometryChanges);
    setZValue(1);

    // Measure text to auto-size
    QFont name_font("sans-serif", 8, QFont::Bold);
    QFont id_font("sans-serif", 7);
    QFontMetricsF name_fm(name_font);
    QFontMetricsF id_fm(id_font);

    QString name = QString::fromStdString(type_name);
    QString id_text = QString::number(behavior_id);

    qreal text_w = std::max(name_fm.horizontalAdvance(name),
                            id_fm.horizontalAdvance(id_text));
    qreal pad = 14; // left accent(4) + margin(8) + right margin(2)
    qreal actual_w = std::max(text_w + pad, 50.0);
    qreal actual_h = name_fm.height() + id_fm.height() + 12; // top(4) + gap(4) + bottom(4)

    setRect(0, 0, actual_w, actual_h);
    setBrush(QBrush(QColor(35, 38, 45)));
    setPen(QPen(base_color, 2));

    // Colored accent bar on left edge
    auto* accent = new QGraphicsRectItem(0, 0, 4, actual_h, this);
    accent->setBrush(QBrush(base_color));
    accent->setPen(Qt::NoPen);

    // Type name
    auto* label = new QGraphicsSimpleTextItem(name, this);
    label->setFont(name_font);
    label->setBrush(QColor(230, 235, 240));
    label->setPos(8, 4);

    // ID
    auto* id_label = new QGraphicsSimpleTextItem(id_text, this);
    id_label->setFont(id_font);
    id_label->setBrush(QColor(140, 150, 165));
    id_label->setPos(8, 4 + name_fm.height() + 4);
}

void NodeItem::set_highlighted(bool on) {
    if (on) {
        setPen(QPen(QColor(255, 60, 60), 3));
    } else {
        setPen(QPen(base_color_, 2));
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        for (auto* edge : edges_)
            edge->update_position();
    }
    return QGraphicsRectItem::itemChange(change, value);
}

// ---------------------------------------------------------------------------
// EdgeItem
// ---------------------------------------------------------------------------

EdgeItem::EdgeItem(NodeItem* from, NodeItem* to, const std::string& label,
                   QGraphicsItem* parent)
    : QGraphicsLineItem(parent), from_(from), to_(to)
{
    setPen(QPen(QColor(100, 110, 125), 1.2));
    setZValue(0);

    if (!label.empty()) {
        label_item_ = new QGraphicsSimpleTextItem(
            QString::fromStdString(label), this);
        label_item_->setFont(QFont("sans-serif", 7));
        label_item_->setBrush(QColor(150, 160, 175));
    }

    from_->add_edge(this);
    to_->add_edge(this);
    update_position();
}

void EdgeItem::update_position() {
    QPointF p1 = from_->mapToScene(from_->rect().right(), from_->rect().center().y());
    QPointF p2 = to_->mapToScene(0, to_->rect().center().y());
    setLine(QLineF(p1, p2));

    if (label_item_) {
        label_item_->setPos((p1.x() + p2.x()) / 2 - 15,
                            (p1.y() + p2.y()) / 2 - 12);
    }
}

void EdgeItem::set_highlighted(bool on) {
    if (on) {
        setPen(QPen(QColor(255, 60, 60), 2.5));
    } else {
        setPen(QPen(QColor(100, 110, 125), 1.2));
    }
}

// ---------------------------------------------------------------------------
// TreeScene
// ---------------------------------------------------------------------------

TreeScene::TreeScene(QObject* parent) : QGraphicsScene(parent) {
    setBackgroundBrush(QColor(24, 26, 32));
}

void TreeScene::set_tree(BehaviorNode* root, const CdClientData* data) {
    clear();
    node_items_.clear();
    root_ = root;
    data_ = data;
    if (!root) return;

    // Row-based layout: depth determines X, leaves get consecutive Y slots,
    // parents center vertically on their children.
    std::unordered_map<int, int> depths;
    std::unordered_set<int> visited;
    assign_depth(root, 0, depths, visited);

    std::unordered_map<int, NodeLayout> layouts;
    qreal next_y = 0;
    visited.clear();
    assign_positions(root, depths, next_y, layouts, visited);

    // Pass 1: create NodeItems and collect edge labels per (from, to) pair
    std::map<std::pair<int,int>, std::vector<std::string>> edge_labels;
    std::map<std::pair<int,int>, std::vector<std::string>> back_ref_labels;
    collect_edges(root, layouts, node_items_, edge_labels, back_ref_labels);

    // Pass 2: create one EdgeItem per unique pair with merged labels
    for (const auto& [key, labels] : edge_labels) {
        auto fit = node_items_.find(key.first);
        auto tit = node_items_.find(key.second);
        if (fit == node_items_.end() || tit == node_items_.end()) continue;
        std::string merged;
        for (size_t i = 0; i < labels.size(); ++i) {
            if (i > 0) merged += ", ";
            merged += labels[i];
        }
        auto* edge = new EdgeItem(fit->second, tit->second, merged);
        addItem(edge);
    }

    // Pass 3: create back-reference edges (dashed, orange)
    for (const auto& [key, labels] : back_ref_labels) {
        auto fit = node_items_.find(key.first);
        auto tit = node_items_.find(key.second);
        if (fit == node_items_.end() || tit == node_items_.end()) continue;
        std::string merged;
        for (size_t i = 0; i < labels.size(); ++i) {
            if (i > 0) merged += ", ";
            merged += labels[i];
        }
        auto* edge = new EdgeItem(fit->second, tit->second, merged);
        edge->set_back_ref(true);
        edge->setPen(QPen(QColor(255, 165, 0), 1.5, Qt::DashLine));
        addItem(edge);
    }

    setSceneRect(itemsBoundingRect().adjusted(-40, -40, 40, 40));
}

// Pass 1: assign depth (distance from root) to each node.
void TreeScene::assign_depth(BehaviorNode* node, int depth,
                              std::unordered_map<int, int>& depths,
                              std::unordered_set<int>& visited) {
    if (!node || visited.count(node->behavior_id)) return;
    visited.insert(node->behavior_id);
    // Keep the shallowest depth if seen from multiple parents
    auto [it, inserted] = depths.try_emplace(node->behavior_id, depth);
    if (!inserted) it->second = std::min(it->second, depth);
    for (auto& child : node->children)
        assign_depth(child.get(), depth + 1, depths, visited);
}

// Pass 2: bottom-up Y assignment. Leaves get the next available Y slot.
// Parents are centered on their children's Y range.
qreal TreeScene::assign_positions(BehaviorNode* node,
                                   const std::unordered_map<int, int>& depths,
                                   qreal& next_y,
                                   std::unordered_map<int, NodeLayout>& layouts,
                                   std::unordered_set<int>& visited) {
    if (!node) return 0;

    // Already placed (dedup) — return its Y
    if (layouts.count(node->behavior_id))
        return layouts[node->behavior_id].y;

    // Prevent infinite recursion on cycles
    if (visited.count(node->behavior_id)) {
        layouts[node->behavior_id] = {0, next_y};
        return next_y;
    }
    visited.insert(node->behavior_id);

    auto dit = depths.find(node->behavior_id);
    qreal x = dit != depths.end() ? dit->second * (NODE_W + H_GAP) : 0;

    // Filter to non-deduplicated children
    std::vector<BehaviorNode*> real_children;
    for (auto& child : node->children) {
        if (!layouts.count(child->behavior_id))
            real_children.push_back(child.get());
    }

    if (real_children.empty()) {
        // Leaf node: assign the next Y slot
        qreal my_y = next_y;
        layouts[node->behavior_id] = {x, my_y};
        next_y += NODE_H + V_GAP;
        return my_y;
    }

    // Recurse children first (bottom-up)
    qreal min_child_y = 1e9, max_child_y = -1e9;
    for (auto* child : real_children) {
        qreal cy = assign_positions(child, depths, next_y, layouts, visited);
        min_child_y = std::min(min_child_y, cy);
        max_child_y = std::max(max_child_y, cy);
    }

    // Center this node on its children
    qreal my_y = (min_child_y + max_child_y) / 2.0;
    layouts[node->behavior_id] = {x, my_y};
    return my_y;
}

void TreeScene::collect_edges(BehaviorNode* node,
                               const std::unordered_map<int, NodeLayout>& layouts,
                               std::unordered_map<int, NodeItem*>& node_items,
                               std::map<std::pair<int,int>, std::vector<std::string>>& edge_labels,
                               std::map<std::pair<int,int>, std::vector<std::string>>& back_ref_labels) {
    if (!node) return;

    auto [it, inserted] = node_items.try_emplace(node->behavior_id, nullptr);
    if (inserted) {
        auto lit = layouts.find(node->behavior_id);
        if (lit == layouts.end()) return;

        auto* item = new NodeItem(node->behavior_id, node->type_name,
                                  type_color(node->type_name), NODE_W, NODE_H);
        item->setPos(lit->second.x, lit->second.y);

        // Build tooltip showing non-child parameters
        if (data_) {
            auto pit = data_->params.find(node->behavior_id);
            if (pit != data_->params.end()) {
                QStringList lines;
                lines << QString("<b>%1</b> (ID: %2)")
                    .arg(QString::fromStdString(node->type_name))
                    .arg(node->behavior_id);
                for (const auto& p : pit->second) {
                    if (is_behavior_ref_param(p.parameter_id)) continue;
                    QString val;
                    if (p.value == static_cast<int>(p.value))
                        val = QString::number(static_cast<int>(p.value));
                    else
                        val = QString::number(p.value, 'f', 3);
                    lines << QString("%1: %2")
                        .arg(QString::fromStdString(p.parameter_id))
                        .arg(val);
                }
                item->setToolTip(lines.join("<br>"));
            }
        }

        addItem(item);
        it->second = item;
    }

    // Forward child edges
    for (auto& child : node->children) {
        collect_edges(child.get(), layouts, node_items, edge_labels, back_ref_labels);

        auto key = std::make_pair(node->behavior_id, child->behavior_id);
        auto& labels = edge_labels[key];
        if (!child->edge_label.empty() &&
            std::find(labels.begin(), labels.end(), child->edge_label) == labels.end()) {
            labels.push_back(child->edge_label);
        }
    }

    // Back-reference edges (End→start, AlterChainDelay→chain, etc.)
    if (data_) {
        auto bit = data_->behaviors.find(node->behavior_id);
        int tmpl_id = bit != data_->behaviors.end() ? bit->second.template_id : 0;
        auto pit = data_->params.find(node->behavior_id);
        if (pit != data_->params.end()) {
            for (const auto& p : pit->second) {
                if (!is_behavior_ref_param(p.parameter_id)) continue;
                if (!is_back_ref_param(p.parameter_id, tmpl_id)) continue;
                int target = static_cast<int>(p.value);
                if (target > 0 && node_items.count(target)) {
                    auto key = std::make_pair(node->behavior_id, target);
                    auto& labels = back_ref_labels[key];
                    if (std::find(labels.begin(), labels.end(), p.parameter_id) == labels.end())
                        labels.push_back(p.parameter_id);
                }
            }
        }
    }
}

void TreeScene::collect_subtree_ids(int behavior_id, std::unordered_set<int>& ids) {
    if (!data_ || ids.count(behavior_id)) return;
    ids.insert(behavior_id);

    // Use the data model's child_behaviors (which already excludes back-refs)
    auto children = data_->child_behaviors(behavior_id);
    for (const auto& [param_name, child_id] : children) {
        collect_subtree_ids(child_id, ids);
    }
}

void TreeScene::highlight_subtree(int behavior_id) {
    // Reset non-back-ref edges and all nodes
    for (auto& [id, item] : node_items_)
        item->set_highlighted(false);
    for (auto* qitem : items()) {
        if (auto* edge = dynamic_cast<EdgeItem*>(qitem)) {
            if (!edge->is_back_ref())
                edge->set_highlighted(false);
            // Back-ref edges keep their dashed orange style always
        }
    }

    if (behavior_id <= 0) return;

    // Collect all IDs in the subtree
    std::unordered_set<int> subtree_ids;
    collect_subtree_ids(behavior_id, subtree_ids);

    // Highlight nodes
    for (int id : subtree_ids) {
        auto it = node_items_.find(id);
        if (it != node_items_.end())
            it->second->set_highlighted(true);
    }

    // Highlight forward edges where both endpoints are in the subtree
    for (auto* qitem : items()) {
        if (auto* edge = dynamic_cast<EdgeItem*>(qitem)) {
            if (edge->is_back_ref()) continue; // leave back-refs as-is
            if (subtree_ids.count(edge->from_node()->behavior_id()) &&
                subtree_ids.count(edge->to_node()->behavior_id())) {
                edge->set_highlighted(true);
            }
        }
    }
}

void TreeScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        auto items_at = this->items(event->scenePos());
        for (auto* item : items_at) {
            if (auto* node = dynamic_cast<NodeItem*>(item)) {
                emit node_context_menu(node->behavior_id(), event->screenPos());
                return;
            }
        }
        // Right-click on empty space
        emit empty_context_menu(event->screenPos());
        return;
    }

    // Check what we clicked BEFORE letting the scene handle it
    bool clicked_node = false;
    auto items_at = this->items(event->scenePos());
    for (auto* item : items_at) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            highlight_subtree(node->behavior_id());
            emit node_clicked(node->behavior_id());
            clicked_node = true;
            break;
        }
    }

    if (!clicked_node) {
        highlight_subtree(0);
    }

    QGraphicsScene::mousePressEvent(event);
}

} // namespace skill_editor
