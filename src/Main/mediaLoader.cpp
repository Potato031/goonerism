#include <qfile.h>
#include <QVideoFrame>
#include <QTimer>
#include "../Includes/timelinewidget.h"

void TimelineWidget::resetMediaState() {
    thumbnailCache.clear();
    audioSamples.clear();
    undoStack.clear();
    redoStack.clear();

    segments.clear();
    Segment initialSegment;
    initialSegment.startMs = 0;
    initialSegment.endMs = 100;
    initialSegment.cropTop = cropTop;
    initialSegment.cropBottom = cropBottom;
    initialSegment.cropLeft = cropLeft;
    initialSegment.cropRight = cropRight;
    segments.append(initialSegment);

    // A fresh load fully resets the composition: extra sources and overlay
    // clips belong to the previous timeline, not the new file.
    sources.clear();
    sourceFilmstrips.clear();
    overlays.clear();
    selectedOverlayIdx = -1;
    overlayDrag = OvNone;
    overlayDragIdx = -1;

    maxAmplitude = 0.01f;
    durationMs = 0;
    zoomFactor = 1.0;
    scrollOffset = 0;
    selectedSegmentIdx = -1;
    selectedSegmentIndices.clear();
    currentPosMs = 0;
    currentAudioTrack = 0;
    totalAudioTracks = 1;
    hasVideoStream = false;
    hasAudioStream = false;
    isExporting = false;
    thumbnailRequestActive = false;
    thumbnailRequestQueue.clear();
    emit overlaysChanged();
}

void TimelineWidget::setMediaSource(const QUrl &url) {
    currentFileUrl = url;
    resetMediaState();

    SourceClip primary;
    primary.path = url.toLocalFile();
    primary.offsetMs = 0;
    sources.append(primary);

    const QFile file(url.toLocalFile());
    originalFileSize = file.size();
    if (!sources.isEmpty()) sources[0].fileSizeBytes = originalFileSize;

    thumbPlayer->setSource(url);
    // detectAudioTracks is now async and will trigger loadAudioFast when done
    detectAudioTracks(url.toLocalFile());

    this->relayout();
    this->update();
}

void TimelineWidget::requestTimelineThumbnails() {
    if (durationMs <= 0 || currentFileUrl.isEmpty()) return;

    thumbnailRequestQueue.clear();
    const int durationSec = qMax(1, static_cast<int>(durationMs / 1000));
    const int stepSec = qMax(1, durationSec / 18);
    for (int sec = 0; sec <= durationSec; sec += stepSec) {
        if (!thumbnailCache.contains(sec)) thumbnailRequestQueue.enqueue(sec);
    }
    if (!thumbnailCache.contains(durationSec)) thumbnailRequestQueue.enqueue(durationSec);
    requestNextTimelineThumbnail();
}

void TimelineWidget::requestNextTimelineThumbnail() {
    if (thumbnailRequestActive || thumbnailRequestQueue.isEmpty()) return;
    thumbnailRequestActive = true;
    const int sec = thumbnailRequestQueue.dequeue();
    thumbPlayer->setPosition(sec * 1000);
    QTimer::singleShot(120, this, [this]() {
        thumbnailRequestActive = false;
        requestNextTimelineThumbnail();
    });
}

void TimelineWidget::setDuration(qint64 duration) {
    if (duration <= 0) return;

    this->durationMs = duration;
    this->zoomFactor = 1.0;
    this->scrollOffset = 0;
    if (!sources.isEmpty()) {
        sources[0].durationMs = duration;
        sources[0].hasVideo = hasVideoStream;
        sources[0].hasAudio = hasAudioStream;
    }

    // Stretch the placeholder to the real duration
    if (segments.isEmpty()) {
        segments.append({0, durationMs});

    } else {
        // Update the first segment to match the real duration
        segments[0].startMs = 0;
        segments[0].endMs = durationMs;
    }

    // Force a layout recalculation and a repaint
    this->relayout();
    this->update();
    QTimer::singleShot(100, this, &TimelineWidget::requestTimelineThumbnails);
}

void TimelineWidget::processVideoFrame(const QVideoFrame &f) {
    if (f.isValid()) {
        int sec = thumbPlayer->position() / 1000;
        if (!thumbnailCache.contains(sec)) {
            thumbnailCache[sec] = f.toImage().scaled(120, trackHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            update();
        }
    }
}
