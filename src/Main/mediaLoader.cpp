#include <qfile.h>
#include <QVideoFrame>

#include "../Includes/timelinewidget.h"

void TimelineWidget::setMediaSource(const QUrl &url) {
    currentFileUrl = url;

    thumbnailCache.clear();
    audioSamples.clear();
    undoStack.clear();
    redoStack.clear();

    maxAmplitude = 0.01f;
    zoomFactor = 1.0;
    scrollOffset = 0;
    selectedSegmentIdx = -1;
    currentPosMs = 0;
    isExporting = false;
    const QFile file(url.toLocalFile());
    originalFileSize = file.size();
    segments.clear();
    segments.append({0, 1000});

    thumbPlayer->setSource(url);
    detectAudioTracks(url.toLocalFile());
    loadAudioFast(url.toLocalFile());

    this->updateGeometry();
    this->update();
}

void TimelineWidget::setDuration(qint64 duration) {
    this->durationMs = duration;
    this->zoomFactor = 1.0;
    this->scrollOffset = 0;

    if (segments.isEmpty()) {
        segments.append({0, durationMs});
    } else {
        segments[0].endMs = durationMs;
    }

    update();
}

void TimelineWidget::processVideoFrame(const QVideoFrame &f) {
    if (f.isValid()) {
        if (const int sec = thumbPlayer->position() / 1000; !thumbnailCache.contains(sec)) {
            thumbnailCache[sec] = f.toImage().scaled(120, trackHeight, Qt::KeepAspectRatioByExpanding);
            update();
        }
    }
}

