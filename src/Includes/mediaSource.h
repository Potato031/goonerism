#ifndef SIMPLEVIDEOEDITOR_MEDIASOURCE_H
#define SIMPLEVIDEOEDITOR_MEDIASOURCE_H

#include <qimage.h>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <qtmetamacros.h>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>
#include <QList>

class VideoWithCropWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor)
    Q_PROPERTY(QColor secondaryColor MEMBER m_secondaryColor)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)

public:
    struct FilterObject {
        float l, t, r, b;
        int mode; // 0: Blur, 1: Pixelate, 2: SolidColor
    };

    QVideoSink* sink;
    QImage lastFrame;
    QPointF lastMousePos;
    QList<FilterObject> filterObjects;
    int selectedFilterIdx = -1;

    QColor m_accentColor = QColor("#50C878");
    QColor m_secondaryColor = QColor("#00FA9A");
    QColor m_backgroundColor = QColor("#040605");

    float cropT = 0.03f, cropB = 0.96f, cropL = 0.0f, cropR = 1.0f;
    bool adjustingFilter = false;

    enum Edge { None, Center, TopLeft, TopRight, BottomLeft, BottomRight };
    Edge activeEdge = None;

    explicit VideoWithCropWidget(QWidget* parent = nullptr) : QWidget(parent) {
        sink = new QVideoSink(this);
        setMouseTracking(true);
        // CRITICAL: This allows the widget to catch Delete/Backspace key presses
        setFocusPolicy(Qt::StrongFocus);
        this->setAttribute(Qt::WA_StyledBackground, true);

        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame){
            lastFrame = frame.toImage();
            update();
        });
    }

    void addFilter(int mode) {
        FilterObject obj = {0.4f, 0.4f, 0.6f, 0.6f, mode};
        filterObjects.append(obj);
        selectedFilterIdx = filterObjects.size() - 1;
        adjustingFilter = true;
        update();
        emit filtersChanged(filterObjects);
    }

    QRect calculateTargetRect() {
        if (lastFrame.isNull()) return rect();
        float imgRatio = static_cast<float>(lastFrame.width()) / lastFrame.height();
        float widgetRatio = static_cast<float>(width()) / height();
        if (imgRatio > widgetRatio) {
            int h = width() / imgRatio;
            return QRect(0, (height() - h) / 2, width(), h);
        } else {
            int w = height() * imgRatio;
            return QRect((width() - w) / 2, 0, w, height());
        }
    }

signals:
    void cropsChanged(float t, float b, float l, float r);
    void filtersChanged(QList<VideoWithCropWidget::FilterObject> filters);

protected:
    // --- DELETE LOGIC ---
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && adjustingFilter) {
            if (selectedFilterIdx >= 0 && selectedFilterIdx < filterObjects.size()) {
                filterObjects.removeAt(selectedFilterIdx);
                selectedFilterIdx = -1;
                adjustingFilter = false;
                update();
                emit filtersChanged(filterObjects);
            }
        }
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), m_backgroundColor);

        if (lastFrame.isNull()) return;

        QRect tr = calculateTargetRect();
        QImage processedFrame = lastFrame.copy();

        QPainter ip(&processedFrame);
        for(const auto& obj : filterObjects) {
            int x = obj.l * processedFrame.width();
            int y = obj.t * processedFrame.height();
            int w = (obj.r - obj.l) * processedFrame.width();
            int h = (obj.b - obj.t) * processedFrame.height();
            QRect area(x, y, w, h);
            if (w <= 0 || h <= 0) continue;

            if (obj.mode == 0 || obj.mode == 1) {
                QImage sub = lastFrame.copy(area);
                int scale = (obj.mode == 0) ? 10 : 30;
                QImage small = sub.scaled(qMax(1, w/scale), qMax(1, h/scale), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                QImage blurred = small.scaled(w, h, Qt::IgnoreAspectRatio, (obj.mode == 0 ? Qt::SmoothTransformation : Qt::FastTransformation));
                ip.drawImage(x, y, blurred);
            } else {
                ip.fillRect(area, Qt::black);
            }
        }
        ip.end();

        p.drawImage(tr, processedFrame);

        for (int i = 0; i < filterObjects.size(); ++i) {
            drawSelectionUI(p, tr, filterObjects[i].l, filterObjects[i].t, filterObjects[i].r, filterObjects[i].b, m_secondaryColor, (selectedFilterIdx == i && adjustingFilter));
        }

        drawSelectionUI(p, tr, cropL, cropT, cropR, cropB, m_accentColor, !adjustingFilter);
    }

    void drawSelectionUI(QPainter &p, QRect tr, float L, float T, float R, float B, QColor color, bool isActive) {
        int x = tr.x() + (L * tr.width());
        int y = tr.y() + (T * tr.height());
        int w = (R - L) * tr.width();
        int h = (B - T) * tr.height();

        p.setOpacity(isActive ? 1.0 : 0.3);
        // If active, use a thicker line (3px) so the user knows it's selected for deletion
        p.setPen(QPen(color, isActive ? 3 : 1, isActive ? Qt::SolidLine : Qt::DashLine));
        p.setBrush(Qt::NoBrush);

        p.drawRect(x, y, w, h);

        if (isActive) {
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(x, y), 6, 6);
            p.drawEllipse(QPoint(x + w, y), 6, 6);
            p.drawEllipse(QPoint(x, y + h), 6, 6);
            p.drawEllipse(QPoint(x + w, y + h), 6, 6);
        }
        p.setOpacity(1.0);
    }

    void mousePressEvent(QMouseEvent* e) override {
        // Ensure the widget gets focus when clicked so keyboard works
        this->setFocus();

        if (lastFrame.isNull()) return;
        QRect tr = calculateTargetRect();
        QPoint p = e->pos();
        int margin = 20;

        for (int i = filterObjects.size() - 1; i >= 0; --i) {
            auto f = filterObjects[i];
            int hL = tr.x() + (f.l * tr.width()), hR = tr.x() + (f.r * tr.width());
            int hT = tr.y() + (f.t * tr.height()), hB = tr.y() + (f.b * tr.height());

            if (QLineF(p, QPoint(hL, hT)).length() < margin) activeEdge = TopLeft;
            else if (QLineF(p, QPoint(hR, hT)).length() < margin) activeEdge = TopRight;
            else if (QLineF(p, QPoint(hL, hB)).length() < margin) activeEdge = BottomLeft;
            else if (QLineF(p, QPoint(hR, hB)).length() < margin) activeEdge = BottomRight;
            else if (p.x() > hL && p.x() < hR && p.y() > hT && p.y() < hB) activeEdge = Center;
            else continue;

            selectedFilterIdx = i;
            adjustingFilter = true;
            lastMousePos = QPointF(static_cast<float>(p.x() - tr.x())/tr.width(), static_cast<float>(p.y() - tr.y())/tr.height());
            update();
            return;
        }

        int cL = tr.x() + (cropL * tr.width()), cR = tr.x() + (cropR * tr.width());
        int cT = tr.y() + (cropT * tr.height()), cB = tr.y() + (cropB * tr.height());
        if (QLineF(p, QPoint(cL, cT)).length() < margin) activeEdge = TopLeft;
        else if (QLineF(p, QPoint(cR, cT)).length() < margin) activeEdge = TopRight;
        else if (QLineF(p, QPoint(cL, cB)).length() < margin) activeEdge = BottomLeft;
        else if (QLineF(p, QPoint(cR, cB)).length() < margin) activeEdge = BottomRight;
        else if (p.x() > cL && p.x() < cR && p.y() > cT && p.y() < cB) activeEdge = Center;
        else {
            activeEdge = None;
            selectedFilterIdx = -1; // Deselect if clicking background
            update();
            return;
        }

        selectedFilterIdx = -1;
        adjustingFilter = false;
        lastMousePos = QPointF(static_cast<float>(p.x() - tr.x())/tr.width(), static_cast<float>(p.y() - tr.y())/tr.height());
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (activeEdge == None || lastFrame.isNull()) return;
        QRect tr = calculateTargetRect();
        float curX = qBound(0.0f, static_cast<float>(e->pos().x() - tr.x()) / tr.width(), 1.0f);
        float curY = qBound(0.0f, static_cast<float>(e->pos().y() - tr.y()) / tr.height(), 1.0f);

        float &L = adjustingFilter ? filterObjects[selectedFilterIdx].l : cropL;
        float &R = adjustingFilter ? filterObjects[selectedFilterIdx].r : cropR;
        float &T = adjustingFilter ? filterObjects[selectedFilterIdx].t : cropT;
        float &B = adjustingFilter ? filterObjects[selectedFilterIdx].b : cropB;

        const float minGap = 0.02f;
        if (activeEdge == TopLeft) { L = qMin(curX, R-minGap); T = qMin(curY, B-minGap); }
        else if (activeEdge == TopRight) { R = qMax(curX, L+minGap); T = qMin(curY, B-minGap); }
        else if (activeEdge == BottomLeft) { L = qMin(curX, R-minGap); B = qMax(curY, T+minGap); }
        else if (activeEdge == BottomRight) { R = qMax(curX, L+minGap); B = qMax(curY, T+minGap); }
        else if (activeEdge == Center) {
            float dx = curX - lastMousePos.x(), dy = curY - lastMousePos.y();
            float w = R - L, h = B - T;
            L = qBound(0.0f, L + dx, 1.0f - w); R = L + w;
            T = qBound(0.0f, T + dy, 1.0f - h); B = T + h;
            lastMousePos = QPointF(curX, curY);
        }
        update();
        if (adjustingFilter) emit filtersChanged(filterObjects);
        else emit cropsChanged(cropT, cropB, cropL, cropR);
    }
    void mouseReleaseEvent(QMouseEvent*) override { activeEdge = None; }
};

#endif