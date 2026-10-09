#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QTimer>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QVideoWidget>
#include <QClipboard>
#include <QMimeData>
#include <QPaintEvent>
#include "../src/Includes/mainWindow.h"
#include "../src/Includes/appsettings.h"
#include "../src/Includes/mediautils.h"

class EditorTests : public QObject {
    Q_OBJECT
    QTemporaryDir sandbox;
    QString video, audioFile;
    bool generate(const QStringList &args) {
        QProcess process;
        process.start("ffmpeg", args);
        return process.waitForFinished(15000) && process.exitCode() == 0;
    }
private slots:
    void initTestCase() {
        QVERIFY(sandbox.isValid());
        qputenv("POTATO_EDITOR_SETTINGS_DIR", sandbox.path().toUtf8());
        QCoreApplication::setApplicationName("PotatoEditorTests");
        QFile style(":/styles.qss");
        QVERIFY(style.open(QIODevice::ReadOnly));
        qApp->setProperty("baseStyleSheet", QString::fromUtf8(style.readAll()));
        video = sandbox.filePath("demo.mp4");
        audioFile = sandbox.filePath("tone.wav");
        QVERIFY(generate({"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30",
                          "-f", "lavfi", "-i", "sine=frequency=440", "-t", "3", "-c:v", "mpeg4", video}));
        QVERIFY(generate({"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=880", "-t", "1", audioFile}));
        auto settings = makeAppSettings();
        settings.setValue("ui/loadNewestVideoOnStartup", false);
        settings.setValue("ui/autoPlayOnImport", false);
        settings.setValue("ui/checkForUpdatesOnStartup", false);
    }
    void newestVideoLoadsAtStartup() {
        const QString directory = sandbox.filePath("startup");
        QDir().mkpath(directory);
        const QString oldVideo = directory + "/old.mp4";
        const QString newestVideo = directory + "/new.mp4";
        QVERIFY(QFile::copy(video, oldVideo));
        QVERIFY(QFile::copy(video, newestVideo));
        QFile old(oldVideo);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addSecs(-60), QFileDevice::FileModificationTime));
        old.close();
        auto settings = makeAppSettings();
        settings.setValue("ui/loadNewestVideoOnStartup", true);
        settings.setValue("ui/autoLoadDirectories", QStringList{directory});
        settings.setValue("ui/recentMediaLimit", 1);
        // An older explicitly imported file must not displace a newer recording.
        settings.setValue("media/recentFiles", QStringList{oldVideo});
        settings.sync();
        {
            MainWindow window;
            QTRY_COMPARE_WITH_TIMEOUT(window.currentMediaPath, newestVideo, 5000);
        }
        settings.setValue("ui/loadNewestVideoOnStartup", false);
        settings.remove("ui/autoLoadDirectories");
        settings.remove("ui/recentMediaLimit");
        settings.remove("media/recentFiles");
        settings.sync();
    }
    void nativePlaybackDoesNotRepaintEditingLayer() {
        struct PaintCounter : QObject {
            int count = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Paint) ++count;
                return false;
            }
        } counter;
        VideoWithCropWidget preview;
        QVideoSink sink;
        preview.resize(1280, 720);
        preview.attachVideoSink(&sink);
        preview.setPlaybackActive(true);
        preview.setNativePresentationActive(true);
        preview.show();
        QTest::qWait(30);
        preview.installEventFilter(&counter);
        QImage image(3840, 2160, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (int i = 0; i < 60; ++i) {
            QVideoFrame nextFrame(image);
            nextFrame.setStartTime(i * 16667);
            sink.setVideoFrame(nextFrame);
            QCoreApplication::processEvents();
        }
        qInfo() << "Editing-layer paints for 60 native frames:" << counter.count;
        QVERIFY2(counter.count <= 1, "Native playback must not repaint the transparent editing layer on every frame.");
    }
    void playbackRepaintsOnlyMovingPlayhead() {
        struct PaintCounter : QObject {
            qint64 pixels = 0;
            QRegion region;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Paint) {
                    region |= static_cast<QPaintEvent *>(event)->region();
                    for (const QRect &rect : static_cast<QPaintEvent *>(event)->region())
                        pixels += qint64(rect.width()) * rect.height();
                }
                return false;
            }
        } counter;
        TimelineWidget timeline;
        timeline.resize(1600, 240);
        timeline.durationMs = 7200000;
        timeline.segments.append({0, timeline.durationMs});
        timeline.selectedSegmentIdx = 0;
        timeline.audioSamples.resize(720000);
        for (size_t i = 0; i < timeline.audioSamples.size(); ++i)
            timeline.audioSamples[i] = float(i % 100) / 100;
        timeline.maxAmplitude = 1;
        timeline.show();
        QTest::qWait(30);
        timeline.installEventFilter(&counter);
        timeline.setPlaybackActive(true);
        qint64 paintNs = 0;
        for (int i = 1; i <= 120; ++i) {
            QTest::qWait(34);
            QElapsedTimer elapsed;
            elapsed.start();
            timeline.setCurrentPosition(i * 50000);
            QCoreApplication::processEvents();
            paintNs += elapsed.nsecsElapsed();
        }
        qInfo() << "120 long-timeline playback paints:" << paintNs / 1000000.0
                << "ms; painted pixels:" << counter.pixels;
        QVERIFY2(counter.pixels < qint64(120) * timeline.width() * timeline.height() / 4,
                 "Playback must not redraw the entire waveform for each playhead move.");
        // Incremental painting must produce exactly the same pixels as a full
        // redraw, including the waveform behind the previous playhead.
        QImage incremental = timeline.grab().toImage();
        counter.region = QRegion();
        QTest::qWait(34);
        timeline.setCurrentPosition(6100000);
        QCoreApplication::processEvents();
        const QRegion dirty = counter.region;
        QVERIFY(!dirty.isEmpty());
        timeline.render(&incremental, dirty.boundingRect().topLeft(), dirty);
        const QImage full = timeline.grab().toImage();
        QCOMPARE(incremental, full);
    }
    void profileInteractivePlayback() {
        const QString path = qEnvironmentVariable("POTATO_EDITOR_PROFILE_MEDIA");
        if (path.isEmpty()) QSKIP("Set POTATO_EDITOR_PROFILE_MEDIA to measure a real recording.");
        if (qEnvironmentVariableIntValue("POTATO_EDITOR_PROFILE_COLD") == 1) {
            const auto cache = MediaUtils::previewCachePath(path);
            QFile::remove(cache);
            QFile::remove(cache + ".timeline.jpg");
        }
        MainWindow window;
        window.resize(1600, 900);
        window.show();
        window.loadClipDirectly(path);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->durationMs > 0, 10000);
        window.player->play();
        QTest::qWait(300);
        QVector<qint64> gaps;
        QElapsedTimer clock;
        clock.start();
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat, &QTimer::timeout, [&]() { gaps.append(clock.restart()); });
        heartbeat.start(5);
        // Run the real event loop: qWait polls it in intervals that distort a
        // five-millisecond heartbeat and inflate measured input latency.
        QEventLoop playbackLoop;
        QTimer::singleShot(4000, &playbackLoop, &QEventLoop::quit);
        playbackLoop.exec();
        heartbeat.stop();
        std::sort(gaps.begin(), gaps.end());
        QVERIFY(!gaps.isEmpty());
        const auto p95 = gaps[gaps.size() * 95 / 100];
        const auto worst = gaps.last();
        qInfo() << "UI heartbeat during 4K playback: p95" << p95 << "ms; worst" << worst << "ms; samples" << gaps.size();
        QVERIFY2(p95 < 25 && worst < 100, "Video playback exceeds the UI responsiveness budget.");
    }
    void emptyWorkspaceAndSmallLayout() {
        MainWindow window;
        window.resize(1280, 850);
        window.show();
        QTest::qWait(60);
        QVERIFY(window.currentMediaPath.isEmpty());
        QVERIFY(window.emptyImportBtn->isVisible());
        QVERIFY(!window.exportBtn->isEnabled());
        QVERIFY(!window.undoBtn->isEnabled());
        window.grab().save(sandbox.filePath("empty.png"));
        window.resize(960, 620);
        QTest::qWait(40);
        QVERIFY(window.transportBar->width() >= window.transportBar->minimumSizeHint().width());
        QVERIFY(window.videoContainer->height() > 100);
        QCOMPARE(window.width(), 960);
        auto *body = window.emptyPreviewPanel->findChild<QLabel*>("EmptyPreviewBody");
        QVERIFY(body->geometry().bottom() < window.emptyImportBtn->geometry().top());
        QVERIFY(window.findChild<QScrollArea*>("ToolScroll"));
        window.grab().save(sandbox.filePath("compact.png"));
    }
    void importHonorsAutoplayAndProducesWaveform() {
        MainWindow window;
        window.resize(1280, 850);
        window.show();
        window.loadClipDirectly(video);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->durationMs > 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->sourceHasVideo(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.timeline->audioSamples.isEmpty(), 10000);
        QCOMPARE(window.player->playbackState(), QMediaPlayer::PausedState);
        QTRY_VERIFY_WITH_TIMEOUT(!window.videoWithCrop->lastFrame.isNull(), 10000);
        window.resize(960, 620);
        QTest::qWait(30);
        QCOMPARE(window.width(), 960);
        window.resize(1280, 850);
        QVERIFY(window.exportVideoAction->isEnabled());
        QVERIFY(!window.emptyImportBtn->isVisible());
        QCOMPARE(window.timeline->sources[0].hasAudio, true);
        // AAC padding differs between FFmpeg versions. Keep the 100 Hz
        // waveform aligned to the three-second fixture within one AAC frame.
        QVERIFY(qAbs(window.timeline->audioSamples.size() * 10 - 3000) <= 30);
        window.grab().save(sandbox.filePath("loaded.png"));
        // Entering text must never invoke a playback shortcut.
        window.exportInput->setFocus();
        QTest::keyClicks(window.exportInput, "hello world");
        QCOMPARE(window.exportInput->text(), QString("hello world"));
        QCOMPARE(window.player->playbackState(), QMediaPlayer::PausedState);
    }
    void rapidSwitchKeepsLatestMediaAndClearsMarkers() {
        MainWindow window;
        window.loadClipDirectly(video);
        window.timeline->markers.append(200);
        window.loadClipDirectly(audioFile);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->sourceHasAudio(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!window.timeline->audioSamples.isEmpty(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window.timeline->durationMs, qint64(1000), 10000);
        QVERIFY(!window.timeline->sourceHasVideo());
        QVERIFY(window.timeline->markers.isEmpty());
        QCOMPARE(window.timeline->sources[0].path, audioFile);
        QCOMPARE(window.timeline->audioSamples.size(), 100);
        QVERIFY(!window.exportVideoAction->isEnabled());
        QVERIFY(window.exportAudioAction->isEnabled());
    }
    void effectsCanBeDroppedDuringNativePlayback() {
        MainWindow window;
        window.resize(1280, 850);
        window.show();
        window.loadClipDirectly(video);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->sourceHasVideo() && window.timeline->durationMs > 0, 10000);
        window.player->play();
        QTRY_VERIFY(window.player->playbackState() == QMediaPlayer::PlayingState);
        QVERIFY(window.videoWithCrop->isHidden());
        QMimeData mime;
        mime.setData("application/x-potato-overlay", "0");
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window.nativeVideoWidget, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window.nativeVideoWidget, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(window.timeline->overlays.size(), 1);
        QCOMPARE(window.timeline->overlays[0].type, 0);
        QTRY_VERIFY(window.videoWithCrop->isVisible());
    }
    void filmstripRequestsShareOneDecoderJob() {
        const QString copy = sandbox.filePath("unique-filmstrip.mp4");
        QVERIFY(QFile::copy(video, copy));
        TimelineWidget timeline;
        TimelineWidget::SourceClip source;
        source.path = copy;
        source.durationMs = 3000;
        timeline.sources.append(source);
        timeline.ensureSourceFilmstrip(0);
        timeline.ensureSourceFilmstrip(0);
        QCOMPARE(timeline.findChildren<QProcess*>().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(timeline.sourceFilmstrips.contains(0), 5000);
        QCOMPARE(timeline.sourceFilmstrips.value(0).size(), QSize(960, 90));
    }
    void overlayDoesNotChangeRulerText() {
        TimelineWidget timeline;
        timeline.resize(1000, 400);
        timeline.setDuration(10000);
        timeline.show();
        QTest::qWait(30);
        const QRect ruler(timeline.sidebarWidth + 4, 1, 700, timeline.rulerHeight - 10);
        const QImage before = timeline.grab(ruler).toImage();
        for (int type = 0; type <= 5; ++type) {
            timeline.addOverlayAt(type, 0);
            QTest::qWait(30);
            const QImage after = timeline.grab(ruler).toImage();
            QVERIFY2(before == after, "Adding an overlay changes the existing ruler text rendering.");
        }
    }
    void invalidSplitLeavesHistoryUntouched() {
        TimelineWidget timeline;
        timeline.setDuration(3000);
        timeline.setCurrentPosition(0);
        timeline.requestSplit();
        QVERIFY(!timeline.canUndo());
        timeline.setCurrentPosition(1500);
        timeline.requestSplit();
        QCOMPARE(timeline.segments.size(), 2);
        QVERIFY(timeline.canUndo());
        timeline.undo();
        QCOMPARE(timeline.segments.size(), 1);
        timeline.redo();
        QCOMPARE(timeline.segments.size(), 2);
    }
    void rulerScrubbingAndTrimUndo() {
        TimelineWidget timeline;
        timeline.resize(600, 220);
        timeline.setDuration(3000);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(300, 10));
        QCOMPARE(timeline.currentPosMs, qint64(1500));
        timeline.setCurrentPosition(0);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(599, 80));
        QMouseEvent move(QEvent::MouseMove, QPointF(400, 80), QPointF(400, 80),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&timeline, &move);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(400, 80));
        QCOMPARE(timeline.segments[0].endMs, qint64(2000));
        timeline.undo();
        QCOMPARE(timeline.segments[0].endMs, qint64(3000));
    }
    void deletingOverlayUsesSameButtonAsKeyboard() {
        TimelineWidget timeline;
        timeline.setDuration(3000);
        timeline.addOverlayAt(0, 100);
        timeline.deleteActiveSelection();
        QVERIFY(timeline.overlays.isEmpty());
        QCOMPARE(timeline.segments.size(), 1);
        timeline.undo();
        QCOMPARE(timeline.overlays.size(), 1);
    }
    void undoAppendRestoresWholeComposition() {
        TimelineWidget timeline;
        timeline.setMediaSource(QUrl::fromLocalFile(video));
        timeline.setDuration(3000);
        timeline.appendMediaSource(audioFile);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.sources.size(), 2, 10000);
        QCOMPARE(timeline.durationMs, qint64(4000));
        timeline.undo();
        QCOMPARE(timeline.sources.size(), 1);
        QCOMPARE(timeline.durationMs, qint64(3000));
        timeline.redo();
        QCOMPARE(timeline.sources.size(), 2);
        QCOMPARE(timeline.durationMs, qint64(4000));
    }
    void mediaSearchAndPanelCommands() {
        MainWindow window;
        window.cachedRecentFiles = {video, audioFile};
        window.recentFilesScanned = true;
        window.updateSidebar();
        window.mediaSearch->setText("tone");
        QVERIFY(window.sidebarListLayout->itemAt(0)->widget()->isHidden());
        QVERIFY(!window.sidebarListLayout->itemAt(1)->widget()->isHidden());
        window.mediaSearch->setText("nothing-matches");
        QVERIFY(!window.sidebarEmptyLabel->isHidden());
        window.viewMenu->actions()[0]->setChecked(false);
        window.viewMenu->actions()[1]->setChecked(false);
        QVERIFY(window.clipSidebar->isHidden());
        QVERIFY(window.timelineTools->parentWidget()->parentWidget()->isHidden());
        window.resetPanelLayout();
        QVERIFY(!window.clipSidebar->isHidden());
        QVERIFY(!window.timelineTools->parentWidget()->parentWidget()->isHidden());
    }
    void commandSearchRunsChosenAction() {
        MainWindow window;
        window.timeline->setZoomFactor(8);
        bool found = false;
        QTimer::singleShot(80, &window, [&]() {
            auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *search = dialog->findChild<QLineEdit*>("CommandSearch");
            if (!search) { dialog->reject(); return; }
            found = true;
            search->setText("fit timeline");
            dialog->grab().save(sandbox.filePath("commands.png"));
            QTest::keyClick(search, Qt::Key_Return);
        });
        window.showCommandPalette();
        QVERIFY(found);
        QCOMPARE(window.timeline->getZoomFactor(), 1.0);
    }
    void audioExportProducesPlayableFile() {
        MainWindow window;
        window.loadClipDirectly(audioFile);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->sourceHasAudio(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window.timeline->durationMs > 0, 10000);
        makeAppSettings().setValue("export/exportDirectory", sandbox.filePath("exports"));
        QSignalSpy finished(window.timeline, &TimelineWidget::exportFinished);
        window.timeline->copyTrimmedAudio();
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 15000);
        QVERIFY(finished.first().first().toBool());
        QVERIFY(!window.exportBusy);
        const auto urls = QApplication::clipboard()->mimeData()->urls();
        QVERIFY(!urls.isEmpty());
        QVERIFY(QFileInfo(urls.first().toLocalFile()).size() > 0);
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", urls.first().toLocalFile()});
        QVERIFY(probe.waitForFinished(5000));
        QCOMPARE(probe.exitCode(), 0);
        QVERIFY(probe.readAllStandardOutput().trimmed().toDouble() > 0.9);
    }
    void autoCutDoesNotOverwriteNewEdits() {
        TimelineWidget timeline;
        timeline.setMediaSource(QUrl::fromLocalFile(audioFile));
        timeline.setDuration(1000);
        QTRY_VERIFY_WITH_TIMEOUT(timeline.sourceHasAudio(), 10000);
        timeline.autoCutSilence();
        timeline.setCurrentPosition(500);
        timeline.requestSplit();
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.autoCutBusy, 10000);
        QCOMPARE(timeline.segments.size(), 2);
        QCOMPARE(timeline.undoHistoryLabels().size(), 1);
    }
    void failedExportReleasesBusyState() {
        MainWindow window;
        auto *process = new QProcess(window.timeline);
        window.timeline->isExporting = true;
        window.timeline->showProgressNotification(process, 1000);
        QVERIFY(window.exportBusy);
        process->start(sandbox.filePath("missing-ffmpeg"));
        QTRY_VERIFY(!window.exportBusy);
        QVERIFY(!window.timeline->isExporting);
        QVERIFY(window.importBtn->isEnabled());
    }
    void closingTimelineDisconnectsProcessCallbacks() {
        auto *timeline = new TimelineWidget;
        auto *process = new QProcess(timeline);
        int completions = 0;
        connect(process, &QProcess::finished, timeline, [timeline, &completions]() {
            ++completions;
            timeline->sourceFilmstrips[0] = QImage(16, 16, QImage::Format_RGB32);
        });
        process->start("ffmpeg", {"-v", "error", "-re", "-i", video, "-f", "null", "-"});
        QVERIFY(process->waitForStarted(5000));
        delete timeline;
        QCOMPARE(completions, 0);
    }
    void closingPreviewDuringCompositionIsSafe() {
        auto *preview = new VideoWithCropWidget;
        preview->resize(640, 360);
        QImage image(1920, 1080, QImage::Format_ARGB32);
        image.fill(Qt::red);
        preview->m_lastRawFrame = QVideoFrame(image);
        preview->filterObjects.append({0, 0, 1, 1, 0, {}});
        preview->triggerScale();
        delete preview;
        QTest::qWait(100);
    }
    void previewCacheIncludesFullPathAndModification() {
        const QString first = MediaUtils::previewCachePath(video);
        const QString other = sandbox.filePath("elsewhere/demo.mp4");
        QVERIFY(first != MediaUtils::previewCachePath(other));
        QFile file(video);
        QVERIFY(file.open(QIODevice::Append));
        file.write("x"); file.close();
        QVERIFY(first != MediaUtils::previewCachePath(video));
    }
    void zoomedTimelinePaintBenchmark() {
        TimelineWidget timeline;
        timeline.resize(1200, 220);
        timeline.setDuration(3600000);
        timeline.audioSamples.fill(0.5f, 360000);
        timeline.setZoomFactor(100);
        QImage image(timeline.size(), QImage::Format_ARGB32_Premultiplied);
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < 30; ++i) timeline.render(&image);
        qInfo() << "30 timeline paints at 100x zoom:" << clock.elapsed() << "ms";
        QVERIFY(clock.elapsed() < 5000);
    }
    void cleanupTestCase() {
        // Keep visual artifacts outside the temporary fixture directory.
        for (const QString &name : {"empty.png", "compact.png", "loaded.png", "commands.png"}) {
            QFile::remove("/tmp/potato-editor-" + name);
            QFile::copy(sandbox.filePath(name), "/tmp/potato-editor-" + name);
        }
    }
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
