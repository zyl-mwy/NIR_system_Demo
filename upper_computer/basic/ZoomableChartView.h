#pragma once

#include <QtCharts/QChartView>
#include <QMouseEvent>
#include <QWheelEvent>
#include <functional>

class ZoomableChartView : public QtCharts::QChartView {
public:
    explicit ZoomableChartView(QWidget* parent = nullptr)
        : QtCharts::QChartView(parent), isPanning(false) {
        setRubberBand(QtCharts::QChartView::RectangleRubberBand);
        setDragMode(QGraphicsView::NoDrag);
        setMouseTracking(true);
    }

    void setOnDoubleClickCallback(std::function<void(ZoomableChartView*)> cb) { onDoubleClick = std::move(cb); }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            isPanning = true;
            panStart = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
        QtCharts::QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (isPanning && chart()) {
            QPointF delta = event->pos() - panStart;
            chart()->scroll(-delta.x(), delta.y());
            panStart = event->pos();
        }
        QtCharts::QChartView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            isPanning = false;
            setCursor(Qt::ArrowCursor);
        }
        QtCharts::QChartView::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
        if (!chart()) return;
        const double factor = (event->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
        chart()->zoom(factor);
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (onDoubleClick) onDoubleClick(this);
        QtCharts::QChartView::mouseDoubleClickEvent(event);
    }

private:
    bool isPanning;
    QPoint panStart;
    std::function<void(ZoomableChartView*)> onDoubleClick;
};


