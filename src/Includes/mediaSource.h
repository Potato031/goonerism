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
#include <QString>
#include <QtConcurrent>
#include <QMutex>

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
    QString emptyStateTitle = "Import media to start editing";
    QString emptyStateBody = "Drag a file into the window or use Import Media.";

    float cropT = 0.03f, cropB = 0.96f, cropL = 0.0f, cropR = 1.0f;
    bool adjustingFilter = false;

    enum Edge { None, Center, TopLeft, TopRight, BottomLeft, BottomRight };
    Edge activeEdge = None;

    // PERFORMANCE: Atomic flag and mutex for off-thread frame processing
    QAtomicInt m_isProcessing{0};
    QMutex m_frameMutex;
    QVideoFrame m_lastRawFrame; 

    explicit VideoWithCropWidget(QWidget* parent = nullptr) : QWidget(parent) {
        sink = new QVideoSink(this);
        setMouseTracking(true);
        // CRITICAL: This allows the widget to catch Delete/Backspace key presses
        setFocusPolicy(Qt::StrongFocus);
        this->setAttribute(Qt::WA_StyledBackground, true);

        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame){
            if (!frame.isValid()) return;
            
            {
                QMutexLocker locker(&m_frameMutex);
                m_lastRawFrame = frame;
            }

            triggerScale();
        });
    }

    void triggerScale() {
        if (!m_lastRawFrame.isValid()) return;
        if (!m_isProcessing.testAndSetRelaxed(0, 1)) return;

        QRect tr = calculateTargetRect();
        QSize targetSize = tr.size();
        if (targetSize.isEmpty()) {
            m_isProcessing = 0;
            return;
        }

        (void)QtConcurrent::run(QThreadPool::globalInstance(), [this, targetSize]() {
            QVideoFrame localFrame;
            {
                QMutexLocker locker(&m_frameMutex);
                localFrame = m_lastRawFrame;
            }

            QImage sourceImage = localFrame.toImage();
            if (sourceImage.isNull()) {
                m_isProcessing = 0;
                return;
            }

            const int sourceWidth = sourceImage.width();
            const int sourceHeight = sourceImage.height();
            QImage img = sourceImage.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            {
                QMutexLocker locker(&m_frameMutex);
                lastFrame = img;
            }
            
            m_isProcessing = 0;
            QMetaObject::invokeMethod(this, [this, sourceWidth, sourceHeight]() {
                setProperty("actualWidth", sourceWidth);
                setProperty("actualHeight", sourceHeight);
                update();
            }, Qt::QueuedConnection);
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

    void setPlaceholderState(const QString &title, const QString &body) {
        emptyStateTitle = title;
        emptyStateBody = body;
        update();
    }

    QRect calculateTargetRect() {
        QRect bounds = rect().adjusted(10, 10, -10, -10);
        if (lastFrame.isNull()) return bounds;

        const float imgRatio = static_cast<float>(lastFrame.width()) / qMax(1, lastFrame.height());
        const float boundsRatio = static_cast<float>(bounds.width()) / qMax(1, bounds.height());

        if (imgRatio > boundsRatio) {
            const int fittedHeight = qRound(bounds.width() / imgRatio);
            return QRect(bounds.x(),
                         bounds.y() + (bounds.height() - fittedHeight) / 2,
                         bounds.width(),
                         fittedHeight);
        }

        const int fittedWidth = qRound(bounds.height() * imgRatio);
        return QRect(bounds.x() + (bounds.width() - fittedWidth) / 2,
                     bounds.y(),
                     fittedWidth,
                     bounds.height());
    }

    QRect displayedFrameRect(const QRect &targetRect, const QImage &frame) const {
        if (frame.isNull()) return targetRect;
        const int xOffset = (targetRect.width() - frame.width()) / 2;
        const int yOffset = (targetRect.height() - frame.height()) / 2;
        return QRect(targetRect.topLeft() + QPoint(xOffset, yOffset), frame.size());
    }

    // Static buffer to avoid allocation jitter at 60fps
    QImage previewBuffer;

signals:
    void cropsChanged(float t, float b, float l, float r);
    void filtersChanged(QList<VideoWithCropWidget::FilterObject> filters);

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        triggerScale(); // Re-scale immediately on resize (fullscreen/window resize)
    }

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
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.fillRect(rect(), m_backgroundColor);

        QImage frameToDraw;
        {
            QMutexLocker locker(&m_frameMutex);
            frameToDraw = lastFrame;
        }

        if (frameToDraw.isNull()) {
            // ... (title drawing logic remains same)
            QFont titleFont = p.font();
            titleFont.setPointSize(16);
            titleFont.setBold(true);
            p.setFont(titleFont);
            p.setPen(m_accentColor.lighter(130));
            p.drawText(rect().adjusted(24, 0, -24, -18), Qt::AlignCenter, emptyStateTitle);

            QFont bodyFont = p.font();
            bodyFont.setPointSize(10);
            bodyFont.setBold(false);
            p.setFont(bodyFont);
            p.setPen(m_accentColor.darker(120));
            p.drawText(rect().adjusted(44, 48, -44, 26), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, emptyStateBody);
            return;
        }

        QRect tr = calculateTargetRect();
        const QRect imageRect = displayedFrameRect(tr, frameToDraw);
        
        if (!filterObjects.isEmpty()) {
            QPainter ip(&frameToDraw);
            for(const auto& obj : filterObjects) {
                int x = obj.l * frameToDraw.width();
                int y = obj.t * frameToDraw.height();
                int w = (obj.r - obj.l) * frameToDraw.width();
                int h = (obj.b - obj.t) * frameToDraw.height();
                QRect area(x, y, w, h);
                if (w <= 0 || h <= 0) continue;

                if (obj.mode == 0 || obj.mode == 1) {
                    QImage sub = frameToDraw.copy(area);
                    int scale = (obj.mode == 0) ? 10 : 30;
                    QImage small = sub.scaled(qMax(1, w/scale), qMax(1, h/scale), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                    QImage blurred = small.scaled(w, h, Qt::IgnoreAspectRatio, (obj.mode == 0 ? Qt::SmoothTransformation : Qt::FastTransformation));
                    ip.drawImage(x, y, blurred);
                } else {
                    ip.fillRect(area, Qt::black);
                }
            }
            ip.end();
        }

        p.drawImage(imageRect.topLeft(), frameToDraw);

        const int cropX = imageRect.x() + qRound(cropL * imageRect.width());
        const int cropY = imageRect.y() + qRound(cropT * imageRect.height());
        const int cropW = qRound((cropR - cropL) * imageRect.width());
        const int cropH = qRound((cropB - cropT) * imageRect.height());
        const QRect cropRect(cropX, cropY, cropW, cropH);

        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 105));
        p.drawRect(QRect(imageRect.left(), imageRect.top(), imageRect.width(), qMax(0, cropRect.top() - imageRect.top())));
        p.drawRect(QRect(imageRect.left(), cropRect.bottom(), imageRect.width(), qMax(0, imageRect.bottom() - cropRect.bottom())));
        p.drawRect(QRect(imageRect.left(), cropRect.top(), qMax(0, cropRect.left() - imageRect.left()), cropRect.height()));
        p.drawRect(QRect(cropRect.right(), cropRect.top(), qMax(0, imageRect.right() - cropRect.right()), cropRect.height()));
        p.restore();

        for (int i = 0; i < filterObjects.size(); ++i) {
            drawSelectionUI(p, imageRect, filterObjects[i].l, filterObjects[i].t, filterObjects[i].r, filterObjects[i].b, m_secondaryColor, (selectedFilterIdx == i && adjustingFilter));
        }

        drawSelectionUI(p, imageRect, cropL, cropT, cropR, cropB, m_accentColor, !adjustingFilter);
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
        QImage frame;
        {
            QMutexLocker locker(&m_frameMutex);
            frame = lastFrame;
        }
        QRect tr = displayedFrameRect(calculateTargetRect(), frame);
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
        QImage frame;
        {
            QMutexLocker locker(&m_frameMutex);
            frame = lastFrame;
        }
        QRect tr = displayedFrameRect(calculateTargetRect(), frame);
        float curX = qBound(0.0f, static_cast<float>(e->pos().x() - tr.x()) / qMax(1, tr.width()), 1.0f);
        float curY = qBound(0.0f, static_cast<float>(e->pos().y() - tr.y()) / qMax(1, tr.height()), 1.0f);

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
