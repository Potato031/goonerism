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
    if (!handleGlobalKey(event)) {
        QWidget::keyPressEvent(event);
    }
}

// Runs the editor's keyboard commands. Called both from this widget's own
// keyPressEvent and from an application-level filter in MainWindow, so
// shortcuts like S (split) work no matter which panel has focus.
bool TimelineWidget::handleGlobalKey(QKeyEvent *event) {
    const auto settings = playbackSettings;
    auto *mainWindow = qobject_cast<MainWindow*>(window());
    const auto editorSettings = mainWindow ? mainWindow->getEditorSettings() : MainWindow::EditorSettings{};

    if (matchesShortcut(event, editorSettings.keyReplay) || event->key() == Qt::Key_0) {
        currentPosMs = qMax<qint64>(0LL, currentPosMs - settings.majorSeekMs);
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
        showNotification("⏪ REPLAY");
        update();
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyForward)) {
        currentPosMs = qMin(durationMs, currentPosMs + settings.majorSeekMs);
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
        update();
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyStepBack)) {
        currentPosMs = qMax<qint64>(0LL, currentPosMs - settings.minorSeekMs);
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
        update();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyStepForward)) {
        currentPosMs = qMin(durationMs, currentPosMs + settings.minorSeekMs);
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
        update();
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyUndo)) {
        undo();
        showNotification("UNDO");
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyRedo)) {
        redo();
        showNotification("REDO");
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyExportGif)) {
        copyTrimmedGif();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyExportAudio)) {
        copyTrimmedAudio();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyExportMutedVideo)) {
        copyTrimmedVideoMuted();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyExportVideo)) {
        copyTrimmedVideo();
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyPlayPause)) {
        emit requestTogglePlayback();
        event->accept();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keySplit)) {
        saveState();
        splitAtPlayhead();
        return true;
    }
    if (matchesShortcut(event, editorSettings.keyDeleteClip) || event->key() == Qt::Key_Backspace) {
        // A selected overlay clip wins over segment deletion.
        if (selectedOverlayIdx != -1) {
            deleteSelectedOverlay();
        } else {
            deleteActiveSelection();
        }
        return true;
    }

    if (matchesShortcut(event, editorSettings.keyCycleAudioTrack)) {
        if (totalAudioTracks > 1) {
            currentAudioTrack = (currentAudioTrack + 1) % totalAudioTracks;
            loadAudioFast(currentFileUrl.toLocalFile());
            emit requestAudioTrackChange(currentAudioTrack);

            QString name = (currentAudioTrack < trackNames.size())
                           ? trackNames[currentAudioTrack]
                           : QString("Track %1").arg(currentAudioTrack + 1);
            showNotification(QString("🔊 %1").arg(name));
        }
        return true;
    }

    return false;
}
