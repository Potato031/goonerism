//
// Created by potato on 2/12/26.
//

#ifndef SIMPLEVIDEOEDITOR_MEDIASOURCE_H
#define SIMPLEVIDEOEDITOR_MEDIASOURCE_H
#include <qimage.h>
#include <QMouseEvent>
#include <QPainter>
#include <qtmetamacros.h>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>

class VideoWithCropWidget : public QWidget {
    Q_OBJECT
    // Link to the CSS Emerald/Hunter Theme
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor)
    Q_PROPERTY(QColor secondaryColor MEMBER m_secondaryColor)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)

public:
    QVideoSink* sink;
    QImage lastFrame;
    QPointF lastMousePos;

    QColor m_accentColor = QColor("#50C878");
    QColor m_secondaryColor = QColor("#00FA9A");
    QColor m_backgroundColor = QColor("#040605");

    float cropT = 0.03f, cropB = 0.96f, cropL = 0.0f, cropR = 1.0f;
    float filterT = 0.1f, filterB = 0.3f, filterL = 0.1f, filterR = 0.3f;

    enum FilterMode { Blur, Pixelate, SolidColor };
    FilterMode currentFilter = Blur;
    bool adjustingFilter = false;

    enum Edge { None, Left, Right, Top, Bottom, Center, TopLeft, TopRight, BottomLeft, BottomRight };
    Edge activeEdge = None;

    explicit VideoWithCropWidget(QWidget* parent = nullptr) : QWidget(parent) {
        sink = new QVideoSink(this);
        setMouseTracking(true);

        // Ensure style properties are loaded
        this->setAttribute(Qt::WA_StyledBackground, true);

        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame){
            lastFrame = frame.toImage();
            this->setProperty("actualWidth", lastFrame.width());
            this->setProperty("actualHeight", lastFrame.height());
            update();
        });
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

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Use the CSS Background Color
        p.fillRect(rect(), m_backgroundColor);

        if (lastFrame.isNull()) return;

        QRect tr = calculateTargetRect();
        QImage processedFrame = lastFrame.copy();
        applyPrivacyFilter(processedFrame);

        p.drawImage(tr, processedFrame);

        // Draw selection boxes using the NEW theme colors
        if (property("filterVisible").toBool()) {
            // Filter uses Mint/Secondary
            drawSelectionBox(p, tr, filterL, filterT, filterR, filterB, m_secondaryColor, adjustingFilter);
        }

        // Crop uses Emerald/Accent
        drawSelectionBox(p, tr, cropL, cropT, cropR, cropB, m_accentColor, !adjustingFilter);
    }

    void applyPrivacyFilter(QImage &img) {
        if (!property("filterVisible").toBool()) return;
        int x = filterL * img.width();
        int y = filterT * img.height();
        int w = (filterR - filterL) * img.width();
        int h = (filterB - filterT) * img.height();
        QRect area(x, y, w, h);
        if (w <= 0 || h <= 0) return;

        if (currentFilter == Blur || currentFilter == Pixelate) {
            QImage sub = img.copy(area);
            int scale = (currentFilter == Blur) ? 10 : 30;
            QImage small = sub.scaled(qMax(1, w/scale), qMax(1, h/scale), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            QImage blurred = small.scaled(w, h, Qt::IgnoreAspectRatio, (currentFilter == Blur ? Qt::SmoothTransformation : Qt::FastTransformation));

            QPainter ip(&img);
            ip.drawImage(x, y, blurred);
        } else if (currentFilter == SolidColor) {
            QPainter ip(&img);
            ip.fillRect(area, Qt::black);
        }
    }

    void drawSelectionBox(QPainter &p, QRect tr, float L, float T, float R, float B, QColor color, bool isActive) {
        int x = tr.x() + (L * tr.width());
        int y = tr.y() + (T * tr.height());
        int w = (R - L) * tr.width();
        int h = (B - T) * tr.height();

        if (!isActive) p.setOpacity(0.3); // Fade non-active tool

        p.setPen(QPen(color, isActive ? 2 : 1, isActive ? Qt::SolidLine : Qt::DashLine));
        p.drawRect(x, y, w, h);

        if (isActive) {
            p.setBrush(color);
            p.setPen(Qt::NoPen);
            // Draw Emerald/Mint Handles
            p.drawEllipse(QPoint(x, y), 6, 6);
            p.drawEllipse(QPoint(x + w, y), 6, 6);
            p.drawEllipse(QPoint(x, y + h), 6, 6);
            p.drawEllipse(QPoint(x + w, y + h), 6, 6);
        }
        p.setOpacity(1.0);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (lastFrame.isNull()) return;
        QRect tr = calculateTargetRect();

        float L = adjustingFilter ? filterL : cropL;
        float R = adjustingFilter ? filterR : cropR;
        float T = adjustingFilter ? filterT : cropT;
        float B = adjustingFilter ? filterB : cropB;

        const int hL = tr.x() + (L * tr.width());
        const int hR = tr.x() + (R * tr.width());
        const int hT = tr.y() + (T * tr.height());
        const int hB = tr.y() + (B * tr.height());

        int margin = 20;
        QPoint p = e->pos();

        if (QLineF(p, QPoint(hL, hT)).length() < margin) activeEdge = TopLeft;
        else if (QLineF(p, QPoint(hR, hT)).length() < margin) activeEdge = TopRight;
        else if (QLineF(p, QPoint(hL, hB)).length() < margin) activeEdge = BottomLeft;
        else if (QLineF(p, QPoint(hR, hB)).length() < margin) activeEdge = BottomRight;
        else if (p.x() > hL && p.x() < hR && p.y() > hT && p.y() < hB) {
            activeEdge = Center;
            lastMousePos = QPointF(static_cast<float>(p.x() - tr.x())/tr.width(), static_cast<float>(p.y() - tr.y())/tr.height());
        }
        else activeEdge = None;
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (activeEdge == None || lastFrame.isNull()) return;
        QRect tr = calculateTargetRect();
        float curX = qBound(0.0f, static_cast<float>(e->pos().x() - tr.x()) / tr.width(), 1.0f);
        float curY = qBound(0.0f, static_cast<float>(e->pos().y() - tr.y()) / tr.height(), 1.0f);

        float &L = adjustingFilter ? filterL : cropL;
        float &R = adjustingFilter ? filterR : cropR;
        float &T = adjustingFilter ? filterT : cropT;
        float &B = adjustingFilter ? filterB : cropB;

        const float minGap = 0.02f;
        if (activeEdge == TopLeft) { L = qMin(curX, R-minGap); T = qMin(curY, B-minGap); }
        else if (activeEdge == TopRight) { R = qMax(curX, L+minGap); T = qMin(curY, B-minGap); }
        else if (activeEdge == BottomLeft) { L = qMin(curX, R-minGap); B = qMax(curY, T+minGap); }
        else if (activeEdge == BottomRight) { R = qMax(curX, L+minGap); B = qMax(curY, T+minGap); }
        else if (activeEdge == Center) {
            float dx = curX - lastMousePos.x();
            const float dy = curY - lastMousePos.y();
            float w = R - L; float h = B - T;
            L = qBound(0.0f, L + dx, 1.0f - w); R = L + w;
            T = qBound(0.0f, T + dy, 1.0f - h); B = T + h;
            lastMousePos = QPointF(curX, curY);
        }
        update();
        emit cropsChanged(cropT, cropB, cropL, cropR);
    }
    void mouseReleaseEvent(QMouseEvent*) override { activeEdge = None; }
};



#endif //SIMPLEVIDEOEDITOR_MEDIASOURCE_H