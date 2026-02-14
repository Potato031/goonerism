#include "../Includes/timelinewidget.h"

void TimelineWidget::keyPressEvent(QKeyEvent *event) {
    bool ctrl = event->modifiers() & Qt::ControlModifier;
    bool shift = event->modifiers() & Qt::ShiftModifier;
    bool alt = event->modifiers() & Qt::AltModifier;

    if (event->key() == Qt::Key_Q || event->key() == Qt::Key_0) {
        currentPosMs = qMax(0LL, currentPosMs - 2000);
        emit playheadMoved(currentPosMs);
        showNotification("⏪ REPLAY");
        update();
        return;
    }

    if (event->key() == Qt::Key_W) {
        currentPosMs = qMin(durationMs, currentPosMs + 2000);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }

    if (event->key() == Qt::Key_Left) {
        currentPosMs = qMax(0LL, currentPosMs - 16);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }
    else if (event->key() == Qt::Key_Right) {
        currentPosMs = qMin(durationMs, currentPosMs + 16);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }

    if (ctrl && !shift && event->key() == Qt::Key_Z) {
        undo();
        showNotification("UNDO");
        return;
    }
    else if (ctrl && shift && event->key() == Qt::Key_Z) {
        redo();
        showNotification("REDO");
        return;
    }

    else if (ctrl && event->key() == Qt::Key_G) {
        copyTrimmedGif();
    }
    else if (ctrl && shift && event->key() == Qt::Key_C) {
        copyTrimmedAudio();
    }
    else if (ctrl && event->key() == Qt::Key_C) {
        if (alt) copyTrimmedVideoMuted();
        else copyTrimmedVideo();
    }

    else if (event->key() == Qt::Key_Space) {
        emit requestTogglePlayback();
        event->accept();
    }
    else if (event->key() == Qt::Key_S) {
        saveState();
        splitAtPlayhead();
    }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        QSet<int> toDelete = selectedSegmentIndices;
        if (selectedSegmentIdx != -1) toDelete.insert(selectedSegmentIdx);

        if (!toDelete.isEmpty()) {

            QList<int> sortedIndices = toDelete.values();
            std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());

            for (int idx : sortedIndices) {
                if (idx >= 0 && idx < static_cast<int>(segments.size())) {
                    segments.erase(segments.begin() + idx);
                }
            }

            selectedSegmentIndices.clear();
            selectedSegmentIdx = -1;
            showNotification(QString("DELETED %1 CLIPS").arg(sortedIndices.size()));
            update();
            return;
        }
    }

    // Inside keyPressEvent, under the alt + A logic:
    else if (alt && event->key() == Qt::Key_A) {
        if (totalAudioTracks > 1) {
            currentAudioTrack = (currentAudioTrack + 1) % totalAudioTracks;

            loadAudioFast(currentFileUrl.toLocalFile());

            // Add this specific emission if not already there
            emit requestAudioTrackChange(currentAudioTrack);

            QString name = (currentAudioTrack < trackNames.size())
                           ? trackNames[currentAudioTrack]
                           : QString("Track %1").arg(currentAudioTrack + 1);

            showNotification(QString("🔊 %1").arg(name));
        }
    }
    else {
        QWidget::keyPressEvent(event);
    }
}