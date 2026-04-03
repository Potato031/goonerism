#include "../Includes/timelinewidget.h"
#include "../Includes/mainWindow.h"

namespace {
bool matchesShortcut(QKeyEvent *event, const QString &shortcut) {
    const QKeySequence configured = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
    if (configured.isEmpty()) return false;
    const int pressed = static_cast<int>(event->modifiers()) | event->key();
    return QKeySequence(pressed).matches(configured) == QKeySequence::ExactMatch;
}
}

void TimelineWidget::keyPressEvent(QKeyEvent *event) {
    const auto settings = playbackSettings;
    auto *mainWindow = qobject_cast<MainWindow*>(window());
    const auto editorSettings = mainWindow ? mainWindow->getEditorSettings() : MainWindow::EditorSettings{};

    if (matchesShortcut(event, editorSettings.keyReplay) || event->key() == Qt::Key_0) {
        currentPosMs = qMax<qint64>(0LL, currentPosMs - settings.majorSeekMs);
        emit playheadMoved(currentPosMs);
        showNotification("⏪ REPLAY");
        update();
        return;
    }

    if (matchesShortcut(event, editorSettings.keyForward)) {
        currentPosMs = qMin(durationMs, currentPosMs + settings.majorSeekMs);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }

    if (matchesShortcut(event, editorSettings.keyStepBack)) {
        currentPosMs = qMax<qint64>(0LL, currentPosMs - settings.minorSeekMs);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }
    else if (matchesShortcut(event, editorSettings.keyStepForward)) {
        currentPosMs = qMin(durationMs, currentPosMs + settings.minorSeekMs);
        emit playheadMoved(currentPosMs);
        update();
        return;
    }

    if (matchesShortcut(event, editorSettings.keyUndo)) {
        undo();
        showNotification("UNDO");
        return;
    }
    else if (matchesShortcut(event, editorSettings.keyRedo)) {
        redo();
        showNotification("REDO");
        return;
    }

    else if (matchesShortcut(event, editorSettings.keyExportGif)) {
        copyTrimmedGif();
    }
    else if (matchesShortcut(event, editorSettings.keyExportAudio)) {
        copyTrimmedAudio();
    }
    else if (matchesShortcut(event, editorSettings.keyExportMutedVideo)) {
        copyTrimmedVideoMuted();
    }
    else if (matchesShortcut(event, editorSettings.keyExportVideo)) {
        copyTrimmedVideo();
    }

    else if (matchesShortcut(event, editorSettings.keyPlayPause)) {
        emit requestTogglePlayback();
        event->accept();
    }
    else if (matchesShortcut(event, editorSettings.keySplit)) {
        saveState();
        splitAtPlayhead();
    }
    else if (matchesShortcut(event, editorSettings.keyDeleteClip) || event->key() == Qt::Key_Backspace) {
        QSet<int> toDelete = selectedSegmentIndices;
        if (selectedSegmentIdx != -1) toDelete.insert(selectedSegmentIdx);

        if (!toDelete.isEmpty()) {
            saveState();

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
            emit clipTrimmed();
            update();
            return;
        }
    }

    // Inside keyPressEvent, under the alt + A logic:
    else if (matchesShortcut(event, editorSettings.keyCycleAudioTrack)) {
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
