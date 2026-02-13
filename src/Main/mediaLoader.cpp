#include <qfile.h>
#include <QVideoFrame>
#include "../Includes/timelinewidget.h"

void TimelineWidget::setMediaSource(const QUrl &url) {
    currentFileUrl = url;

    // Clear everything to prevent old video data from bleeding into the new one
    thumbnailCache.clear();
    audioSamples.clear();
    undoStack.clear();
    redoStack.clear();
    segments.clear();

    maxAmplitude = 0.01f;
    zoomFactor = 1.0;
    scrollOffset = 0;
    selectedSegmentIdx = -1;
    currentPosMs = 0;
    isExporting = false;

    const QFile file(url.toLocalFile());
    originalFileSize = file.size();

    // We do NOT add a segment here anymore.
    // We wait for the player to report the actual duration.

    thumbPlayer->setSource(url);
    detectAudioTracks(url.toLocalFile());
    loadAudioFast(url.toLocalFile());

    this->updateGeometry();
    this->update();
}

void TimelineWidget::setDuration(qint64 duration) {
    if (duration <= 0) return;

    this->durationMs = duration;

    // Reset view so the whole video fits the screen width
    this->zoomFactor = 1.0;
    this->scrollOffset = 0;

    // If segments are empty (which they are after setMediaSource),
    // create the initial clip spanning the full duration.
    if (segments.isEmpty()) {
        segments.append({0, durationMs});
    } else if (segments.size() == 1) {
        // If there's only one segment and it's wrong, fix it
        segments[0].startMs = 0;
        segments[0].endMs = durationMs;
    }

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