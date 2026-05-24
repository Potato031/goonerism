#include "../Includes/timelinewidget.h"
#include <QPainter>
#include <QStyle>
#include <QFile>
#include <QVideoFrame>
#include <QTimer>

namespace {
QString formatTimelineDuration(qint64 ms) {
    ms = qMax<qint64>(0, ms);
    if (ms >= 60000) {
        const qint64 totalSeconds = ms / 1000;
        return QString("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QString("%1.%2s").arg(ms / 1000).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
}

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    videoSink = new QVideoSink(this);
    thumbPlayer = new QMediaPlayer(this);
    thumbPlayer->setVideoOutput(videoSink);

    audioGain = 1.0f;
    selectedSegmentIdx = -1;
    zoomFactor = 1.0;
    scrollOffset = 0;
    durationMs = 0;

    this->setAttribute(Qt::WA_StyledBackground, true);
    this->ensurePolished();
    this->style()->unpolish(this);
    this->style()->polish(this);

    connect(videoSink, &QVideoSink::videoFrameChanged, this, &TimelineWidget::processVideoFrame);
}

void TimelineWidget::setCurrentPosition(qint64 ms) {
    const int oldSegment = segmentIndexAtTime(currentPosMs);
    currentPosMs = ms;
    const int newSegment = segmentIndexAtTime(currentPosMs);
    if (newSegment >= 0 && oldSegment != newSegment) {
        selectedSegmentIdx = newSegment;
        selectedSegmentIndices.clear();
        emitVisualStateForCurrentContext();
    }
    update();
}

void TimelineWidget::splitAtPlayhead() {
    const int splitGuard = playbackSettings.splitGuardMs;
    for (int i = 0; i < segments.size(); ++i) {
        if (currentPosMs > segments[i].startMs + splitGuard && currentPosMs < segments[i].endMs - splitGuard) {
            Segment splitSegment = segments[i];
            qint64 originalEnd = splitSegment.endMs;
            segments[i].endMs = currentPosMs;
            splitSegment.startMs = currentPosMs;
            splitSegment.endMs = originalEnd;
            segments.insert(i + 1, splitSegment);
            selectedSegmentIdx = i + 1;
            showNotification("CLIP SPLIT ✂️");
            emit clipTrimmed();
            emitVisualStateForCurrentContext();
            update();
            return;
        }
    }
}

void TimelineWidget::deleteSelectedSegment() {
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) {
        saveState();
        segments.removeAt(selectedSegmentIdx);
        selectedSegmentIdx = -1;
        showNotification("CLIP DELETED 🗑️");
        validatePlayheadPosition();
        emit clipTrimmed();
        update();
    }
}

void TimelineWidget::validatePlayheadPosition() {
    if (segments.isEmpty()) return;

    int currentClipIdx = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (currentPosMs >= segments[i].startMs && currentPosMs < segments[i].endMs) {
            currentClipIdx = i;
            break;
        }
    }

    if (currentClipIdx == -1) {
        qint64 nextStart = -1;
        for (const auto& clip : segments) {
            if (clip.startMs >= currentPosMs) {
                nextStart = clip.startMs;
                break;
            }
        }

        if (nextStart != -1) {
            currentPosMs = nextStart;
        } else {
            currentPosMs = segments.first().startMs;
        }
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
    }
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get Device Pixel Ratio for Windows High DPI Scaling
    double dpr = this->devicePixelRatioF();

    QColor accent = m_accentColor;
    QColor secondary = m_secondaryColor;
    QColor bg = m_backgroundColor;

    // ALWAYS draw background so widget is never invisible
    painter.fillRect(rect(), bg);

    int viewWidth = width() - sidebarWidth;
    int contentWidth = viewWidth * zoomFactor;
    int vTop = rulerHeight + 10;
    int aTop = vTop + trackHeight + 15;

    // Safety check: if no duration, don't draw clips
    if (durationMs <= 0 || segments.isEmpty()) return;

    painter.save();
    painter.setClipRect(sidebarWidth, 0, width() - sidebarWidth, height());
    painter.translate(sidebarWidth - scrollOffset, 0);

    double pxPerMs = static_cast<double>(contentWidth) / durationMs;

    for (int i = 0; i < segments.size(); ++i) {
        QRectF clipRect(segments[i].startMs * pxPerMs, vTop, (segments[i].endMs - segments[i].startMs) * pxPerMs, trackHeight);
        bool isSel = (i == selectedSegmentIdx) || selectedSegmentIndices.contains(i);

        if (isSel) {
            painter.setPen(QPen(accent, 2.5));
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        } else {
            painter.setPen(QPen(accent.darker(160), 1.0));
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 25));
        }

        painter.drawRoundedRect(clipRect, 5, 5);

        if (!thumbnailCache.isEmpty()) {
            painter.save();
            painter.setClipRect(clipRect.adjusted(2, 2, -2, -2));
            painter.setOpacity(isSel ? 0.62 : 0.42);
            const int thumbW = 88;
            for (int x = static_cast<int>(clipRect.left()) + 4; x < clipRect.right(); x += thumbW) {
                const qint64 timeAtX = qBound<qint64>(segments[i].startMs,
                    static_cast<qint64>(x / pxPerMs), segments[i].endMs);
                const int sec = static_cast<int>(timeAtX / 1000);
                if (!thumbnailCache.contains(sec)) continue;
                QRect target(x, static_cast<int>(clipRect.top()) + 3, thumbW - 4, trackHeight - 6);
                painter.drawImage(target, thumbnailCache.value(sec));
            }
            painter.restore();
        }

        if (segments[i].cropTop > 0.001f || segments[i].cropBottom < 0.999f ||
            segments[i].cropLeft > 0.001f || segments[i].cropRight < 0.999f ||
            !segments[i].filters.isEmpty()) {
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(segments[i].filters.isEmpty()
                                 ? QColor(255, 135, 95, 85)
                                 : QColor(255, 107, 74, 115));
            painter.drawRoundedRect(QRectF(clipRect.left() + 6, clipRect.top() + 6, 34, 14), 3, 3);
            QFont fxFont = painter.font();
            fxFont.setPointSizeF(7);
            fxFont.setBold(true);
            painter.setFont(fxFont);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(clipRect.left() + 6, clipRect.top() + 5, 34, 15), Qt::AlignCenter,
                             segments[i].filters.isEmpty() ? "CROP" : "FX");
            painter.restore();
        }

        // Draw Gain Label (Visual Feedback)
        if (segments[i].gain != 1.0f) {
            painter.save();
            painter.setPen(Qt::white);
            QFont labelFont = painter.font(); labelFont.setPointSizeF(7);
            painter.setFont(labelFont);
            painter.drawText(clipRect.adjusted(5, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                             QString("%1x").arg(segments[i].gain, 0, 'f', 1));
            painter.restore();
        }

        // Waveforms using segment-specific gain
        if (!audioSamples.empty()) {
            QColor currentWaveColor = isSel ? accent : accent.darker(180);
            painter.setPen(QPen(currentWaveColor, 1));

            int startIdx = (segments[i].startMs * audioSamples.size()) / durationMs;
            int endIdx = (segments[i].endMs * audioSamples.size()) / durationMs;

            // PERFORMANCE OPTIMIZATION: Only draw one line per pixel to save CPU/GPU cycles
            // especially important on 4K displays where the timeline can be very wide.
            double samplesPerPixel = (double)audioSamples.size() / contentWidth;
            int step = qMax(1, (int)samplesPerPixel);

            for (int s = startIdx; s < endIdx && s < (int)audioSamples.size(); s += step) {
                int x = (s * (double)contentWidth) / audioSamples.size();
                
                // Find max in this pixel range for a better visual representation
                float maxInStep = 0.0f;
                for (int j = 0; j < step && (s + j) < endIdx && (s + j) < (int)audioSamples.size(); ++j) {
                    maxInStep = qMax(maxInStep, audioSamples[s + j]);
                }

                float norm = (maxInStep / maxAmplitude) * segments[i].gain;
                int h = qMin((float)trackHeight, norm * (trackHeight - 10));
                painter.drawLine(x, aTop + (trackHeight/2) - h/2, x, aTop + (trackHeight/2) + h/2);
            }
        }
    }

    // Playhead
    int playheadX = currentPosMs * pxPerMs;
    QColor pulseColor = secondary;
    pulseColor.setAlphaF(pulseAlpha);

    painter.setPen(Qt::NoPen);
    painter.setBrush(pulseColor);
    QPolygon arrow;
    arrow << QPoint(playheadX - 8, 0) << QPoint(playheadX + 8, 0) << QPoint(playheadX, 12);
    painter.drawPolygon(arrow);

    painter.setPen(QPen(pulseColor, 2));
    painter.drawLine(playheadX, 12, playheadX, height());
    painter.restore();

    // Metadata Badges
    if (originalFileSize > 0) {
        double totalSec = getTotalSegmentsDuration();
        double originalSec = durationMs / 1000.0;
        double weightedSpatialRatio = 0.0;
        for (const auto &seg : segments) {
            const double segDuration = qMax<qint64>(1, seg.endMs - seg.startMs);
            weightedSpatialRatio += segDuration * ((seg.cropRight - seg.cropLeft) * (seg.cropBottom - seg.cropTop));
        }
        const double spatialRatio = totalSec > 0.0 ? weightedSpatialRatio / (totalSec * 1000.0) : 1.0;
        double estBytes = (double)originalFileSize * (totalSec / originalSec) * spatialRatio;

        QString sizeStr;
        double displaySize = estBytes;
        QString unit = "B";
        if (estBytes >= 1024) { displaySize /= 1024; unit = "KB"; }
        if (estBytes >= 1024 * 1024) { displaySize /= 1024; unit = "MB"; }
        if (estBytes >= 1024 * 1024 * 1024) { displaySize /= 1024; unit = "GB"; }
        sizeStr = QString("EST: %1 %2").arg(displaySize, 0, 'f', 2).arg(unit);

        painter.setRenderHint(QPainter::TextAntialiasing);
        QFont font = painter.font();
        font.setBold(true); font.setPointSizeF(8.5);
        painter.setFont(font);

        double mb = estBytes / (1024.0 * 1024.0);
        QColor sizeColor = (mb <= 25.0) ? QColor("#00C853") : (mb <= 100.0) ? accent : secondary;

        const QString durationStr = QString("TOTAL: %1").arg(formatTimelineDuration(static_cast<qint64>(totalSec * 1000.0)));
        int durationBadgeW = painter.fontMetrics().horizontalAdvance(durationStr) + 30;
        QRect durationRect(width() - durationBadgeW - 15, 15, durationBadgeW, 28);
        painter.setBrush(QColor(0, 0, 0, 200));
        painter.setPen(QPen(accent, 1.5));
        painter.drawRoundedRect(durationRect, 6, 6);
        painter.setPen(Qt::white);
        painter.drawText(durationRect, Qt::AlignCenter, durationStr);

        int sizeBadgeW = painter.fontMetrics().horizontalAdvance(sizeStr) + 30;
        QRect sizeRect(durationRect.left() - sizeBadgeW - 10, 15, sizeBadgeW, 28);
        painter.setBrush(QColor(0, 0, 0, 200));
        painter.setPen(QPen(sizeColor, 1.5));
        painter.drawRoundedRect(sizeRect, 6, 6);
        painter.setPen(sizeColor);
        painter.drawText(sizeRect, Qt::AlignCenter, sizeStr);

        QString activeName = (currentAudioTrack < (int)trackNames.size()) ? trackNames[currentAudioTrack] : QString("TRACK %1").arg(currentAudioTrack + 1);
        int trackBadgeW = painter.fontMetrics().horizontalAdvance(activeName.toUpper()) + 30;
        QRect trackRect(sizeRect.left() - trackBadgeW - 10, 15, trackBadgeW, 28);
        painter.setPen(QPen(accent, 1.5));
        painter.drawRoundedRect(trackRect, 6, 6);
        painter.setPen(Qt::white);
        painter.drawText(trackRect, Qt::AlignCenter, activeName.toUpper());
    }
}

void TimelineWidget::updateCropValues(float t, float b, float l, float r) {
    cropTop = t;
    cropBottom = b;
    cropLeft = l;
    cropRight = r;
    for (int idx : targetVisualSegments()) {
        if (idx >= 0 && idx < segments.size()) {
            segments[idx].cropTop = cropTop;
            segments[idx].cropBottom = cropBottom;
            segments[idx].cropLeft = cropLeft;
            segments[idx].cropRight = cropRight;
        }
    }
    update();
}

void TimelineWidget::setCurrentFilters(const QList<VideoWithCropWidget::FilterObject> &filters) {
    currentFilters = filters;
    for (int idx : targetVisualSegments()) {
        if (idx >= 0 && idx < segments.size()) {
            segments[idx].filters = currentFilters;
        }
    }
    update();
}

int TimelineWidget::segmentIndexAtTime(qint64 timeMs) const {
    for (int i = 0; i < segments.size(); ++i) {
        if (timeMs >= segments[i].startMs && timeMs <= segments[i].endMs) return i;
    }
    return -1;
}

int TimelineWidget::activeVisualSegmentIndex() const {
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) return selectedSegmentIdx;
    if (!selectedSegmentIndices.isEmpty()) return *selectedSegmentIndices.constBegin();
    return segmentIndexAtTime(currentPosMs);
}

QSet<int> TimelineWidget::targetVisualSegments() const {
    QSet<int> targets = selectedSegmentIndices;
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) targets.insert(selectedSegmentIdx);
    if (targets.isEmpty()) {
        const int activeIdx = segmentIndexAtTime(currentPosMs);
        if (activeIdx >= 0) targets.insert(activeIdx);
    }
    return targets;
}

void TimelineWidget::applyCurrentVisualsToSelection(bool allSegments) {
    saveState();
    QSet<int> targets;
    if (allSegments) {
        for (int i = 0; i < segments.size(); ++i) targets.insert(i);
    } else {
        targets = targetVisualSegments();
    }

    for (int idx : targets) {
        if (idx < 0 || idx >= segments.size()) continue;
        segments[idx].cropTop = cropTop;
        segments[idx].cropBottom = cropBottom;
        segments[idx].cropLeft = cropLeft;
        segments[idx].cropRight = cropRight;
        segments[idx].filters = currentFilters;
    }

    showNotification(allSegments ? "VISUALS APPLIED TO ALL CLIPS" : "VISUALS APPLIED TO CLIP");
    emit clipTrimmed();
    update();
}

void TimelineWidget::clearVisualsForSelection(bool allSegments) {
    saveState();
    QSet<int> targets;
    if (allSegments) {
        for (int i = 0; i < segments.size(); ++i) targets.insert(i);
    } else {
        targets = targetVisualSegments();
    }

    for (int idx : targets) {
        if (idx < 0 || idx >= segments.size()) continue;
        segments[idx].cropTop = 0.0f;
        segments[idx].cropBottom = 1.0f;
        segments[idx].cropLeft = 0.0f;
        segments[idx].cropRight = 1.0f;
        segments[idx].filters.clear();
    }

    emitVisualStateForCurrentContext();
    showNotification(allSegments ? "CLEARED ALL VISUALS" : "CLEARED CLIP VISUALS");
    emit clipTrimmed();
    update();
}

bool TimelineWidget::visualStateForCurrentContext(float &t, float &b, float &l, float &r,
                                                  QList<VideoWithCropWidget::FilterObject> &filters) const {
    const int idx = activeVisualSegmentIndex();
    if (idx < 0 || idx >= segments.size()) return false;
    const auto &seg = segments[idx];
    t = seg.cropTop;
    b = seg.cropBottom;
    l = seg.cropLeft;
    r = seg.cropRight;
    filters = seg.filters;
    return true;
}

void TimelineWidget::emitVisualStateForCurrentContext() {
    float t, b, l, r;
    QList<VideoWithCropWidget::FilterObject> filters;
    if (visualStateForCurrentContext(t, b, l, r, filters)) {
        cropTop = t;
        cropBottom = b;
        cropLeft = l;
        cropRight = r;
        currentFilters = filters;
        emit visualStateChanged(t, b, l, r, filters);
    }
}

void TimelineWidget::forceFitToDuration() {
    if (durationMs > 0) {
        this->zoomFactor = 1.0;
        this->scrollOffset = 0;

        if (!segments.isEmpty()) {
            segments[0].startMs = 0;
            segments[0].endMs = durationMs;
        }

        emit clipTrimmed();
        this->update();
    }
}
