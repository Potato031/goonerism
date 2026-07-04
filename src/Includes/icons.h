//
// Painted vector icons — crisp at any DPI, no image assets required.
//

#ifndef SIMPLEVIDEOEDITOR_ICONS_H
#define SIMPLEVIDEOEDITOR_ICONS_H

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QtMath>
#include <functional>

namespace Icons {

using GlyphPainter = std::function<void(QPainter &p, const QRectF &r)>;

// Renders a glyph on a normalized 100x100 canvas at several pixel sizes.
inline QIcon makeIcon(const GlyphPainter &glyph, const QColor &color, qreal strokeWidth = 9.0) {
    QIcon icon;
    for (int px : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = px / 100.0;
        p.scale(s, s);
        QPen pen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        glyph(p, QRectF(0, 0, 100, 100));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

inline void fillGlyph(QPainter &p, const QPainterPath &path) {
    QColor c = p.pen().color();
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path);
    p.restore();
}

inline QIcon play(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath t;
        t.moveTo(32, 20); t.lineTo(82, 50); t.lineTo(32, 80); t.closeSubpath();
        fillGlyph(p, t);
    }, c);
}

inline QIcon pause(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath b;
        b.addRoundedRect(28, 22, 15, 56, 5, 5);
        b.addRoundedRect(57, 22, 15, 56, 5, 5);
        fillGlyph(p, b);
    }, c);
}

// |< frame step back
inline QIcon stepBack(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath g;
        g.addRoundedRect(24, 26, 11, 48, 4, 4);
        g.moveTo(76, 26); g.lineTo(42, 50); g.lineTo(76, 74); g.closeSubpath();
        fillGlyph(p, g);
    }, c);
}

// >| frame step forward
inline QIcon stepForward(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath g;
        g.addRoundedRect(65, 26, 11, 48, 4, 4);
        g.moveTo(24, 26); g.lineTo(58, 50); g.lineTo(24, 74); g.closeSubpath();
        fillGlyph(p, g);
    }, c);
}

// << jump back
inline QIcon jumpBack(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath g;
        g.moveTo(50, 28); g.lineTo(20, 50); g.lineTo(50, 72); g.closeSubpath();
        g.moveTo(82, 28); g.lineTo(52, 50); g.lineTo(82, 72); g.closeSubpath();
        fillGlyph(p, g);
    }, c);
}

// >> jump forward
inline QIcon jumpForward(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath g;
        g.moveTo(18, 28); g.lineTo(48, 50); g.lineTo(18, 72); g.closeSubpath();
        g.moveTo(50, 28); g.lineTo(80, 50); g.lineTo(50, 72); g.closeSubpath();
        fillGlyph(p, g);
    }, c);
}

inline QIcon volume(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath s;
        s.moveTo(18, 40); s.lineTo(32, 40); s.lineTo(50, 24); s.lineTo(50, 76);
        s.lineTo(32, 60); s.lineTo(18, 60); s.closeSubpath();
        fillGlyph(p, s);
        p.drawArc(QRectF(48, 32, 28, 36), -60 * 16, 120 * 16);
        p.drawArc(QRectF(42, 20, 48, 60), -55 * 16, 110 * 16);
    }, c);
}

inline QIcon volumeMuted(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath s;
        s.moveTo(14, 40); s.lineTo(28, 40); s.lineTo(46, 24); s.lineTo(46, 76);
        s.lineTo(28, 60); s.lineTo(14, 60); s.closeSubpath();
        fillGlyph(p, s);
        p.drawLine(QPointF(58, 38), QPointF(84, 62));
        p.drawLine(QPointF(84, 38), QPointF(58, 62));
    }, c);
}

inline QIcon fullscreen(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawPolyline(QPolygonF({{22, 40}, {22, 22}, {40, 22}}));
        p.drawPolyline(QPolygonF({{60, 22}, {78, 22}, {78, 40}}));
        p.drawPolyline(QPolygonF({{78, 60}, {78, 78}, {60, 78}}));
        p.drawPolyline(QPolygonF({{40, 78}, {22, 78}, {22, 60}}));
    }, c);
}

inline QIcon exitFullscreen(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawPolyline(QPolygonF({{40, 22}, {40, 40}, {22, 40}}));
        p.drawPolyline(QPolygonF({{60, 22}, {60, 40}, {78, 40}}));
        p.drawPolyline(QPolygonF({{78, 60}, {60, 60}, {60, 78}}));
        p.drawPolyline(QPolygonF({{40, 78}, {40, 60}, {22, 60}}));
    }, c);
}

inline QIcon winMinimize(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(26, 58), QPointF(74, 58));
    }, c);
}

inline QIcon winMaximize(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawRoundedRect(QRectF(27, 27, 46, 46), 6, 6);
    }, c, 7.0);
}

inline QIcon winRestore(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawRoundedRect(QRectF(35, 22, 38, 38), 5, 5);
        p.drawRoundedRect(QRectF(22, 35, 38, 38), 5, 5);
    }, c, 7.0);
}

inline QIcon winClose(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(28, 28), QPointF(72, 72));
        p.drawLine(QPointF(72, 28), QPointF(28, 72));
    }, c);
}

// Small abstract app mark for the titlebar — a rounded blob, not a photo icon.
inline QIcon appMark(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath blob;
        blob.addRoundedRect(20, 20, 60, 60, 20, 20);
        fillGlyph(p, blob);
    }, c);
}

inline QIcon chevronDown(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawPolyline(QPolygonF({{25, 35}, {50, 65}, {75, 35}}));
    }, c);
}

// camera / snapshot
inline QIcon snapshot(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath body;
        body.addRoundedRect(16, 32, 68, 44, 8, 8);
        p.drawPath(body);
        p.drawPolyline(QPolygonF({{36, 32}, {42, 22}, {58, 22}, {64, 32}}));
        p.drawEllipse(QPointF(50, 54), 13, 13);
    }, c, 8.0);
}

// scissors / split
inline QIcon split(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(50, 16), QPointF(50, 50));
        QPen dash = p.pen();
        dash.setStyle(Qt::CustomDashLine);
        dash.setDashPattern({0.1, 2.2});
        p.save();
        p.setPen(dash);
        p.drawLine(QPointF(50, 54), QPointF(50, 66));
        p.restore();
        p.drawEllipse(QPointF(32, 76), 10, 10);
        p.drawEllipse(QPointF(68, 76), 10, 10);
        p.drawLine(QPointF(38, 68), QPointF(58, 38));
        p.drawLine(QPointF(62, 68), QPointF(42, 38));
    }, c, 8.0);
}

inline QIcon trash(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(20, 30), QPointF(80, 30));
        p.drawPolyline(QPolygonF({{38, 30}, {38, 20}, {62, 20}, {62, 30}}));
        QPainterPath bin;
        bin.moveTo(28, 30); bin.lineTo(32, 82); bin.lineTo(68, 82); bin.lineTo(72, 30);
        p.drawPath(bin);
        p.drawLine(QPointF(42, 42), QPointF(43, 70));
        p.drawLine(QPointF(58, 42), QPointF(57, 70));
    }, c, 8.0);
}

inline QIcon undo(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawArc(QRectF(24, 26, 52, 52), 30 * 16, -270 * 16);
        QPainterPath a;
        a.moveTo(14, 32); a.lineTo(38, 22); a.lineTo(36, 46); a.closeSubpath();
        fillGlyph(p, a);
    }, c);
}

inline QIcon redo(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawArc(QRectF(24, 26, 52, 52), 150 * 16, 270 * 16);
        QPainterPath a;
        a.moveTo(86, 32); a.lineTo(62, 22); a.lineTo(64, 46); a.closeSubpath();
        fillGlyph(p, a);
    }, c);
}

// + import
inline QIcon importMedia(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(50, 24), QPointF(50, 76));
        p.drawLine(QPointF(24, 50), QPointF(76, 50));
    }, c, 11.0);
}

// up-arrow out of tray
inline QIcon exportMedia(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawLine(QPointF(50, 18), QPointF(50, 58));
        QPainterPath a;
        a.moveTo(32, 34); a.lineTo(50, 14); a.lineTo(68, 34);
        p.drawPath(a);
        p.drawPolyline(QPolygonF({{20, 58}, {20, 82}, {80, 82}, {80, 58}}));
    }, c);
}

inline QIcon gear(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawEllipse(QPointF(50, 50), 13, 13);
        for (int i = 0; i < 8; ++i) {
            const qreal angle = qDegreesToRadians(i * 45.0);
            const QPointF dir(qCos(angle), qSin(angle));
            p.drawLine(QPointF(50, 50) + dir * 24, QPointF(50, 50) + dir * 34);
        }
    }, c, 9.0);
}

inline QIcon help(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        QPainterPath q;
        q.moveTo(34, 38);
        q.cubicTo(34, 18, 68, 18, 68, 38);
        q.cubicTo(68, 52, 50, 50, 50, 64);
        p.drawPath(q);
        QPainterPath dot;
        dot.addEllipse(QPointF(50, 82), 6, 6);
        fillGlyph(p, dot);
    }, c);
}

inline QIcon crop(const QColor &c) {
    return makeIcon([](QPainter &p, const QRectF &) {
        p.drawPolyline(QPolygonF({{30, 14}, {30, 70}, {86, 70}}));
        p.drawPolyline(QPolygonF({{14, 30}, {70, 30}, {70, 86}}));
    }, c, 9.0);
}

} // namespace Icons

#endif // SIMPLEVIDEOEDITOR_ICONS_H
