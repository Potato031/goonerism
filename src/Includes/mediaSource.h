#ifndef SIMPLEVIDEOEDITOR_MEDIASOURCE_H
#define SIMPLEVIDEOEDITOR_MEDIASOURCE_H

#include <qimage.h>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <qtmetamacros.h>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>
#include <QList>
#include <QString>
#include <QtConcurrent>
#include <QMutex>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

class VideoWithCropWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor)
    Q_PROPERTY(QColor secondaryColor MEMBER m_secondaryColor)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)

public:
    struct FilterObject {
        float l, t, r, b;
        int mode;      // 0: Blur, 1: Pixelate, 2: SolidColor, 3: Text
        QString text;  // only used by mode 3
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
    // Set when a rescale request arrives while the worker is busy, so the
    // last request is never silently dropped (matters when paused: no new
    // frame would ever re-trigger the composite).
    QAtomicInt m_pendingRescale{0};
    QMutex m_frameMutex;
    QVideoFrame m_lastRawFrame;

    explicit VideoWithCropWidget(QWidget* parent = nullptr) : QWidget(parent) {
        sink = new QVideoSink(this);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setAcceptDrops(true); // effect buttons can be dragged straight onto the video
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

    // PERFORMANCE: baking blur/pixelate/blackout boxes into the frame is expensive
    // (sub-image copy + two scales per box). This must never run on the UI thread's
    // paintEvent, or every repaint during playback (30-60/sec) stalls input handling.
    // It runs here, in the same background worker that already scales the raw frame.
    static void compositeFilters(QImage &target, const QList<FilterObject> &filters) {
        if (filters.isEmpty()) return;
        QPainter ip(&target);
        for (const auto &obj : filters) {
            int x = obj.l * target.width();
            int y = obj.t * target.height();
            int w = (obj.r - obj.l) * target.width();
            int h = (obj.b - obj.t) * target.height();
            QRect area(x, y, w, h);
            if (w <= 0 || h <= 0) continue;

            if (obj.mode == 0 || obj.mode == 1) {
                QImage sub = target.copy(area);
                int scale = (obj.mode == 0) ? 10 : 30;
                QImage small = sub.scaled(qMax(1, w / scale), qMax(1, h / scale), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                QImage blurred = small.scaled(w, h, Qt::IgnoreAspectRatio, (obj.mode == 0 ? Qt::SmoothTransformation : Qt::FastTransformation));
                ip.drawImage(x, y, blurred);
            } else if (obj.mode == 2) {
                ip.fillRect(area, Qt::black);
            } else { // Text overlay: mirrors ffmpeg drawtext (white, dark outline, centered)
                ip.setRenderHint(QPainter::Antialiasing);
                ip.setRenderHint(QPainter::TextAntialiasing);
                QFont font;
                font.setBold(true);
                font.setPixelSize(qMax(8, qRound(h * 0.6)));
                const QString text = obj.text.isEmpty() ? QStringLiteral("Your text") : obj.text;

                QPainterPath path;
                QFontMetrics fm(font);
                const QRect textBounds = fm.boundingRect(area, Qt::AlignCenter | Qt::TextWordWrap, text);
                int lineY = textBounds.top() + fm.ascent();
                for (const QString &line : text.split('\n')) {
                    const int lineW = fm.horizontalAdvance(line);
                    path.addText(area.center().x() - lineW / 2.0, lineY, font, line);
                    lineY += fm.lineSpacing();
                }
                ip.setPen(QPen(QColor(0, 0, 0, 170), qMax(2.0, h * 0.05)));
                ip.setBrush(Qt::white);
                ip.drawPath(path);
                ip.setPen(Qt::NoPen);
                ip.drawPath(path);
            }
        }
        ip.end();
    }

    void triggerScale() {
        if (!m_lastRawFrame.isValid()) return;
        if (!m_isProcessing.testAndSetRelaxed(0, 1)) {
            m_pendingRescale.storeRelaxed(1);
            return;
        }

        QRect tr = calculateTargetRect();
        QSize targetSize = tr.size();
        if (targetSize.isEmpty()) {
            m_isProcessing = 0;
            return;
        }
        const QList<FilterObject> filtersSnapshot = filterObjects;

        (void)QtConcurrent::run(QThreadPool::globalInstance(), [this, targetSize, filtersSnapshot]() {
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
            compositeFilters(img, filtersSnapshot);

            {
                QMutexLocker locker(&m_frameMutex);
                lastFrame = img;
            }

            m_isProcessing = 0;
            QMetaObject::invokeMethod(this, [this, sourceWidth, sourceHeight]() {
                setProperty("actualWidth", sourceWidth);
                setProperty("actualHeight", sourceHeight);
                update();
                if (m_pendingRescale.fetchAndStoreRelaxed(0)) triggerScale();
            }, Qt::QueuedConnection);
        });
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
    void filterSelectionChanged(int index);
    void overlayDropped(int type);

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        triggerScale(); // Re-scale immediately on resize (fullscreen/window resize)
    }

    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasFormat("application/x-potato-overlay")) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent *event) override {
        if (event->mimeData()->hasFormat("application/x-potato-overlay")) {
            emit overlayDropped(event->mimeData()->data("application/x-potato-overlay").toInt());
            event->acceptProposedAction();
        }
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

        // NOTE: frameToDraw already has blur/pixelate/blackout boxes baked in by the
        // background worker (see triggerScale/compositeFilters) so painting stays cheap.
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
            emit filterSelectionChanged(selectedFilterIdx);
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
            emit filterSelectionChanged(-1);
            update();
            return;
        }

        selectedFilterIdx = -1;
        adjustingFilter = false;
        emit filterSelectionChanged(-1);
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
        if (adjustingFilter) {
            triggerScale();
            emit filtersChanged(filterObjects);
        } else {
            emit cropsChanged(cropT, cropB, cropL, cropR);
        }
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (adjustingFilter) triggerScale();
        activeEdge = None;
    }
};

#endif
