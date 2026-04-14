#pragma once
// zoomable_view.h — QGraphicsView with scroll-wheel zoom and left-click pan.
//
// Left-click on a node selects/drags it.
// Left-click on empty space pans the canvas.
// Scroll wheel zooms.

#include <QGraphicsView>
#include <QScrollBar>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace skill_editor {

class ZoomableView : public QGraphicsView {
    Q_OBJECT
public:
    using QGraphicsView::QGraphicsView;

protected:
    void wheelEvent(QWheelEvent* event) override {
        constexpr double sensitivity = 0.0006;
        double delta = std::clamp(static_cast<double>(event->angleDelta().y()),
                                  -240.0, 240.0);
        double factor = 1.0 + delta * sensitivity;
        scale(factor, factor);
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->key() == Qt::Key_0) {
                resetTransform();
                if (scene())
                    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
                return;
            }
            if (event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus) {
                scale(1.25, 1.25);
                return;
            }
            if (event->key() == Qt::Key_Minus) {
                scale(0.8, 0.8);
                return;
            }
        }
        QGraphicsView::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Check if clicking on an item
            auto* item = itemAt(event->pos());
            if (item) {
                // Let the scene handle node selection/dragging
                QGraphicsView::mousePressEvent(event);
            } else {
                // Empty space — start panning
                panning_ = true;
                pan_start_ = event->pos();
                setCursor(Qt::ClosedHandCursor);
                event->accept();
            }
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (panning_) {
            QPointF old_pos = mapToScene(pan_start_);
            QPointF new_pos = mapToScene(event->pos());
            QPointF delta = new_pos - old_pos;
            pan_start_ = event->pos();
            setTransformationAnchor(NoAnchor);
            translate(delta.x(), delta.y());
            setTransformationAnchor(AnchorUnderMouse);
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (panning_ && event->button() == Qt::LeftButton) {
            panning_ = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool panning_ = false;
    QPoint pan_start_;
};

} // namespace skill_editor
