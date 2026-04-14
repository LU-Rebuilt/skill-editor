#pragma once
// tree_scene.h — QGraphicsScene that renders behavior trees as node graphs.
//
// Each behavior is a draggable NodeItem group (rect + labels).
// Edges are EdgeItem lines that update when nodes move.
// Selected node and its subtree are highlighted.

#include "behavior_model.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsSceneMouseEvent>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace skill_editor {

class EdgeItem;

// A draggable behavior node in the graph
class NodeItem : public QGraphicsRectItem {
public:
    NodeItem(int behavior_id, const std::string& type_name,
             QColor base_color, qreal w, qreal h, QGraphicsItem* parent = nullptr);

    int behavior_id() const { return behavior_id_; }
    void add_edge(EdgeItem* edge) { edges_.push_back(edge); }
    const std::vector<EdgeItem*>& edges() const { return edges_; }

    void set_highlighted(bool on);
    QColor base_color() const { return base_color_; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    int behavior_id_;
    QColor base_color_;
    std::vector<EdgeItem*> edges_;
};

// An edge connecting two NodeItems, with an optional label
class EdgeItem : public QGraphicsLineItem {
public:
    EdgeItem(NodeItem* from, NodeItem* to, const std::string& label,
             QGraphicsItem* parent = nullptr);

    void update_position();
    void set_highlighted(bool on);
    NodeItem* from_node() const { return from_; }
    NodeItem* to_node() const { return to_; }
    bool is_back_ref() const { return is_back_ref_; }
    void set_back_ref(bool v) { is_back_ref_ = v; }

private:
    NodeItem* from_;
    NodeItem* to_;
    QGraphicsSimpleTextItem* label_item_ = nullptr;
    bool is_back_ref_ = false;
};

class TreeScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit TreeScene(QObject* parent = nullptr);

    void set_tree(BehaviorNode* root, const CdClientData* data = nullptr);
    void highlight_subtree(int behavior_id);

signals:
    void node_clicked(int behavior_id);
    void node_context_menu(int behavior_id, const QPointF& screen_pos);
    void empty_context_menu(const QPointF& screen_pos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    struct NodeLayout {
        qreal x = 0, y = 0;
    };

    // Row-based layout: all nodes at the same depth share an X column.
    // Pass 1: assign depths and count leaf descendants.
    // Pass 2: assign Y positions bottom-up (leaves get consecutive slots,
    //          parents center on their children).
    void assign_depth(BehaviorNode* node, int depth,
                      std::unordered_map<int, int>& depths,
                      std::unordered_set<int>& visited);
    qreal assign_positions(BehaviorNode* node,
                           const std::unordered_map<int, int>& depths,
                           qreal& next_y,
                           std::unordered_map<int, NodeLayout>& layouts,
                           std::unordered_set<int>& visited);

    void collect_edges(BehaviorNode* node,
                       const std::unordered_map<int, NodeLayout>& layouts,
                       std::unordered_map<int, NodeItem*>& node_items,
                       std::map<std::pair<int,int>, std::vector<std::string>>& edge_labels,
                       std::map<std::pair<int,int>, std::vector<std::string>>& back_ref_labels);
    void collect_subtree_ids(int behavior_id, std::unordered_set<int>& ids);

    static constexpr qreal NODE_W = 90;
    static constexpr qreal NODE_H = 90;
    static constexpr qreal H_GAP = 80;
    static constexpr qreal V_GAP = 12;

    std::unordered_map<int, NodeItem*> node_items_;
    BehaviorNode* root_ = nullptr;
    const CdClientData* data_ = nullptr;
};

} // namespace skill_editor
