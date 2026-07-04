#ifndef SIMPLEVIDEOEDITOR_OVERLAYSHAPES_H
#define SIMPLEVIDEOEDITOR_OVERLAYSHAPES_H

#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QRectF>
#include <cmath>

// Shape/arrow annotation rendering, shared between the live preview composite
// (VideoWithCropWidget::compositeFilters) and the export-time PNG bake
// (export.cpp) so what you see while editing is exactly what gets exported —
// ffmpeg has no native ellipse/arrow filter to match against otherwise.
namespace OverlayShapes {

enum Kind { Rectangle = 0, Ellipse = 1, Arrow = 2 };

inline void paint(QPainter &p, const QRectF &area, int kind, const QColor &color, int thickness) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, qMax(1, thickness), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (kind == Ellipse) {
        p.drawEllipse(area);
    } else if (kind == Arrow) {
        const QPointF tail(area.left(), area.bottom());
        const QPointF head(area.right(), area.top());
        p.drawLine(tail, head);

        const double angle = std::atan2(head.y() - tail.y(), head.x() - tail.x());
        const double headLen = qMax(10.0, area.width() * 0.18);
        const double spread = 0.39269908169872414; // pi/8 — M_PI isn't defined on MSVC without _USE_MATH_DEFINES
        const QPointF a1 = head - QPointF(std::cos(angle - spread), std::sin(angle - spread)) * headLen;
        const QPointF a2 = head - QPointF(std::cos(angle + spread), std::sin(angle + spread)) * headLen;

        QPainterPath arrowHead;
        arrowHead.moveTo(head);
        arrowHead.lineTo(a1);
        arrowHead.lineTo(a2);
        arrowHead.closeSubpath();
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPath(arrowHead);
    } else {
        p.drawRect(area);
    }
    p.restore();
}

}

#endif // SIMPLEVIDEOEDITOR_OVERLAYSHAPES_H
