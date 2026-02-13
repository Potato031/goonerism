#include "../Includes/timelinewidget.h"
#include <QPainter>
#include <QStyle>
#include <QFile>
#include <QVideoFrame>

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

void TimelineWidget::splitAtPlayhead() {
    for (int i = 0; i < segments.size(); ++i) {
        if (currentPosMs > segments[i].startMs + 200 && currentPosMs < segments[i].endMs - 200) {
            saveState();
            qint64 originalEnd = segments[i].endMs;
            segments[i].endMs = currentPosMs;
            segments.insert(i + 1, { currentPosMs, originalEnd });
            selectedSegmentIdx = i + 1;
            showNotification("CLIP SPLIT ✂️");
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

    // Draw Clips
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

        // Waveforms
        if (!audioSamples.empty()) {
            QColor currentWaveColor = isSel ? accent : accent.darker(180);
            painter.setPen(QPen(currentWaveColor, 1));

            int startIdx = (segments[i].startMs * audioSamples.size()) / durationMs;
            int endIdx = (segments[i].endMs * audioSamples.size()) / durationMs;

            for (int s = startIdx; s < endIdx && s < (int)audioSamples.size(); s++) {
                int x = (s * (double)contentWidth) / audioSamples.size();
                float norm = (audioSamples[s] / maxAmplitude) * audioGain;
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
        double estBytes = (double)originalFileSize * (totalSec / originalSec) * ((cropRight - cropLeft) * (cropBottom - cropTop));

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

        int sizeBadgeW = painter.fontMetrics().horizontalAdvance(sizeStr) + 30;
        QRect sizeRect(width() - sizeBadgeW - 15, 15, sizeBadgeW, 28);
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