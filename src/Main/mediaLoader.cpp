#include <qfile.h>
#include <QVideoFrame>
#include "../Includes/timelinewidget.h"

void TimelineWidget::setMediaSource(const QUrl &url) {
    currentFileUrl = url;

    thumbnailCache.clear();
    audioSamples.clear();
    undoStack.clear();
    redoStack.clear();

    segments.clear();
    segments.append({0, 100});

    maxAmplitude = 0.01f;
    zoomFactor = 1.0;
    scrollOffset = 0;
    selectedSegmentIdx = -1;
    currentPosMs = 0;
    isExporting = false;

    const QFile file(url.toLocalFile());
    originalFileSize = file.size();

    thumbPlayer->setSource(url);
    detectAudioTracks(url.toLocalFile());
    loadAudioFast(url.toLocalFile());

    this->updateGeometry();
    this->update();
}

void TimelineWidget::setDuration(qint64 duration) {
    if (duration <= 0) return;

    this->durationMs = duration;
    this->zoomFactor = 1.0;
    this->scrollOffset = 0;

    // Stretch the placeholder to the real duration
    if (segments.isEmpty()) {
        segments.append({0, durationMs});

    } else {
        // Update the first segment to match the real duration
        segments[0].startMs = 0;
        segments[0].endMs = durationMs;
    }

    // Force a layout recalculation and a repaint
    this->updateGeometry();
    this->update();
}

void TimelineWidget::processVideoFrame(const QVideoFrame &f) {
    if (f.isValid()) {
        int sec = thumbPlayer->position() / 1000;
        if (!thumbnailCache.contains(sec)) {
            thumbnailCache[sec] = f.toImage().scaled(120, trackHeight, Qt::KeepAspectRatioByExpanding);
            update();
        }
    }
}