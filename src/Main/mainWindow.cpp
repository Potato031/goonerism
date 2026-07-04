#include "../Includes/mainWindow.h"
#include <iostream>
#include "../Includes/resizeFilter.h"
#include "../Includes/dropFilter.h"
#include "../Includes/timelinewidget.h"
#include "../Includes/previewLabel.h"
#include "../Includes/mediautils.h"
#include "../Includes/appsettings.h"
#include "../Includes/icons.h"
#include "../Includes/dragToolButton.h"
#include <QMenu>
#include <QProgressBar>
#include <QInputDialog>
#include <QAbstractSpinBox>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QDir>
#include <QListWidget>
#include <QFormLayout>
#include <QCheckBox>
#include <QStyle>
#include <QFileInfo>
#include <QShortcut>
#include <QLineEdit>
#include <QApplication>
#include <QGraphicsBlurEffect>
#include <QStandardPaths>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QTimer>
#include <QGridLayout>
#include <QResizeEvent>
#include <QDateTime>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QColorDialog>
#include <QKeySequenceEdit>
#include <QFontComboBox>
#include <algorithm>
#include <cmath>

namespace {

QString buildMediaBadgeText(bool hasVideo, bool hasAudio) {
    if (hasVideo && hasAudio) return "VIDEO + AUDIO";
    if (hasVideo) return "VIDEO ONLY";
    if (hasAudio) return "AUDIO ONLY";
    return "MEDIA";
}

QString defaultExportDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Edited";
}

QString withFallbackColor(const QString &value, const QString &fallback) {
    const QColor color(value.trimmed());
    return color.isValid() ? color.name(QColor::HexRgb) : fallback;
}

QString formatTimecode(qint64 ms) {
    ms = qMax<qint64>(0, ms);
    const qint64 totalSeconds = ms / 1000;
    return QString("%1:%2.%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    loadEditorSettings();
    this->setAcceptDrops(true);
    DropFilter* filter = new DropFilter([this](const QString &path) {
        loadClipDirectly(path);
    }, this);
    qApp->installEventFilter(filter);
    // Route editor shortcuts (split, seek, export…) to the timeline no matter
    // which panel currently has keyboard focus.
    qApp->installEventFilter(this);
    setupConnections();
    loadInitialVideo();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setObjectName("centralWidget");
    centralWidget->setAcceptDrops(true);
    setCentralWidget(centralWidget);
    setObjectName("MainCanvas");
    setWindowTitle("Potato Editor Studio");

    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setupToolbar();

    mainSplitter = new QSplitter(Qt::Vertical, centralWidget);
    mainSplitter->setHandleWidth(6);
    mainSplitter->setChildrenCollapsible(false);

    topPaneSplitter = new QSplitter(Qt::Horizontal, mainSplitter);
    topPaneSplitter->setHandleWidth(6);
    topPaneSplitter->setChildrenCollapsible(false);

    setupSidebar();
    setupWorkspace();

    // Tool Rack (Effects)
    timelineTools = new QFrame();
    timelineTools->setObjectName("timelineTools");
    timelineTools->setFixedWidth(200);
    timelineToolsLayout = new QVBoxLayout(timelineTools);
    timelineToolsLayout->setContentsMargins(12, 12, 12, 12);
    timelineToolsLayout->setSpacing(8);

    toolHeaderLabel = new QLabel("INSPECTOR");
    toolHeaderLabel->setObjectName("SectionHeader");
    timelineToolsLayout->addWidget(toolHeaderLabel);

    redactGroupLabel = new QLabel("OVERLAYS · CLICK OR DRAG ONTO VIDEO");
    redactGroupLabel->setObjectName("InspectorGroupLabel");
    redactGroupLabel->setWordWrap(true);

    textBtn = new DragToolButton(3, "T  Text");
    blurBtn = new DragToolButton(0, "◐  Blur region");
    pixelBtn = new DragToolButton(1, "▦  Pixelate region");
    solidBtn = new DragToolButton(2, "■  Blackout region");
    actionsGroupLabel = new QLabel("ACTIONS");
    actionsGroupLabel->setObjectName("InspectorGroupLabel");
    autoCutBtn = new QPushButton("✂  Auto-cut silence");
    resetCropBtn = new QPushButton("⤺  Reset crop");
    for (QPushButton *button : {textBtn, blurBtn, pixelBtn, solidBtn, autoCutBtn, resetCropBtn}) {
        button->setProperty("class", "ToolBtn");
        button->setLayoutDirection(Qt::LeftToRight);
    }
    textBtn->setToolTip("Add a text overlay at the playhead (or drag onto the video/timeline)");
    blurBtn->setToolTip("Add a blur overlay at the playhead (or drag onto the video/timeline)");
    pixelBtn->setToolTip("Add a pixelate overlay at the playhead (or drag onto the video/timeline)");
    solidBtn->setToolTip("Add a blackout overlay at the playhead (or drag onto the video/timeline)");

    timelineToolsLayout->addWidget(redactGroupLabel);
    timelineToolsLayout->addWidget(textBtn);
    timelineToolsLayout->addWidget(blurBtn);
    timelineToolsLayout->addWidget(pixelBtn);
    timelineToolsLayout->addWidget(solidBtn);
    timelineToolsLayout->addSpacing(10);
    timelineToolsLayout->addWidget(actionsGroupLabel);
    timelineToolsLayout->addWidget(autoCutBtn);
    timelineToolsLayout->addWidget(resetCropBtn);
    timelineToolsLayout->addStretch();

    topPaneSplitter->addWidget(clipSidebar);
    topPaneSplitter->addWidget(workspace);
    topPaneSplitter->addWidget(timelineTools);
    topPaneSplitter->setStretchFactor(1, 1);

    mainSplitter->addWidget(topPaneSplitter);
    setupTimeline();
    mainSplitter->addWidget(timelineShell);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setSizes({600, 240});

    // Gutter around the panel area so rounded panels don't touch window edges.
    auto* splitterWrap = new QWidget(centralWidget);
    auto* splitterWrapLayout = new QVBoxLayout(splitterWrap);
    splitterWrapLayout->setContentsMargins(8, 8, 8, 8);
    splitterWrapLayout->addWidget(mainSplitter);
    mainLayout->addWidget(splitterWrap, 1);

    footer = new QFrame(); footer->hide(); mainLayout->addWidget(footer);
    transportHintLabel = new QLabel(); transportHintLabel->hide();

    player = new QMediaPlayer(this);
    audio = new QAudioOutput(this);
    player->setAudioOutput(audio);
    player->setVideoSink(videoWithCrop->sink);
    audio->setVolume(0.8);
    
    playPauseShortcut = nullptr;
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importMedia);
}

void MainWindow::setupToolbar() {
    toolbar = new QFrame();
    toolbar->setObjectName("toolbar");
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(16, 0, 12, 0);
    toolbarLayout->setSpacing(10);

    auto* logoLayout = new QHBoxLayout();
    logoLayout->setSpacing(5);
    logoBoldLabel = new QLabel("POTATO");
    logoBoldLabel->setObjectName("LogoBold");
    logoLightLabel = new QLabel("STUDIO");
    logoLightLabel->setObjectName("LogoLight");
    logoLayout->addWidget(logoBoldLabel);
    logoLayout->addWidget(logoLightLabel);
    toolbarLayout->addLayout(logoLayout);

    toolbarLayout->addSpacing(14);

    currentMediaLabel = new QLabel("NO MEDIA LOADED");
    currentMediaLabel->setObjectName("CurrentMediaPill");
    toolbarLayout->addWidget(currentMediaLabel);

    toolbarLayout->addStretch();

    exportInput = new QLineEdit();
    exportInput->setObjectName("ExportNameInput");
    exportInput->setPlaceholderText("Clip name…");
    exportInput->setFixedWidth(220);
    exportInput->setClearButtonEnabled(true);
    toolbarLayout->addWidget(exportInput);

    importBtn = new QPushButton("IMPORT");
    importBtn->setObjectName("PrimaryGhostBtn");
    importBtn->setMinimumWidth(90);
    importBtn->setMinimumHeight(30);
    importBtn->setCursor(Qt::PointingHandCursor);
    toolbarLayout->addWidget(importBtn);

    exportBtn = new QPushButton("EXPORT");
    exportBtn->setObjectName("ExportBtn");
    exportBtn->setMinimumHeight(30);
    exportBtn->setCursor(Qt::PointingHandCursor);
    exportMenu = new QMenu(exportBtn);
    exportVideoAction = exportMenu->addAction("Export Video");
    exportMutedAction = exportMenu->addAction("Export Video (Muted)");
    exportAudioAction = exportMenu->addAction("Export Audio");
    exportGifAction = exportMenu->addAction("Export GIF");
    exportBtn->setMenu(exportMenu);
    toolbarLayout->addWidget(exportBtn);

    toolbarLayout->addSpacing(4);

    helpBtn = new QPushButton();
    helpBtn->setProperty("class", "IconBtn");
    helpBtn->setFixedSize(32, 32);
    helpBtn->setToolTip("Keyboard shortcuts");
    helpBtn->setCursor(Qt::PointingHandCursor);
    toolbarLayout->addWidget(helpBtn);

    settingsBtn = new QPushButton();
    settingsBtn->setProperty("class", "IconBtn");
    settingsBtn->setFixedSize(32, 32);
    settingsBtn->setToolTip("Settings");
    settingsBtn->setCursor(Qt::PointingHandCursor);
    toolbarLayout->addWidget(settingsBtn);

    mainLayout->addWidget(toolbar);
}

void MainWindow::setupSidebar() {
    clipSidebar = new QFrame();
    clipSidebar->setObjectName("clipSidebar");
    auto* sidebarLayout = new QVBoxLayout(clipSidebar);
    sidebarLayout->setContentsMargins(12, 12, 12, 12);
    sidebarLayout->setSpacing(8);
    
    previewHeader = new QFrame();
    auto* previewHeaderLayout = new QHBoxLayout(previewHeader);
    previewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto* mediaHeader = new QLabel("MEDIA BIN");
    mediaHeader->setObjectName("SectionHeader");
    previewHeaderLayout->addWidget(mediaHeader);
    previewHeaderLayout->addStretch();
    sidebarImportBtn = new QPushButton();
    sidebarImportBtn->setProperty("class", "IconBtn");
    sidebarImportBtn->setFixedSize(24, 24);
    sidebarImportBtn->setToolTip("Import media");
    sidebarImportBtn->setCursor(Qt::PointingHandCursor);
    previewHeaderLayout->addWidget(sidebarImportBtn);
    sidebarLayout->addWidget(previewHeader);

    sidebarScroll = new QScrollArea();
    sidebarScroll->setWidgetResizable(true);
    sidebarScroll->setFrameShape(QFrame::NoFrame);
    sidebarContent = new QWidget();
    sidebarListLayout = new QVBoxLayout(sidebarContent);
    sidebarListLayout->setContentsMargins(0, 0, 0, 0);
    sidebarListLayout->setSpacing(2);
    sidebarListLayout->setAlignment(Qt::AlignTop);
    sidebarScroll->setWidget(sidebarContent);
    sidebarLayout->addWidget(sidebarScroll);

    sidebarCountLabel = new QLabel("0 ITEMS");
    sidebarCountLabel->setObjectName("SidebarCountLabel");
    sidebarLayout->addWidget(sidebarCountLabel);
    
    sidebarEmptyLabel = new QLabel(); sidebarEmptyLabel->hide();
}

void MainWindow::setupWorkspace() {
    workspace = new QFrame();
    workspace->setObjectName("workspace");
    stageColumnLayout = new QVBoxLayout(workspace);
    stageColumnLayout->setContentsMargins(14, 14, 14, 14);
    stageColumnLayout->setSpacing(10);

    workspaceContentLayout = new QHBoxLayout();

    videoContainer = new QFrame();
    videoContainer->setObjectName("VideoContainer");
    auto* videoInternalLayout = new QVBoxLayout(videoContainer);
    videoInternalLayout->setContentsMargins(0, 0, 0, 0);

    videoWithCrop = new VideoWithCropWidget(videoContainer);
    videoInternalLayout->addWidget(videoWithCrop);
    stageColumnLayout->addWidget(videoContainer, 1);

    // --- Transport bar: timecode | jump/step/play controls | volume · speed · snapshot · fullscreen
    transportBar = new QFrame();
    transportBar->setObjectName("TransportBar");
    auto* transportLayout = new QHBoxLayout(transportBar);
    transportLayout->setContentsMargins(12, 5, 12, 5);
    transportLayout->setSpacing(6);

    timecodeLabel = new QLabel("00:00.000 / 00:00.000");
    timecodeLabel->setObjectName("TimecodeLabel");
    transportLayout->addWidget(timecodeLabel);

    transportLayout->addStretch();

    auto makeTransportBtn = [](const QString &tooltip, int size = 34) {
        auto* btn = new QPushButton();
        btn->setProperty("class", "IconBtn");
        btn->setFixedSize(size, size);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    jumpBackBtn = makeTransportBtn("Jump back");
    stepBackBtn = makeTransportBtn("Previous frame");
    playPauseBtn = new QPushButton();
    playPauseBtn->setObjectName("ActionBtn");
    playPauseBtn->setFixedSize(42, 42);
    playPauseBtn->setIconSize(QSize(20, 20));
    playPauseBtn->setCursor(Qt::PointingHandCursor);
    playPauseBtn->setFocusPolicy(Qt::NoFocus);
    playPauseBtn->setToolTip("Play / pause");
    stepFwdBtn = makeTransportBtn("Next frame");
    jumpFwdBtn = makeTransportBtn("Jump forward");

    transportLayout->addWidget(jumpBackBtn, 0, Qt::AlignVCenter);
    transportLayout->addWidget(stepBackBtn, 0, Qt::AlignVCenter);
    transportLayout->addWidget(playPauseBtn, 0, Qt::AlignVCenter);
    transportLayout->addWidget(stepFwdBtn, 0, Qt::AlignVCenter);
    transportLayout->addWidget(jumpFwdBtn, 0, Qt::AlignVCenter);

    transportLayout->addStretch();

    muteBtn = makeTransportBtn("Mute", 30);
    muteBtn->setCheckable(true);
    transportLayout->addWidget(muteBtn, 0, Qt::AlignVCenter);

    volSlider = new QSlider(Qt::Horizontal);
    volSlider->setObjectName("VolumeSlider");
    volSlider->setRange(0, 100);
    volSlider->setValue(80);
    volSlider->setFixedWidth(88);
    volSlider->setToolTip("Volume");
    volSlider->setCursor(Qt::PointingHandCursor);
    volSlider->setFocusPolicy(Qt::NoFocus);
    transportLayout->addWidget(volSlider, 0, Qt::AlignVCenter);

    transportLayout->addSpacing(6);

    speedBox = new QComboBox();
    speedBox->setObjectName("SpeedBox");
    speedBox->addItems({"0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"});
    speedBox->setCurrentText("1x");
    speedBox->setToolTip("Playback speed");
    speedBox->setCursor(Qt::PointingHandCursor);
    speedBox->setFocusPolicy(Qt::NoFocus);
    transportLayout->addWidget(speedBox, 0, Qt::AlignVCenter);

    transportLayout->addSpacing(6);

    snapshotBtn = makeTransportBtn("Save current frame as PNG", 30);
    transportLayout->addWidget(snapshotBtn, 0, Qt::AlignVCenter);

    fullscreenBtn = makeTransportBtn("Fullscreen preview", 30);
    transportLayout->addWidget(fullscreenBtn, 0, Qt::AlignVCenter);

    stageColumnLayout->addWidget(transportBar);
}

void MainWindow::setupTimeline() {
    timelineShell = new QFrame();
    timelineShell->setObjectName("TimelineShell");
    auto* timelineShellLayout = new QVBoxLayout(timelineShell);
    timelineShellLayout->setContentsMargins(12, 12, 12, 12);
    timelineShellLayout->setSpacing(8);

    auto* timelineHeader = new QHBoxLayout();
    timelineHeader->setSpacing(8);
    auto* timelineLabel = new QLabel("TIMELINE");
    timelineLabel->setObjectName("SectionHeader");
    timelineHeader->addWidget(timelineLabel);

    timelineHeader->addSpacing(6);

    auto makeEditBtn = [](const QString &tooltip) {
        auto* btn = new QPushButton();
        btn->setProperty("class", "IconBtn");
        btn->setFixedSize(28, 28);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };
    undoBtn = makeEditBtn("Undo");
    redoBtn = makeEditBtn("Redo");
    splitBtn = makeEditBtn("Split clip at playhead");
    deleteClipBtn = makeEditBtn("Delete selected clips");
    timelineHeader->addWidget(undoBtn);
    timelineHeader->addWidget(redoBtn);
    timelineHeader->addWidget(splitBtn);
    timelineHeader->addWidget(deleteClipBtn);

    timelineHeader->addStretch();

    audioTrackChip = new QLabel();
    audioTrackChip->setObjectName("MiniBadge");
    audioTrackChip->setToolTip("Active audio track (Alt+A cycles)");
    audioTrackChip->hide();
    timelineHeader->addWidget(audioTrackChip);

    estSizeChip = new QLabel();
    estSizeChip->setObjectName("MiniBadge");
    estSizeChip->setToolTip("Estimated export size");
    estSizeChip->hide();
    timelineHeader->addWidget(estSizeChip);

    timelineHeader->addSpacing(4);

    exportProgressBar = new QProgressBar();
    exportProgressBar->setObjectName("ExportProgressBar");
    exportProgressBar->setRange(0, 100);
    exportProgressBar->setTextVisible(true);
    exportProgressBar->setFormat("%p%");
    exportProgressBar->setFixedSize(180, 16);
    exportProgressBar->hide();
    timelineHeader->addWidget(exportProgressBar);

    statusLabel = new QLabel("READY");
    statusLabel->setObjectName("StatusLabel");
    timelineHeader->addWidget(statusLabel);

    timelineHeader->addSpacing(8);

    auto* zoomLabel = new QLabel("ZOOM");
    zoomLabel->setObjectName("InspectorGroupLabel");
    timelineHeader->addWidget(zoomLabel);

    timelineZoomSlider = new QSlider(Qt::Horizontal);
    timelineZoomSlider->setObjectName("ZoomSlider");
    timelineZoomSlider->setRange(0, 1000);
    timelineZoomSlider->setValue(0);
    timelineZoomSlider->setFixedWidth(110);
    timelineHeader->addWidget(timelineZoomSlider);

    timelineFitBtn = new QPushButton("FIT");
    timelineFitBtn->setObjectName("PrimaryGhostBtn");
    timelineFitBtn->setFixedWidth(40);
    timelineHeader->addWidget(timelineFitBtn);

    timelineShellLayout->addLayout(timelineHeader);

    // The timeline grows taller as overlay lanes stack up; a scroll area keeps
    // the audio waveform reachable instead of squeezing it off-screen.
    timeline = new TimelineWidget(nullptr);
    auto* timelineScroll = new QScrollArea(timelineShell);
    timelineScroll->setObjectName("TimelineScroll");
    timelineScroll->setWidgetResizable(true);
    timelineScroll->setFrameShape(QFrame::NoFrame);
    timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timelineScroll->setWidget(timeline);
    timelineShellLayout->addWidget(timelineScroll);
}

void MainWindow::setupConnections() {
    connect(timelineZoomSlider, &QSlider::valueChanged, this, [this](int v) {
        if (isUpdating) return;
        const double z = std::pow(100.0, v / 1000.0);
        timeline->setZoomFactor(z);
    });
    connect(timeline, &TimelineWidget::zoomChanged, this, [this](double z) {
        const double v = 1000.0 * (std::log(qMax(1.0, z)) / std::log(100.0));
        isUpdating = true;
        timelineZoomSlider->setValue(qBound(0, qRound(v), 1000));
        isUpdating = false;
    });
    connect(timelineFitBtn, &QPushButton::clicked, this, [this]() { timeline->resetZoomView(); });

    connect(volSlider, &QSlider::valueChanged, this, &MainWindow::updateVolume);
    connect(timeline, &TimelineWidget::audioGainChanged, this, &MainWindow::updateVolume);
    connect(player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::handlePlaybackState);
    connect(timeline, &TimelineWidget::requestTogglePlayback, [this]() {
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });

    connect(fullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleVideoFullscreen);

    connect(playPauseBtn, &QPushButton::clicked, [this]() {
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });

    connect(player, &QMediaPlayer::positionChanged, [this](qint64 pos) {
        // Player positions are local to the file currently loaded; the
        // timeline works in composition time.
        const qint64 timelinePos = pos + timeline->sourceOffsetMs(activeSourceIdx);
        timeline->setCurrentPosition(timelinePos);
        updateVolume();
        // Overlays fade in/out as the playhead crosses their clips.
        if (timeline->overlaysAtTime(timelinePos) != previewOverlayMap) syncOverlaysToPreview();
        if (player->playbackState() == QMediaPlayer::PlayingState) {
            timeline->validatePlayheadPosition();
            if (timeline->currentPosMs != timelinePos) seekTimeline(timeline->currentPosMs);
            if (timeline->currentPosMs >= timeline->getEndLimit()) seekTimeline(timeline->getStartLimit());

            // When playback runs past the end of the currently loaded file but
            // the composition continues (appended clips), jump to what's next.
            const auto &sources = timeline->sources;
            if (activeSourceIdx >= 0 && activeSourceIdx < sources.size()) {
                const qint64 sourceEnd = sources[activeSourceIdx].offsetMs + sources[activeSourceIdx].durationMs;
                if (sources[activeSourceIdx].durationMs > 0 && timelinePos >= sourceEnd - 30 &&
                    timeline->getEndLimit() > sourceEnd) {
                    seekTimeline(sourceEnd);
                }
            }
            timeline->update();
        } else {
            timeline->update();
        }
        updateTimecodeDisplay();
    });

    // --- Overlay clips: inspector buttons + drag-drop + preview region sync ---
    connect(textBtn, &QPushButton::clicked, this, [this]() { timeline->addOverlayAtPlayhead(3); });
    connect(blurBtn, &QPushButton::clicked, this, [this]() { timeline->addOverlayAtPlayhead(0); });
    connect(pixelBtn, &QPushButton::clicked, this, [this]() { timeline->addOverlayAtPlayhead(1); });
    connect(solidBtn, &QPushButton::clicked, this, [this]() { timeline->addOverlayAtPlayhead(2); });
    connect(videoWithCrop, &VideoWithCropWidget::overlayDropped, this, [this](int type) {
        timeline->addOverlayAtPlayhead(type);
    });
    connect(timeline, &TimelineWidget::overlaysChanged, this, &MainWindow::syncOverlaysToPreview);
    connect(timeline, &TimelineWidget::requestEditTextOverlay, this, &MainWindow::editTextOverlay);

    // Region edits on the preview write straight back to the overlay clips.
    connect(videoWithCrop, &VideoWithCropWidget::filtersChanged, this,
            [this](const QList<VideoWithCropWidget::FilterObject> &filters) {
        if (syncingPreview) return;
        for (int i = 0; i < filters.size() && i < previewOverlayMap.size(); ++i) {
            const int ovIdx = previewOverlayMap[i];
            if (ovIdx < 0 || ovIdx >= timeline->overlays.size()) continue;
            timeline->overlays[ovIdx].l = filters[i].l;
            timeline->overlays[ovIdx].t = filters[i].t;
            timeline->overlays[ovIdx].r = filters[i].r;
            timeline->overlays[ovIdx].b = filters[i].b;
        }
        timeline->update();
    });
    connect(videoWithCrop, &VideoWithCropWidget::filterSelectionChanged, this, [this](int idx) {
        if (syncingPreview) return;
        timeline->selectedOverlayIdx = (idx >= 0 && idx < previewOverlayMap.size()) ? previewOverlayMap[idx] : -1;
        timeline->update();
    });

    // --- Export progress rendered inside the editor ---
    connect(timeline, &TimelineWidget::exportStarted, this, [this](const QString &label) {
        statusLabel->setText(label);
        statusLabel->show();
        exportProgressBar->setValue(0);
        exportProgressBar->show();
        exportBtn->setEnabled(false);
    });
    connect(timeline, &TimelineWidget::exportProgress, this, [this](int percent) {
        exportProgressBar->setValue(percent);
    });
    connect(timeline, &TimelineWidget::exportFinished, this, [this](bool success, const QString &message) {
        exportProgressBar->hide();
        exportBtn->setEnabled(!currentMediaPath.isEmpty());
        statusLabel->setText(message);
        statusLabel->show();
        statusLabel->setProperty("state", success ? "ok" : "error");
        statusLabel->style()->unpolish(statusLabel);
        statusLabel->style()->polish(statusLabel);
        QTimer::singleShot(6000, statusLabel, [this]() {
            statusLabel->clear();
            statusLabel->hide();
        });
    });

    connect(player, &QMediaPlayer::durationChanged, [this](qint64 d) {
        if (d <= 0) return;
        // Source switches during multi-clip playback also fire durationChanged;
        // those must not reset the composition — just apply the seek that was
        // requested before the file finished loading.
        if (switchingSource) {
            switchingSource = false;
            if (pendingSeekLocalPos >= 0) {
                player->setPosition(pendingSeekLocalPos);
                pendingSeekLocalPos = -1;
            }
            return;
        }
        QApplication::processEvents();
        QTimer::singleShot(100, this, [this, d]() {
            timeline->setDuration(d);
            timeline->updateGeometry();
            timeline->forceFitToDuration();
            player->setPosition(0);
            player->play();
            refreshMediaState();
            timeline->update();
            updateTimecodeDisplay();
        });
    });

    connect(timeline, &TimelineWidget::requestAudioTrackChange, [this](int trackIndex) {
        player->setActiveAudioTrack(trackIndex);
        updateTimelineChips();
    });

    connect(timeline, &TimelineWidget::mediaProbingFinished, this, &MainWindow::refreshMediaState);

    connect(videoWithCrop, &VideoWithCropWidget::cropsChanged, timeline, &TimelineWidget::updateCropValues);
    connect(timeline, &TimelineWidget::visualStateChanged, this,
            [this](float t, float b, float l, float r) {
        videoWithCrop->cropT = t;
        videoWithCrop->cropB = b;
        videoWithCrop->cropL = l;
        videoWithCrop->cropR = r;
        videoWithCrop->triggerScale();
        videoWithCrop->update();
    });
    connect(timeline, &TimelineWidget::clipTrimmed, [this]() {
        float t, b, l, r;
        if (timeline->visualStateForCurrentContext(t, b, l, r)) {
            videoWithCrop->cropT = t; videoWithCrop->cropB = b;
            videoWithCrop->cropL = l; videoWithCrop->cropR = r;
        }
        videoWithCrop->triggerScale();
        videoWithCrop->update();
        updateTimecodeDisplay();
        updateTimelineChips();
    });

    connect(timeline, &TimelineWidget::playheadMoved, this, &MainWindow::seekTimeline);
    connect(autoCutBtn, &QPushButton::clicked, [this]() { timeline->autoCutSilence(); });
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
    connect(helpBtn, &QPushButton::clicked, this, &MainWindow::showShortcutsDialog);
    connect(sidebarImportBtn, &QPushButton::clicked, this, &MainWindow::importMedia);

    // Export menu
    connect(exportVideoAction, &QAction::triggered, this, [this]() { timeline->copyTrimmedVideo(); });
    connect(exportMutedAction, &QAction::triggered, this, [this]() { timeline->copyTrimmedVideoMuted(); });
    connect(exportAudioAction, &QAction::triggered, this, [this]() { timeline->copyTrimmedAudio(); });
    connect(exportGifAction, &QAction::triggered, this, [this]() { timeline->copyTrimmedGif(); });

    // Transport: jump / frame-step (in composition time)
    connect(jumpBackBtn, &QPushButton::clicked, this, [this]() {
        seekTimeline(qMax<qint64>(0, timeline->currentPosMs - timeline->getPlaybackSettings().majorSeekMs));
    });
    connect(jumpFwdBtn, &QPushButton::clicked, this, [this]() {
        seekTimeline(timeline->currentPosMs + timeline->getPlaybackSettings().majorSeekMs);
    });
    connect(stepBackBtn, &QPushButton::clicked, this, [this]() {
        player->pause();
        seekTimeline(qMax<qint64>(0, timeline->currentPosMs - timeline->getPlaybackSettings().minorSeekMs));
    });
    connect(stepFwdBtn, &QPushButton::clicked, this, [this]() {
        player->pause();
        seekTimeline(timeline->currentPosMs + timeline->getPlaybackSettings().minorSeekMs);
    });

    // Volume / speed / snapshot
    connect(muteBtn, &QPushButton::toggled, this, [this](bool muted) {
        audio->setMuted(muted);
        muteBtn->setIcon(muted ? volumeMutedIcon : volumeIcon);
        muteBtn->setToolTip(muted ? "Unmute" : "Mute");
    });
    connect(speedBox, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        QString v = text;
        v.chop(1);
        const qreal rate = v.toDouble();
        if (rate > 0.0) player->setPlaybackRate(rate);
    });
    connect(snapshotBtn, &QPushButton::clicked, this, &MainWindow::saveSnapshot);

    // Timeline edit buttons
    connect(undoBtn, &QPushButton::clicked, this, [this]() { timeline->undo(); });
    connect(redoBtn, &QPushButton::clicked, this, [this]() { timeline->redo(); });
    connect(splitBtn, &QPushButton::clicked, this, [this]() { timeline->requestSplit(); });
    connect(deleteClipBtn, &QPushButton::clicked, this, [this]() { timeline->deleteActiveSelection(); });
    connect(resetCropBtn, &QPushButton::clicked, [this]() {
        videoWithCrop->cropT = editorSettings.defaultCropTop; videoWithCrop->cropB = editorSettings.defaultCropBottom;
        videoWithCrop->cropL = editorSettings.defaultCropLeft; videoWithCrop->cropR = editorSettings.defaultCropRight;
        timeline->cropTop = editorSettings.defaultCropTop; timeline->cropBottom = editorSettings.defaultCropBottom;
        timeline->cropLeft = editorSettings.defaultCropLeft; timeline->cropRight = editorSettings.defaultCropRight;
        timeline->applyCurrentVisualsToSelection(true);
        videoWithCrop->triggerScale();
        videoWithCrop->update();
        timeline->update();
    });

    connect(player, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            // If appended clips continue past this file, move into them;
            // otherwise loop the composition.
            const auto &sources = timeline->sources;
            qint64 next = timeline->getStartLimit();
            if (activeSourceIdx >= 0 && activeSourceIdx < sources.size()) {
                const qint64 sourceEnd = sources[activeSourceIdx].offsetMs + sources[activeSourceIdx].durationMs;
                if (timeline->getEndLimit() > sourceEnd) next = sourceEnd;
            }
            seekTimeline(next);
            player->play();
        }
    });

    playPauseShortcut = new QShortcut(this);
    connect(playPauseShortcut, &QShortcut::activated, [this]() {
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });

    if (exportInput) {
        connect(exportInput, &QLineEdit::textChanged, [this](const QString &text) {
            timeline->customExportName = text.trimmed().replace(" ", "_");
        });
    }
}

void MainWindow::toggleVideoFullscreen() {
    if (!videoContainer || !stageColumnLayout) return;

    if (isVideoFullscreen) {
        restoreVideoFromFullscreen();
        return;
    }

    isVideoFullscreen = true;
    fullscreenBtn->setIcon(exitFullscreenIcon);
    fullscreenBtn->setToolTip("Exit fullscreen");

    videoFullscreenPlaceholder = new QWidget(workspace);
    videoFullscreenPlaceholder->setMinimumSize(videoContainer->size());
    const int videoIndex = stageColumnLayout->indexOf(videoContainer);
    if (videoIndex >= 0) {
        stageColumnLayout->insertWidget(videoIndex, videoFullscreenPlaceholder, 1);
    }

    stageColumnLayout->removeWidget(videoContainer);
    videoContainer->setParent(nullptr);

    videoFullscreenDialog = new QDialog(this, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    videoFullscreenDialog->setModal(false);
    videoFullscreenDialog->setObjectName("VideoFullscreenDialog");
    videoFullscreenDialog->setStyleSheet("QDialog#VideoFullscreenDialog { background-color: #000000; }");

    auto *layout = new QVBoxLayout(videoFullscreenDialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(videoContainer, 1);

    connect(videoFullscreenDialog, &QDialog::finished, this, [this](int) {
        if (isVideoFullscreen) restoreVideoFromFullscreen();
    });

    videoFullscreenDialog->showFullScreen();
    videoFullscreenDialog->raise();
    videoFullscreenDialog->activateWindow();
    
    // FIX: Ensure container is visible and updated even when paused
    videoContainer->show();
    videoContainer->update();
    videoContainer->setFocus();
}

void MainWindow::restoreVideoFromFullscreen() {
    if (!isVideoFullscreen || !stageColumnLayout || !videoContainer) return;

    isVideoFullscreen = false;
    fullscreenBtn->setIcon(fullscreenIcon);
    fullscreenBtn->setToolTip("Fullscreen preview");

    if (videoFullscreenDialog) {
        videoFullscreenDialog->blockSignals(true);
        videoFullscreenDialog->hide();
        videoFullscreenDialog->deleteLater();
        videoFullscreenDialog = nullptr;
    }

    videoContainer->setParent(nullptr);

    const int placeholderIndex = videoFullscreenPlaceholder ? stageColumnLayout->indexOf(videoFullscreenPlaceholder) : -1;
    if (placeholderIndex >= 0) {
        stageColumnLayout->insertWidget(placeholderIndex, videoContainer, 1);
        stageColumnLayout->removeWidget(videoFullscreenPlaceholder);
        videoFullscreenPlaceholder->deleteLater();
        videoFullscreenPlaceholder = nullptr;
    } else {
        stageColumnLayout->addWidget(videoContainer, 1);
    }
    
    videoContainer->setFocus();
    videoContainer->show();
    videoContainer->updateGeometry();
}

void MainWindow::handlePlaybackState(QMediaPlayer::PlaybackState state) {
    const bool playing = state == QMediaPlayer::PlayingState;
    playPauseBtn->setIcon(playing ? pauseIcon : playIcon);
    playPauseBtn->setToolTip(playing ? "Pause" : "Play");
}

void MainWindow::updateTimecodeDisplay() {
    if (!timecodeLabel) return;
    if (currentMediaPath.isEmpty()) {
        timecodeLabel->setText("00:00.000 / 00:00.000");
        return;
    }
    const qint64 totalMs = static_cast<qint64>(timeline->getTotalSegmentsDuration() * 1000.0);
    timecodeLabel->setText(QString("%1 / %2").arg(formatTimecode(timeline->currentPosMs), formatTimecode(totalMs)));
}

void MainWindow::updateTimelineChips() {
    if (!audioTrackChip || !estSizeChip) return;
    if (currentMediaPath.isEmpty()) {
        audioTrackChip->hide();
        estSizeChip->hide();
        return;
    }

    if (timeline->sourceHasAudio()) {
        audioTrackChip->setText(timeline->currentAudioTrackName().toUpper());
        audioTrackChip->setVisible(true);
    } else {
        audioTrackChip->hide();
    }

    const double mb = timeline->estimatedExportSizeMB();
    if (mb > 0.0) {
        estSizeChip->setText(QString("EST %1 MB").arg(mb, 0, 'f', mb < 10.0 ? 2 : 1));
        estSizeChip->setProperty("tone", mb <= 25.0 ? "good" : mb <= 100.0 ? "" : "warn");
        estSizeChip->style()->unpolish(estSizeChip);
        estSizeChip->style()->polish(estSizeChip);
        estSizeChip->setVisible(true);
    } else {
        estSizeChip->hide();
    }
}

void MainWindow::updateVolume() {
    // 1. Get the current clip-specific gain from the timeline
    float segmentGain = 1.0f;

    // We need a way to ask the timeline: "What is the gain at this exact millisecond?"
    // (composition time, so it also works while an appended clip is playing)
    segmentGain = timeline->getGainAtPos(timeline->currentPosMs);

    // 2. Multiply everything: Global Slider * Master Gain * Segment Gain
    float finalVol = (volSlider->value() / 100.0f) * timeline->audioGain * segmentGain;

    audio->setVolume(qBound(0.0f, finalVol, 1.0f));
}

void MainWindow::importMedia() {
    QString file = QFileDialog::getOpenFileName(this, "Import Media", "", MediaUtils::importDialogFilter());
    if (!file.isEmpty()) {
        loadClipDirectly(file);
    }
}

void MainWindow::loadClipDirectly(const QString &filePath) {
    if (!MediaUtils::isSupportedMediaFile(filePath)) {
        QMessageBox::warning(this, "Unsupported file",
                             "That file does not look like audio or video media.");
        return;
    }

    // Track in recents WITHOUT reshuffling the visible list: reordering while
    // the user is clicking around made the bin feel broken.
    if (!cachedRecentFiles.contains(filePath)) {
        cachedRecentFiles.prepend(filePath);
        const int recentLimit = qMax(1, editorSettings.recentMediaLimit);
        while (cachedRecentFiles.size() > recentLimit) cachedRecentFiles.removeLast();
    }

    currentMediaPath = filePath;
    activeSourceIdx = 0;
    switchingSource = false;
    pendingSeekLocalPos = -1;
    previewOverlayMap.clear();
    videoWithCrop->lastFrame = QImage();
    videoWithCrop->filterObjects.clear();
    videoWithCrop->selectedFilterIdx = -1;
    videoWithCrop->adjustingFilter = false;
    videoWithCrop->cropT = editorSettings.defaultCropTop;
    videoWithCrop->cropB = editorSettings.defaultCropBottom;
    videoWithCrop->cropL = editorSettings.defaultCropLeft;
    videoWithCrop->cropR = editorSettings.defaultCropRight;
    timeline->cropTop = editorSettings.defaultCropTop;
    timeline->cropBottom = editorSettings.defaultCropBottom;
    timeline->cropLeft = editorSettings.defaultCropLeft;
    timeline->cropRight = editorSettings.defaultCropRight;
    videoWithCrop->setPlaceholderState("Loading media...", "Preparing timeline, waveform, and preview.");
    videoWithCrop->update();

    speedBox->setCurrentText("1x");
    player->setPlaybackRate(1.0);
    player->setSource(QUrl::fromLocalFile(filePath));
    timeline->setMediaSource(QUrl::fromLocalFile(filePath));
    if (editorSettings.autoPlayOnImport) player->play();
    else player->pause();
    refreshMediaState();
}

void MainWindow::loadEditorSettings() {
    QSettings settings = makeAppSettings();
    auto uiValue = [&](const QString &key, const QVariant &fallback) {
        return settings.contains("ui/" + key) ? settings.value("ui/" + key, fallback)
                                              : settings.value("general/" + key, fallback);
    };
    // Theme version 2 = the revamped design system. Branding/palette values saved
    // by older builds are intentionally ignored so the new look applies once.
    const int themeVersion = settings.value("appearance/themeVersion", 1).toInt();
    editorSettings.autoPlayOnImport = uiValue("autoPlayOnImport", editorSettings.autoPlayOnImport).toBool();
    editorSettings.checkForUpdatesOnStartup = uiValue("checkForUpdatesOnStartup", editorSettings.checkForUpdatesOnStartup).toBool();
    editorSettings.defaultVolumePercent = uiValue("defaultVolumePercent", editorSettings.defaultVolumePercent).toInt();
    editorSettings.recentMediaLimit = uiValue("recentMediaLimit", editorSettings.recentMediaLimit).toInt();
    editorSettings.notificationDurationMs = uiValue("notificationDurationMs", editorSettings.notificationDurationMs).toInt();
    editorSettings.notificationPosition = uiValue("notificationPosition", editorSettings.notificationPosition).toString();
    editorSettings.updateCheckDelayMs = uiValue("updateCheckDelayMs", editorSettings.updateCheckDelayMs).toInt();
    if (themeVersion >= 2) {
        editorSettings.windowTitle = uiValue("windowTitle", editorSettings.windowTitle).toString();
        editorSettings.logoPrimaryText = uiValue("logoPrimaryText", editorSettings.logoPrimaryText).toString();
        editorSettings.logoSecondaryText = uiValue("logoSecondaryText", editorSettings.logoSecondaryText).toString();
        editorSettings.importButtonText = uiValue("importButtonText", editorSettings.importButtonText).toString();
    }
    editorSettings.sidebarWidth = uiValue("sidebarWidth", editorSettings.sidebarWidth).toInt();
    editorSettings.sidebarPosition = uiValue("sidebarPosition", editorSettings.sidebarPosition).toString();
    editorSettings.toolButtonOrder = uiValue("toolButtonOrder", editorSettings.toolButtonOrder).toString();
    editorSettings.defaultCropTop = settings.value("editing/defaultCropTop", editorSettings.defaultCropTop).toFloat();
    editorSettings.defaultCropBottom = settings.value("editing/defaultCropBottom", editorSettings.defaultCropBottom).toFloat();
    editorSettings.defaultCropLeft = settings.value("editing/defaultCropLeft", editorSettings.defaultCropLeft).toFloat();
    editorSettings.defaultCropRight = settings.value("editing/defaultCropRight", editorSettings.defaultCropRight).toFloat();
    editorSettings.previewPlaceholderTitle = settings.value("editing/previewPlaceholderTitle", editorSettings.previewPlaceholderTitle).toString();
    editorSettings.previewPlaceholderBody = settings.value("editing/previewPlaceholderBody", editorSettings.previewPlaceholderBody).toString();
    editorSettings.emptyTransportHint = settings.value("editing/emptyTransportHint", editorSettings.emptyTransportHint).toString();
    editorSettings.videoTransportHint = settings.value("editing/videoTransportHint", editorSettings.videoTransportHint).toString();
    editorSettings.audioTransportHint = settings.value("editing/audioTransportHint", editorSettings.audioTransportHint).toString();
    if (themeVersion >= 2) {
    editorSettings.timelineAccentColor = settings.value("appearance/timelineAccentColor", editorSettings.timelineAccentColor).toString();
    editorSettings.timelineSecondaryColor = settings.value("appearance/timelineSecondaryColor", editorSettings.timelineSecondaryColor).toString();
    editorSettings.timelineBackgroundColor = settings.value("appearance/timelineBackgroundColor", editorSettings.timelineBackgroundColor).toString();
    editorSettings.timelineTrackColor = settings.value("appearance/timelineTrackColor", editorSettings.timelineTrackColor).toString();
    editorSettings.timelineWaveformColor = settings.value("appearance/timelineWaveformColor", editorSettings.timelineWaveformColor).toString();
    editorSettings.previewAccentColor = settings.value("appearance/previewAccentColor", editorSettings.previewAccentColor).toString();
    editorSettings.previewSecondaryColor = settings.value("appearance/previewSecondaryColor", editorSettings.previewSecondaryColor).toString();
    editorSettings.previewBackgroundColor = settings.value("appearance/previewBackgroundColor", editorSettings.previewBackgroundColor).toString();
    editorSettings.appBackgroundStartColor = settings.value("appearance/appBackgroundStartColor", editorSettings.appBackgroundStartColor).toString();
    editorSettings.appBackgroundEndColor = settings.value("appearance/appBackgroundEndColor", editorSettings.appBackgroundEndColor).toString();
    editorSettings.panelSurfaceColor = settings.value("appearance/panelSurfaceColor", editorSettings.panelSurfaceColor).toString();
    editorSettings.panelAltSurfaceColor = settings.value("appearance/panelAltSurfaceColor", editorSettings.panelAltSurfaceColor).toString();
    editorSettings.controlSurfaceColor = settings.value("appearance/controlSurfaceColor", editorSettings.controlSurfaceColor).toString();
    editorSettings.controlHoverColor = settings.value("appearance/controlHoverColor", editorSettings.controlHoverColor).toString();
    editorSettings.borderColor = settings.value("appearance/borderColor", editorSettings.borderColor).toString();
    editorSettings.primaryTextColor = settings.value("appearance/primaryTextColor", editorSettings.primaryTextColor).toString();
    editorSettings.mutedTextColor = settings.value("appearance/mutedTextColor", editorSettings.mutedTextColor).toString();
    editorSettings.sectionLabelColor = settings.value("appearance/sectionLabelColor", editorSettings.sectionLabelColor).toString();
    editorSettings.logoPrimaryColor = settings.value("appearance/logoPrimaryColor", editorSettings.logoPrimaryColor).toString();
    editorSettings.logoSecondaryColor = settings.value("appearance/logoSecondaryColor", editorSettings.logoSecondaryColor).toString();
    editorSettings.appFontFamily = settings.value("appearance/appFontFamily", editorSettings.appFontFamily).toString();
    editorSettings.appFontPointSize = settings.value("appearance/appFontPointSize", editorSettings.appFontPointSize).toInt();
    editorSettings.logoFontPointSize = settings.value("appearance/logoFontPointSize", editorSettings.logoFontPointSize).toInt();
    editorSettings.mediaBadgeFontPointSize = settings.value("appearance/mediaBadgeFontPointSize", editorSettings.mediaBadgeFontPointSize).toInt();
    editorSettings.metaFontPointSize = settings.value("appearance/metaFontPointSize", editorSettings.metaFontPointSize).toInt();
    editorSettings.panelCornerRadius = settings.value("appearance/panelCornerRadius", editorSettings.panelCornerRadius).toInt();
    editorSettings.buttonCornerRadius = settings.value("appearance/buttonCornerRadius", editorSettings.buttonCornerRadius).toInt();
    }
    editorSettings.keyPlayPause = settings.value("keybinds/playPause", editorSettings.keyPlayPause).toString();
    editorSettings.keySplit = settings.value("keybinds/split", editorSettings.keySplit).toString();
    editorSettings.keyDeleteClip = settings.value("keybinds/deleteClip", editorSettings.keyDeleteClip).toString();
    editorSettings.keyReplay = settings.value("keybinds/replay", editorSettings.keyReplay).toString();
    editorSettings.keyForward = settings.value("keybinds/forward", editorSettings.keyForward).toString();
    editorSettings.keyStepBack = settings.value("keybinds/stepBack", editorSettings.keyStepBack).toString();
    editorSettings.keyStepForward = settings.value("keybinds/stepForward", editorSettings.keyStepForward).toString();
    editorSettings.keyUndo = settings.value("keybinds/undo", editorSettings.keyUndo).toString();
    editorSettings.keyRedo = settings.value("keybinds/redo", editorSettings.keyRedo).toString();
    editorSettings.keyExportGif = settings.value("keybinds/exportGif", editorSettings.keyExportGif).toString();
    editorSettings.keyExportAudio = settings.value("keybinds/exportAudio", editorSettings.keyExportAudio).toString();
    editorSettings.keyExportVideo = settings.value("keybinds/exportVideo", editorSettings.keyExportVideo).toString();
    editorSettings.keyExportMutedVideo = settings.value("keybinds/exportMutedVideo", editorSettings.keyExportMutedVideo).toString();
    editorSettings.keyCycleAudioTrack = settings.value("keybinds/cycleAudioTrack", editorSettings.keyCycleAudioTrack).toString();
    editorSettings.autoLoadDirectories = settings.value("ui/autoLoadDirectories").toStringList();

    TimelineWidget::PlaybackSettings playback = timeline->getPlaybackSettings();
    playback.majorSeekMs = settings.value("editing/majorSeekMs", playback.majorSeekMs).toInt();
    playback.minorSeekMs = settings.value("editing/minorSeekMs", playback.minorSeekMs).toInt();
    playback.splitGuardMs = settings.value("editing/splitGuardMs", playback.splitGuardMs).toInt();
    playback.minSegmentDurationMs = settings.value("editing/minSegmentDurationMs", playback.minSegmentDurationMs).toInt();
    timeline->setPlaybackSettings(playback);

    TimelineWidget::AutoCutSettings autoCut = timeline->getAutoCutSettings();
    autoCut.silenceThresholdDb = settings.value("autoCut/silenceThresholdDb", autoCut.silenceThresholdDb).toDouble();
    autoCut.minimumSilenceDurationSec = settings.value("autoCut/minimumSilenceDurationSec", autoCut.minimumSilenceDurationSec).toDouble();
    autoCut.paddingSec = settings.value("autoCut/paddingSec", autoCut.paddingSec).toDouble();
    autoCut.minimumClipDurationSec = settings.value("autoCut/minimumClipDurationSec", autoCut.minimumClipDurationSec).toDouble();
    timeline->setAutoCutSettings(autoCut);

    TimelineWidget::ExportSettings exportSettings = timeline->getExportSettings();
    exportSettings.exportDirectory = settings.value("export/exportDirectory", defaultExportDirectory()).toString();
    exportSettings.gifFps = settings.value("export/gifFps", exportSettings.gifFps).toInt();
    exportSettings.gifWidth = settings.value("export/gifWidth", exportSettings.gifWidth).toInt();
    exportSettings.audioBitrateKbps = settings.value("export/audioBitrateKbps", exportSettings.audioBitrateKbps).toInt();
    exportSettings.compressedAudioBitrateKbps = settings.value("export/compressedAudioBitrateKbps", exportSettings.compressedAudioBitrateKbps).toInt();
    exportSettings.videoCompressionThresholdMB = settings.value("export/videoCompressionThresholdMB", exportSettings.videoCompressionThresholdMB).toDouble();
    exportSettings.targetCompressedSizeMB = settings.value("export/targetCompressedSizeMB", exportSettings.targetCompressedSizeMB).toDouble();
    exportSettings.fileNamePrefix = settings.value("export/fileNamePrefix", exportSettings.fileNamePrefix).toString();
    exportSettings.includeSourceNameInExport = settings.value("export/includeSourceNameInExport", exportSettings.includeSourceNameInExport).toBool();
    timeline->setExportSettings(exportSettings);

    if (settings.contains("window/geometry")) {
        restoreGeometry(settings.value("window/geometry").toByteArray());
    }
    applyEditorSettings();
}

void MainWindow::saveEditorSettings() const {
    QSettings settings = makeAppSettings();
    settings.remove("general");
    settings.setValue("appearance/themeVersion", 2);
    settings.setValue("ui/autoPlayOnImport", editorSettings.autoPlayOnImport);
    settings.setValue("ui/checkForUpdatesOnStartup", editorSettings.checkForUpdatesOnStartup);
    settings.setValue("ui/defaultVolumePercent", editorSettings.defaultVolumePercent);
    settings.setValue("ui/recentMediaLimit", editorSettings.recentMediaLimit);
    settings.setValue("ui/notificationDurationMs", editorSettings.notificationDurationMs);
    settings.setValue("ui/notificationPosition", editorSettings.notificationPosition);
    settings.setValue("ui/updateCheckDelayMs", editorSettings.updateCheckDelayMs);
    settings.setValue("ui/windowTitle", editorSettings.windowTitle);
    settings.setValue("ui/logoPrimaryText", editorSettings.logoPrimaryText);
    settings.setValue("ui/logoSecondaryText", editorSettings.logoSecondaryText);
    settings.setValue("ui/importButtonText", editorSettings.importButtonText);
    settings.setValue("ui/sidebarWidth", editorSettings.sidebarWidth);
    settings.setValue("ui/sidebarPosition", editorSettings.sidebarPosition);
    settings.setValue("ui/toolButtonOrder", editorSettings.toolButtonOrder);
    settings.setValue("editing/defaultCropTop", editorSettings.defaultCropTop);
    settings.setValue("editing/defaultCropBottom", editorSettings.defaultCropBottom);
    settings.setValue("editing/defaultCropLeft", editorSettings.defaultCropLeft);
    settings.setValue("editing/defaultCropRight", editorSettings.defaultCropRight);
    settings.setValue("editing/previewPlaceholderTitle", editorSettings.previewPlaceholderTitle);
    settings.setValue("editing/previewPlaceholderBody", editorSettings.previewPlaceholderBody);
    settings.setValue("editing/emptyTransportHint", editorSettings.emptyTransportHint);
    settings.setValue("editing/videoTransportHint", editorSettings.videoTransportHint);
    settings.setValue("editing/audioTransportHint", editorSettings.audioTransportHint);
    settings.setValue("appearance/timelineAccentColor", editorSettings.timelineAccentColor);
    settings.setValue("appearance/timelineSecondaryColor", editorSettings.timelineSecondaryColor);
    settings.setValue("appearance/timelineBackgroundColor", editorSettings.timelineBackgroundColor);
    settings.setValue("appearance/timelineTrackColor", editorSettings.timelineTrackColor);
    settings.setValue("appearance/timelineWaveformColor", editorSettings.timelineWaveformColor);
    settings.setValue("appearance/previewAccentColor", editorSettings.previewAccentColor);
    settings.setValue("appearance/previewSecondaryColor", editorSettings.previewSecondaryColor);
    settings.setValue("appearance/previewBackgroundColor", editorSettings.previewBackgroundColor);
    settings.setValue("appearance/appBackgroundStartColor", editorSettings.appBackgroundStartColor);
    settings.setValue("appearance/appBackgroundEndColor", editorSettings.appBackgroundEndColor);
    settings.setValue("appearance/panelSurfaceColor", editorSettings.panelSurfaceColor);
    settings.setValue("appearance/panelAltSurfaceColor", editorSettings.panelAltSurfaceColor);
    settings.setValue("appearance/controlSurfaceColor", editorSettings.controlSurfaceColor);
    settings.setValue("appearance/controlHoverColor", editorSettings.controlHoverColor);
    settings.setValue("appearance/borderColor", editorSettings.borderColor);
    settings.setValue("appearance/primaryTextColor", editorSettings.primaryTextColor);
    settings.setValue("appearance/mutedTextColor", editorSettings.mutedTextColor);
    settings.setValue("appearance/sectionLabelColor", editorSettings.sectionLabelColor);
    settings.setValue("appearance/logoPrimaryColor", editorSettings.logoPrimaryColor);
    settings.setValue("appearance/logoSecondaryColor", editorSettings.logoSecondaryColor);
    settings.setValue("appearance/appFontFamily", editorSettings.appFontFamily);
    settings.setValue("appearance/appFontPointSize", editorSettings.appFontPointSize);
    settings.setValue("appearance/logoFontPointSize", editorSettings.logoFontPointSize);
    settings.setValue("appearance/mediaBadgeFontPointSize", editorSettings.mediaBadgeFontPointSize);
    settings.setValue("appearance/metaFontPointSize", editorSettings.metaFontPointSize);
    settings.setValue("appearance/panelCornerRadius", editorSettings.panelCornerRadius);
    settings.setValue("appearance/buttonCornerRadius", editorSettings.buttonCornerRadius);
    settings.setValue("keybinds/playPause", editorSettings.keyPlayPause);
    settings.setValue("keybinds/split", editorSettings.keySplit);
    settings.setValue("keybinds/deleteClip", editorSettings.keyDeleteClip);
    settings.setValue("keybinds/replay", editorSettings.keyReplay);
    settings.setValue("keybinds/forward", editorSettings.keyForward);
    settings.setValue("keybinds/stepBack", editorSettings.keyStepBack);
    settings.setValue("keybinds/stepForward", editorSettings.keyStepForward);
    settings.setValue("keybinds/undo", editorSettings.keyUndo);
    settings.setValue("keybinds/redo", editorSettings.keyRedo);
    settings.setValue("keybinds/exportGif", editorSettings.keyExportGif);
    settings.setValue("keybinds/exportAudio", editorSettings.keyExportAudio);
    settings.setValue("keybinds/exportVideo", editorSettings.keyExportVideo);
    settings.setValue("keybinds/exportMutedVideo", editorSettings.keyExportMutedVideo);
    settings.setValue("keybinds/cycleAudioTrack", editorSettings.keyCycleAudioTrack);
    settings.setValue("ui/autoLoadDirectories", editorSettings.autoLoadDirectories);

    const auto playback = timeline->getPlaybackSettings();
    settings.setValue("editing/majorSeekMs", playback.majorSeekMs);
    settings.setValue("editing/minorSeekMs", playback.minorSeekMs);
    settings.setValue("editing/splitGuardMs", playback.splitGuardMs);
    settings.setValue("editing/minSegmentDurationMs", playback.minSegmentDurationMs);

    const auto autoCut = timeline->getAutoCutSettings();
    settings.setValue("autoCut/silenceThresholdDb", autoCut.silenceThresholdDb);
    settings.setValue("autoCut/minimumSilenceDurationSec", autoCut.minimumSilenceDurationSec);
    settings.setValue("autoCut/paddingSec", autoCut.paddingSec);
    settings.setValue("autoCut/minimumClipDurationSec", autoCut.minimumClipDurationSec);

    const auto exportSettings = timeline->getExportSettings();
    settings.setValue("export/exportDirectory", exportSettings.exportDirectory);
    settings.setValue("export/gifFps", exportSettings.gifFps);
    settings.setValue("export/gifWidth", exportSettings.gifWidth);
    settings.setValue("export/audioBitrateKbps", exportSettings.audioBitrateKbps);
    settings.setValue("export/compressedAudioBitrateKbps", exportSettings.compressedAudioBitrateKbps);
    settings.setValue("export/videoCompressionThresholdMB", exportSettings.videoCompressionThresholdMB);
    settings.setValue("export/targetCompressedSizeMB", exportSettings.targetCompressedSizeMB);
    settings.setValue("export/fileNamePrefix", exportSettings.fileNamePrefix);
    settings.setValue("export/includeSourceNameInExport", exportSettings.includeSourceNameInExport);
    settings.setValue("window/geometry", saveGeometry());
    settings.sync();
}

void MainWindow::applyEditorSettings() {
    setWindowTitle(editorSettings.windowTitle);
    if (logoBoldLabel) logoBoldLabel->setText(editorSettings.logoPrimaryText);
    if (logoLightLabel) logoLightLabel->setText(editorSettings.logoSecondaryText);
    if (importBtn) importBtn->setText(editorSettings.importButtonText);
    if (clipSidebar) clipSidebar->setFixedWidth(qMax(180, editorSettings.sidebarWidth));
    applyToolButtonOrder();
    applyIcons();
    if (playPauseShortcut) playPauseShortcut->setKey(QKeySequence::fromString(editorSettings.keyPlayPause, QKeySequence::PortableText));
    QFont appFont = qApp->font();
    appFont.setFamily(editorSettings.appFontFamily);
    appFont.setPointSize(qMax(8, editorSettings.appFontPointSize));
    qApp->setFont(appFont);
    qApp->setStyleSheet(buildAppStyleSheet());
    volSlider->setValue(qBound(0, editorSettings.defaultVolumePercent, 100));
    audio->setVolume(qBound(0.0, editorSettings.defaultVolumePercent / 100.0, 1.0));
    videoWithCrop->cropT = editorSettings.defaultCropTop;
    videoWithCrop->cropB = editorSettings.defaultCropBottom;
    videoWithCrop->cropL = editorSettings.defaultCropLeft;
    videoWithCrop->cropR = editorSettings.defaultCropRight;
    videoWithCrop->m_accentColor = QColor(editorSettings.previewAccentColor);
    videoWithCrop->m_secondaryColor = QColor(editorSettings.previewSecondaryColor);
    videoWithCrop->m_backgroundColor = QColor(editorSettings.previewBackgroundColor);
    videoWithCrop->setPlaceholderState(editorSettings.previewPlaceholderTitle, editorSettings.previewPlaceholderBody);
    timeline->cropTop = editorSettings.defaultCropTop;
    timeline->cropBottom = editorSettings.defaultCropBottom;
    timeline->cropLeft = editorSettings.defaultCropLeft;
    timeline->cropRight = editorSettings.defaultCropRight;
    timeline->m_accentColor = QColor(editorSettings.timelineAccentColor);
    timeline->m_secondaryColor = QColor(editorSettings.timelineSecondaryColor);
    timeline->m_backgroundColor = QColor(editorSettings.timelineBackgroundColor);
    timeline->m_trackColor = QColor(editorSettings.timelineTrackColor);
    timeline->m_waveformColor = QColor(editorSettings.timelineWaveformColor);

    cachedRecentFiles.clear();

    if (topPaneSplitter && clipSidebar && timelineTools && workspace) {
        if (editorSettings.sidebarPosition == "right") {
            topPaneSplitter->insertWidget(0, workspace);
            topPaneSplitter->insertWidget(1, timelineTools);
            topPaneSplitter->insertWidget(2, clipSidebar);
        } else {
            topPaneSplitter->insertWidget(0, clipSidebar);
            topPaneSplitter->insertWidget(1, workspace);
            topPaneSplitter->insertWidget(2, timelineTools);
        }
    }

    timeline->update();
    videoWithCrop->update();
    refreshMediaState();
}

QString MainWindow::buildAppStyleSheet() const {
    const QString base = qApp->property("baseStyleSheet").toString();
    const QString accent = withFallbackColor(editorSettings.timelineAccentColor, "#FF875F");
    const QString secondary = withFallbackColor(editorSettings.timelineSecondaryColor, "#FF6B4A");
    const QString timelineBg = withFallbackColor(editorSettings.timelineBackgroundColor, "#14181D");
    const QString track = withFallbackColor(editorSettings.timelineTrackColor, "#272C34");
    const QString waveform = withFallbackColor(editorSettings.timelineWaveformColor, "#7D5F56");
    const QString previewAccent = withFallbackColor(editorSettings.previewAccentColor, "#FF875F");
    const QString previewSecondary = withFallbackColor(editorSettings.previewSecondaryColor, "#FF6B4A");
    const QString previewBg = withFallbackColor(editorSettings.previewBackgroundColor, "#0F1115");
    const QString appBackgroundStart = withFallbackColor(editorSettings.appBackgroundStartColor, "#111317");
    const QString appBackgroundEnd = withFallbackColor(editorSettings.appBackgroundEndColor, "#0F1115");
    const QString panelSurface = withFallbackColor(editorSettings.panelSurfaceColor, "#1A1D23");
    const QString panelAltSurface = withFallbackColor(editorSettings.panelAltSurfaceColor, "#1D2128");
    const QString controlSurface = withFallbackColor(editorSettings.controlSurfaceColor, "#22272F");
    const QString controlHover = withFallbackColor(editorSettings.controlHoverColor, "#2D333D");
    const QString border = withFallbackColor(editorSettings.borderColor, "#2A3038");
    const QString primaryText = withFallbackColor(editorSettings.primaryTextColor, "#F1F4F7");
    const QString mutedText = withFallbackColor(editorSettings.mutedTextColor, "#C6CDD5");
    const QString sectionLabel = withFallbackColor(editorSettings.sectionLabelColor, "#D6DCE3");
    const QString logoPrimary = withFallbackColor(editorSettings.logoPrimaryColor, "#F4F6F8");
    const QString logoSecondary = withFallbackColor(editorSettings.logoSecondaryColor, "#FF9B77");
    const int fontSize = qMax(8, editorSettings.appFontPointSize);
    const int logoFontSize = qMax(10, editorSettings.logoFontPointSize);
    const int badgeFontSize = qMax(7, editorSettings.mediaBadgeFontPointSize);
    const int metaFontSize = qMax(7, editorSettings.metaFontPointSize);
    const int panelRadius = qMax(0, editorSettings.panelCornerRadius);
    const int buttonRadius = qMax(0, editorSettings.buttonCornerRadius);
    const int badgeRadius = qMax(0, panelRadius - 1);
    const int bigButtonRadius = buttonRadius + 1;
    const QColor accentColor(accent);
    const QColor panelSurfaceColor(panelSurface);
    const QColor panelAltSurfaceColor(panelAltSurface);
    const QColor controlSurfaceColor(controlSurface);
    const QColor controlHoverColor(controlHover);
    const QColor borderColor(border);
    auto withAlpha = [](QColor color, int alpha) {
        color.setAlpha(alpha);
        return color.name(QColor::HexArgb);
    };
    const QString frameBorder = withAlpha(borderColor, 60);
    const QString subtleBorder = withAlpha(borderColor, 40);
    const QString chipBg = withAlpha(accentColor, 22);
    const QString chipBorder = withAlpha(accentColor, 58);
    const QString accentSoft = withAlpha(accentColor, 40);
    const QString accentGlow = withAlpha(accentColor, 90);
    const QString hoverWash = withAlpha(QColor("#FFFFFF"), 14);
    const QString chipText = QColor(previewAccent).lighter(135).name(QColor::HexRgb);
    const QString accentHover = accentColor.lighter(112).name(QColor::HexRgb);
    const QString accentPressed = accentColor.darker(112).name(QColor::HexRgb);
    // Text drawn on top of the accent color: dark for bright accents, light otherwise.
    const QString onAccent = accentColor.lightness() > 110 ? "#1C0E08" : "#FFFFFF";

    QString sheet = QString(R"(
QWidget { font-family: "@font"; font-size: @fontSizept; color: @text; }
QMainWindow#MainCanvas, QWidget#centralWidget { background: @bgStart; }
QDialog, QMessageBox { background: @bgEnd; }

QFrame#toolbar {
    background: @panel;
    border: none;
    border-bottom: 1px solid rgba(0, 0, 0, 0.65);
    border-radius: 0;
}
QFrame#workspace, QFrame#footer, QFrame#clipSidebar, QFrame#PanelHeader,
QWidget#timelineTools, QFrame#ActionStrip, QFrame#TimelineShell {
    background: @panel;
    border: 1px solid @subtleBorder;
    border-radius: @panelRadiuspx;
}
QFrame#workspace { background: @panelAlt; }
QFrame#TransportBar {
    background: @panelAlt;
    border: 1px solid @subtleBorder;
    border-radius: @panelRadiuspx;
}
QFrame#VideoContainer { background: @previewBg; border: 1px solid @subtleBorder; border-radius: @panelRadiuspx; }
VideoWithCropWidget#VideoSurface { background-color: @previewBg; }

QLabel#LogoBold { color: @logo1; font-size: @logoFontSizept; }
QLabel#LogoLight { color: @logo2; font-size: @logoFontSizept; }
QLabel#SectionHeader { color: @section; }
QLabel#SectionLabel, QLabel#InputLabel, QLabel#HeroEyebrow, QLabel#InspectorGroupLabel { color: @section; }
QLabel#MetaData, QLabel#SubtleHint, QLabel#VersionLabel, QLabel#EmptyStateLabel, QLabel#HeroBody { color: @muted; font-size: @metaFontSizept; }
QLabel#SidebarTitle { color: @text; }
QLabel#SidebarCountLabel { color: @section; }
QLabel#StatusLabel { color: @accent; }
QDialog QLabel { color: @text; }
QFormLayout QLabel { color: @muted; }

QLabel#TimecodeLabel {
    background: @timelineBg;
    border: 1px solid @subtleBorder;
    border-radius: @btnRadiuspx;
    color: @text;
}
QLabel#CurrentMediaPill, QLabel#MiniBadge {
    background: @chipBg;
    border: 1px solid @chipBorder;
    color: @chipText;
    font-size: @badgeFontSizept;
}

QPushButton { color: @text; }
QPushButton#ActionBtn { background: @accent; border: none; }
QPushButton#ActionBtn:hover { background: @accentHover; }
QPushButton#ActionBtn:pressed { background: @accentPressed; }
QPushButton#ActionBtn:disabled { background: @control; }

QPushButton#ExportBtn {
    background: @accent;
    color: @onAccent;
    border: none;
    border-radius: @btnRadiuspx;
}
QPushButton#ExportBtn:hover { background: @accentHover; }
QPushButton#ExportBtn:pressed { background: @accentPressed; }
QPushButton#ExportBtn:disabled { background: @control; color: @muted; }

QPushButton[class="IconBtn"] { background: transparent; border: 1px solid transparent; border-radius: @btnRadiuspx; }
QPushButton[class="IconBtn"]:hover { background: @hoverWash; border-color: @subtleBorder; }
QPushButton[class="IconBtn"]:checked { background: @chipBg; border-color: @accentSoft; }

QPushButton#PrimaryGhostBtn, QPushButton[class="ToolBtn"], QPushButton#FullscreenBtn,
QDialogButtonBox QPushButton, QComboBox, QAbstractSpinBox, QLineEdit, QKeySequenceEdit, QFontComboBox {
    background: @control;
    border: 1px solid @subtleBorder;
    border-radius: @btnRadiuspx;
    color: @text;
    selection-background-color: @accentSoft;
}
QPushButton#PrimaryGhostBtn:hover, QPushButton[class="ToolBtn"]:hover, QPushButton#FullscreenBtn:hover,
QDialogButtonBox QPushButton:hover, QComboBox:hover, QAbstractSpinBox:hover, QLineEdit:hover,
QKeySequenceEdit:hover, QFontComboBox:hover {
    background: @controlHover;
    border-color: @accentSoft;
}
QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus, QKeySequenceEdit:focus, QFontComboBox:focus,
QPushButton#PrimaryGhostBtn:focus, QPushButton[class="ToolBtn"]:focus { border-color: @accentGlow; }
QLineEdit#ExportNameInput { background: @panelAlt; }

QComboBox QAbstractItemView {
    background: @panelAlt;
    border: 1px solid @frameBorder;
    border-radius: @panelRadiuspx;
    selection-background-color: @accentSoft;
}

QMenu { background: @panelAlt; border: 1px solid @frameBorder; border-radius: @panelRadiuspx; }
QMenu::item { border-radius: @btnRadiuspx; }
QMenu::item { color: @text; }
QMenu::item:selected { background: @chipBg; }
QMenu::separator { background: @frameBorder; }

QTabWidget::pane { background: @panel; border: 1px solid @subtleBorder; border-radius: @panelRadiuspx; }
QDialog#SettingsDialog QTabWidget::pane { background: @panel; }
QTabBar::tab { color: @muted; }
QTabBar::tab:selected { background: @control; color: @chipText; border-color: @accentSoft; }
QTabBar::tab:hover:!selected { color: @text; }

QCheckBox { color: @text; }
QCheckBox::indicator { background: @control; border: 1px solid @frameBorder; }
QCheckBox::indicator:hover { border-color: @accentGlow; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; }

QSlider::sub-page:horizontal { background: @accent; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: @accentSoft; }

QWidget#SidebarItem { background: @panelAlt; border: 1px solid @subtleBorder; border-radius: @btnRadiuspx; }
QWidget#SidebarItem[active="true"] { background: @controlHover; border: 1px solid @accentGlow; }
QWidget#SidebarItem:hover { background: @controlHover; border: 1px solid @accentSoft; }
PreviewLabel#SidebarPreview { background: @previewBg; border: 1px solid @subtleBorder; }

QWidget#ProgressNotificationBg { background: @panelAlt; border: 1px solid @accentSoft; }
QProgressBar#ProgressNotificationBar { background: @timelineBg; color: @text; }
QProgressBar#ProgressNotificationBar::chunk { background: @accent; }
QWidget#ToastNotification QLabel#ToastLabel { background: @accent; color: @onAccent; border-radius: @btnRadiuspx; }
QProgressBar#ExportProgressBar { background: @timelineBg; border: 1px solid @subtleBorder; color: @text; border-radius: @btnRadiuspx; }
QProgressBar#ExportProgressBar::chunk { background: @accent; }

QListWidget#SettingsNav { background: @panel; border: 1px solid @subtleBorder; }
QListWidget#SettingsNav::item { color: @muted; }
QListWidget#SettingsNav::item:hover { background: @hoverWash; color: @text; }
QListWidget#SettingsNav::item:selected { background: @chipBg; color: @chipText; }
QLabel#SettingsPageTitle { color: @section; }

QMenu#TimelineContextMenu { background: @panelAlt; border: 1px solid @frameBorder; padding: 4px; }
QMenu#TimelineContextMenu::item { padding: 6px 22px; border-radius: @btnRadiuspx; }
QMenu#TimelineContextMenu::item:selected { background: @chipBg; color: @text; }

TimelineWidget {
    qproperty-accentColor: @accent;
    qproperty-secondaryColor: @secondary;
    qproperty-backgroundColor: @timelineBg;
    qproperty-trackColor: @track;
    qproperty-waveformColor: @waveform;
}
TimelineWidget:focus { border: 1px solid @accentSoft; border-radius: @panelRadiuspx; }
VideoWithCropWidget {
    qproperty-accentColor: @previewAccent;
    qproperty-secondaryColor: @previewSecondary;
    qproperty-backgroundColor: @previewBg;
}
)");

    const QList<QPair<QString, QString>> tokens = {
        {"@fontSize", QString::number(fontSize)},
        {"@font", editorSettings.appFontFamily},
        {"@logoFontSize", QString::number(logoFontSize)},
        {"@badgeFontSize", QString::number(badgeFontSize)},
        {"@metaFontSize", QString::number(metaFontSize)},
        {"@panelRadius", QString::number(panelRadius)},
        {"@btnRadius", QString::number(buttonRadius)},
        {"@accentHover", accentHover},
        {"@accentPressed", accentPressed},
        {"@accentSoft", accentSoft},
        {"@accentGlow", accentGlow},
        {"@accent", accent},
        {"@secondary", secondary},
        {"@onAccent", onAccent},
        {"@timelineBg", timelineBg},
        {"@track", track},
        {"@waveform", waveform},
        {"@previewAccent", previewAccent},
        {"@previewSecondary", previewSecondary},
        {"@previewBg", previewBg},
        {"@bgStart", appBackgroundStart},
        {"@bgEnd", appBackgroundEnd},
        {"@panelAlt", panelAltSurface},
        {"@panel", panelSurface},
        {"@controlHover", controlHover},
        {"@control", controlSurface},
        {"@frameBorder", frameBorder},
        {"@subtleBorder", subtleBorder},
        {"@chipBg", chipBg},
        {"@chipBorder", chipBorder},
        {"@chipText", chipText},
        {"@hoverWash", hoverWash},
        {"@text", primaryText},
        {"@muted", mutedText},
        {"@section", sectionLabel},
        {"@logo1", logoPrimary},
        {"@logo2", logoSecondary},
    };
    for (const auto &token : tokens) {
        sheet.replace(token.first, token.second);
    }
    return base + sheet;
}

void MainWindow::applyToolButtonOrder() {
    if (!timelineToolsLayout || !toolHeaderLabel) return;

    while (QLayoutItem *item = timelineToolsLayout->takeAt(0)) {
        delete item;
    }

    if (!toolHeaderLabel->text().trimmed().isEmpty()) {
        timelineToolsLayout->addWidget(toolHeaderLabel);
    }
    QStringList order = editorSettings.toolButtonOrder.split(',', Qt::SkipEmptyParts);
    if (!order.contains("text")) order.prepend("text"); // configs saved before text overlays existed
    const QList<QPair<QString, QWidget*>> redactButtons = {
        {"text", textBtn},
        {"blur", blurBtn},
        {"pixel", pixelBtn},
        {"blackout", solidBtn}
    };
    const QList<QPair<QString, QWidget*>> actionButtons = {
        {"autocut", autoCutBtn},
        {"resetcrop", resetCropBtn}
    };

    bool redactGroupShown = false;
    bool actionsGroupShown = false;
    for (const QString &rawId : order) {
        const QString id = rawId.trimmed().toLower();
        for (const auto &pair : redactButtons) {
            if (pair.first == id && pair.second) {
                if (!redactGroupShown && redactGroupLabel) {
                    timelineToolsLayout->addWidget(redactGroupLabel);
                    redactGroupShown = true;
                }
                timelineToolsLayout->addWidget(pair.second);
            }
        }
        for (const auto &pair : actionButtons) {
            if (pair.first == id && pair.second) {
                if (!actionsGroupShown && actionsGroupLabel) {
                    if (redactGroupShown) timelineToolsLayout->addSpacing(10);
                    timelineToolsLayout->addWidget(actionsGroupLabel);
                    actionsGroupShown = true;
                }
                timelineToolsLayout->addWidget(pair.second);
            }
        }
    }
    timelineToolsLayout->addStretch();
}

void MainWindow::applyIcons() {
    const QColor iconColor(withFallbackColor(editorSettings.primaryTextColor, "#EFEFF4"));
    const QColor mutedColor(withFallbackColor(editorSettings.mutedTextColor, "#9C9CA8"));
    const QColor onAccent("#1C0E08");

    playIcon = Icons::play(onAccent);
    pauseIcon = Icons::pause(onAccent);
    volumeIcon = Icons::volume(iconColor);
    volumeMutedIcon = Icons::volumeMuted(iconColor);
    fullscreenIcon = Icons::fullscreen(iconColor);
    exitFullscreenIcon = Icons::exitFullscreen(iconColor);

    playPauseBtn->setIcon(player && player->playbackState() == QMediaPlayer::PlayingState ? pauseIcon : playIcon);
    jumpBackBtn->setIcon(Icons::jumpBack(iconColor));
    stepBackBtn->setIcon(Icons::stepBack(iconColor));
    stepFwdBtn->setIcon(Icons::stepForward(iconColor));
    jumpFwdBtn->setIcon(Icons::jumpForward(iconColor));
    muteBtn->setIcon(muteBtn->isChecked() ? volumeMutedIcon : volumeIcon);
    snapshotBtn->setIcon(Icons::snapshot(iconColor));
    fullscreenBtn->setIcon(isVideoFullscreen ? exitFullscreenIcon : fullscreenIcon);
    helpBtn->setIcon(Icons::help(mutedColor));
    settingsBtn->setIcon(Icons::gear(mutedColor));
    sidebarImportBtn->setIcon(Icons::importMedia(mutedColor));
    undoBtn->setIcon(Icons::undo(iconColor));
    redoBtn->setIcon(Icons::redo(iconColor));
    splitBtn->setIcon(Icons::split(iconColor));
    deleteClipBtn->setIcon(Icons::trash(iconColor));
    exportBtn->setIcon(Icons::exportMedia(onAccent));
    exportBtn->setIconSize(QSize(14, 14));

    const QSize small(15, 15);
    for (QPushButton *btn : {jumpBackBtn, stepBackBtn, stepFwdBtn, jumpFwdBtn,
                             muteBtn, snapshotBtn, fullscreenBtn, helpBtn, settingsBtn,
                             undoBtn, redoBtn, splitBtn, deleteClipBtn}) {
        btn->setIconSize(QSize(16, 16));
    }
    sidebarImportBtn->setIconSize(QSize(13, 13));
    muteBtn->setIconSize(small);
    snapshotBtn->setIconSize(small);
    fullscreenBtn->setIconSize(small);
}

void MainWindow::saveSnapshot() {
    if (videoWithCrop->lastFrame.isNull()) {
        TimelineWidget::showNotification("NO FRAME TO SAVE");
        return;
    }

    const auto exportSettings = timeline->getExportSettings();
    QString dir = exportSettings.exportDirectory;
    if (dir.isEmpty()) dir = defaultExportDirectory();
    QDir().mkpath(dir);

    const QString base = exportInput && !exportInput->text().trimmed().isEmpty()
        ? exportInput->text().trimmed().replace(' ', '_')
        : QFileInfo(currentMediaPath).completeBaseName();
    const QString path = QString("%1/%2_frame_%3.png")
        .arg(dir, base, QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    if (videoWithCrop->lastFrame.save(path)) {
        TimelineWidget::showNotification("FRAME SAVED 📸");
        statusLabel->setText("FRAME SAVED");
        statusLabel->show();
    } else {
        TimelineWidget::showNotification("SNAPSHOT FAILED ❌");
    }
}

void MainWindow::showShortcutsDialog() {
    QDialog dialog(this);
    dialog.setObjectName("SettingsDialog");
    dialog.setWindowTitle("Keyboard Shortcuts");
    dialog.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 14);
    layout->setSpacing(6);

    auto *title = new QLabel("KEYBOARD SHORTCUTS");
    title->setObjectName("SectionHeader");
    layout->addWidget(title);

    const QList<QPair<QString, QString>> rows = {
        {"Play / pause", editorSettings.keyPlayPause},
        {"Split clip", editorSettings.keySplit},
        {"Delete clip", editorSettings.keyDeleteClip},
        {"Replay", editorSettings.keyReplay},
        {"Forward", editorSettings.keyForward},
        {"Step back", editorSettings.keyStepBack},
        {"Step forward", editorSettings.keyStepForward},
        {"Undo", editorSettings.keyUndo},
        {"Redo", editorSettings.keyRedo},
        {"Export video", editorSettings.keyExportVideo},
        {"Export muted video", editorSettings.keyExportMutedVideo},
        {"Export audio", editorSettings.keyExportAudio},
        {"Export GIF", editorSettings.keyExportGif},
        {"Cycle audio track", editorSettings.keyCycleAudioTrack},
    };

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(7);
    for (int i = 0; i < rows.size(); ++i) {
        auto *nameLabel = new QLabel(rows[i].first);
        auto *keyLabel = new QLabel(rows[i].second);
        keyLabel->setObjectName("TimecodeLabel");
        keyLabel->setAlignment(Qt::AlignCenter);
        grid->addWidget(nameLabel, i, 0);
        grid->addWidget(keyLabel, i, 1, Qt::AlignRight);
    }
    layout->addLayout(grid);
    layout->addSpacing(8);

    auto *hint = new QLabel("Customize these in Settings → Keybinds.");
    hint->setObjectName("MetaData");
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

// Seeks the composition, loading a different source file into the player when
// the target time belongs to an appended clip.
void MainWindow::seekTimeline(qint64 timelinePosMs) {
    const auto &sources = timeline->sources;
    if (sources.isEmpty()) {
        player->setPosition(timelinePosMs);
        return;
    }

    const int targetIdx = timeline->sourceIndexForTimelineTime(timelinePosMs);
    const qint64 localPos = qMax<qint64>(0, timelinePosMs - timeline->sourceOffsetMs(targetIdx));

    if (targetIdx != activeSourceIdx) {
        const bool wasPlaying = player->playbackState() == QMediaPlayer::PlayingState;
        activeSourceIdx = targetIdx;
        switchingSource = true;
        pendingSeekLocalPos = localPos; // re-applied once the new file has loaded
        player->setSource(QUrl::fromLocalFile(sources[targetIdx].path));
        player->setPosition(localPos);
        if (wasPlaying) player->play();
    } else {
        player->setPosition(localPos);
    }
    timeline->setCurrentPosition(timelinePosMs);
    updateTimecodeDisplay();
    // The player may not emit a position tick at the target time (especially
    // right after a source switch), so keep the preview overlays in sync here.
    if (timeline->overlaysAtTime(timelinePosMs) != previewOverlayMap) syncOverlaysToPreview();
}

// Pushes the overlay clips active at the playhead into the preview widget so
// their regions render (and can be dragged) on the video.
void MainWindow::syncOverlaysToPreview() {
    syncingPreview = true;
    previewOverlayMap = timeline->overlaysAtTime(timeline->currentPosMs);

    QList<VideoWithCropWidget::FilterObject> regions;
    int selectedPreviewIdx = -1;
    for (int i = 0; i < previewOverlayMap.size(); ++i) {
        const auto &ov = timeline->overlays[previewOverlayMap[i]];
        VideoWithCropWidget::FilterObject obj;
        obj.l = ov.l; obj.t = ov.t; obj.r = ov.r; obj.b = ov.b;
        obj.mode = ov.type;
        obj.text = ov.text;
        regions.append(obj);
        if (previewOverlayMap[i] == timeline->selectedOverlayIdx) selectedPreviewIdx = i;
    }

    videoWithCrop->filterObjects = regions;
    videoWithCrop->selectedFilterIdx = selectedPreviewIdx;
    videoWithCrop->adjustingFilter = selectedPreviewIdx >= 0;
    videoWithCrop->triggerScale();
    videoWithCrop->update();
    syncingPreview = false;
}

void MainWindow::editTextOverlay(int index) {
    if (index < 0 || index >= timeline->overlays.size()) return;
    auto &ov = timeline->overlays[index];
    if (ov.type != 3) return;

    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, "Text Overlay",
                                                        "Text shown on the video:", ov.text, &ok);
    if (!ok) return;
    ov.text = text.trimmed().isEmpty() ? QStringLiteral("Your text") : text;
    timeline->update();
    syncOverlaysToPreview();
}

void MainWindow::openSettingsDialog() {
    QDialog dialog(this);
    dialog.setObjectName("SettingsDialog");
    dialog.setWindowTitle("Settings");
    dialog.resize(860, 560);
    dialog.setMinimumSize(640, 420);
    dialog.setSizeGripEnabled(true);
    dialog.setStyleSheet(buildAppStyleSheet());

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(12);

    // Sidebar navigation + stacked pages (West tab text renders rotated and
    // cramped — a list nav reads like a proper settings screen).
    auto *body = new QHBoxLayout();
    body->setSpacing(14);
    auto *navList = new QListWidget(&dialog);
    navList->setObjectName("SettingsNav");
    navList->setFixedWidth(150);
    navList->setFrameShape(QFrame::NoFrame);
    auto *pageStack = new QStackedWidget(&dialog);
    auto *pageColumn = new QVBoxLayout();
    pageColumn->setSpacing(6);
    auto *pageTitle = new QLabel(&dialog);
    pageTitle->setObjectName("SettingsPageTitle");
    pageColumn->addWidget(pageTitle);
    pageColumn->addWidget(pageStack, 1);
    body->addWidget(navList);
    body->addLayout(pageColumn, 1);
    layout->addLayout(body, 1);

    auto wrapSettingsPage = [&dialog](QWidget *page) {
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        page->setContentsMargins(4, 4, 12, 4);
        scroll->setWidget(page);
        return scroll;
    };
    auto addSettingsPage = [&](QWidget *page, const QString &name) {
        pageStack->addWidget(wrapSettingsPage(page));
        navList->addItem(name);
    };
    connect(navList, &QListWidget::currentRowChanged, &dialog, [pageStack, pageTitle, navList](int row) {
        if (row < 0) return;
        pageStack->setCurrentIndex(row);
        pageTitle->setText(navList->item(row)->text().toUpper());
    });
    auto makeResetButton = [&](QWidget *parent) {
        auto *button = new QPushButton("RESET THIS TAB", parent);
        button->setObjectName("PrimaryGhostBtn");
        return button;
    };

    auto *generalTab = new QWidget(&dialog);
    auto *generalForm = new QFormLayout(generalTab);
    generalForm->setLabelAlignment(Qt::AlignRight);

    auto makeStyledLabel = [&](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("SectionLabel");
        return label;
    };
    auto *autoPlayCheck = new QCheckBox("Start playback automatically after importing media", generalTab);
    autoPlayCheck->setChecked(editorSettings.autoPlayOnImport);
    auto *updateCheck = new QCheckBox("Check for updates on startup", generalTab);
    updateCheck->setChecked(editorSettings.checkForUpdatesOnStartup);
    auto *defaultVolumeSpin = new QSpinBox(generalTab);
    defaultVolumeSpin->setRange(0, 100);
    defaultVolumeSpin->setSuffix("%");
    defaultVolumeSpin->setValue(editorSettings.defaultVolumePercent);
    auto *recentMediaSpin = new QSpinBox(generalTab);
    recentMediaSpin->setRange(1, 32);
    recentMediaSpin->setValue(editorSettings.recentMediaLimit);
    auto *notificationSpin = new QSpinBox(generalTab);
    notificationSpin->setRange(250, 10000);
    notificationSpin->setSingleStep(250);
    notificationSpin->setSuffix(" ms");
    notificationSpin->setValue(editorSettings.notificationDurationMs);
    auto *notificationPositionBox = new QComboBox(generalTab);
    notificationPositionBox->addItems({"top-right", "top-left", "bottom-right", "bottom-left"});
    notificationPositionBox->setCurrentText(editorSettings.notificationPosition);
    auto *updateDelaySpin = new QSpinBox(generalTab);
    updateDelaySpin->setRange(0, 30000);
    updateDelaySpin->setSingleStep(250);
    updateDelaySpin->setSuffix(" ms");
    updateDelaySpin->setValue(editorSettings.updateCheckDelayMs);
    auto *windowTitleEdit = new QLineEdit(editorSettings.windowTitle, generalTab);
    auto *logoPrimaryEdit = new QLineEdit(editorSettings.logoPrimaryText, generalTab);
    auto *logoSecondaryEdit = new QLineEdit(editorSettings.logoSecondaryText, generalTab);
    auto *importButtonEdit = new QLineEdit(editorSettings.importButtonText, generalTab);
    auto *settingsPathEdit = new QLineEdit(appSettingsFilePath(), generalTab);
    settingsPathEdit->setReadOnly(true);
    auto *sidebarWidthSpin = new QSpinBox(generalTab);
    sidebarWidthSpin->setRange(180, 600);
    sidebarWidthSpin->setSuffix(" px");
    sidebarWidthSpin->setValue(editorSettings.sidebarWidth);
    auto *sidebarPositionBox = new QComboBox(generalTab);
    sidebarPositionBox->addItems({"left", "right"});
    sidebarPositionBox->setCurrentText(editorSettings.sidebarPosition);
    auto *toolOrderEdit = new QLineEdit(editorSettings.toolButtonOrder, generalTab);
    generalForm->addRow(autoPlayCheck);
    generalForm->addRow(updateCheck);
    generalForm->addRow(makeStyledLabel("Default volume"), defaultVolumeSpin);
    generalForm->addRow(makeStyledLabel("Recent media items"), recentMediaSpin);
    generalForm->addRow(makeStyledLabel("Toast duration"), notificationSpin);
    generalForm->addRow(makeStyledLabel("Toast corner"), notificationPositionBox);
    generalForm->addRow(makeStyledLabel("Update check delay"), updateDelaySpin);
    generalForm->addRow(makeStyledLabel("Window title"), windowTitleEdit);
    generalForm->addRow(makeStyledLabel("Logo primary text"), logoPrimaryEdit);
    generalForm->addRow(makeStyledLabel("Logo secondary text"), logoSecondaryEdit);
    generalForm->addRow(makeStyledLabel("Import button text"), importButtonEdit);
    generalForm->addRow(makeStyledLabel("Settings file"), settingsPathEdit);
    generalForm->addRow(makeStyledLabel("Sidebar width"), sidebarWidthSpin);
    generalForm->addRow(makeStyledLabel("Sidebar side"), sidebarPositionBox);
    generalForm->addRow(makeStyledLabel("Tool button order"), toolOrderEdit);
    auto *generalResetBtn = makeResetButton(generalTab);
    generalForm->addRow(generalResetBtn);
    addSettingsPage(generalTab, "General");

    const auto playback = timeline->getPlaybackSettings();
    auto *editingTab = new QWidget(&dialog);
    auto *editingForm = new QFormLayout(editingTab);
    editingForm->setLabelAlignment(Qt::AlignRight);
    auto *majorSeekSpin = new QSpinBox(editingTab);
    majorSeekSpin->setRange(100, 30000);
    majorSeekSpin->setSuffix(" ms");
    majorSeekSpin->setValue(playback.majorSeekMs);
    auto *minorSeekSpin = new QSpinBox(editingTab);
    minorSeekSpin->setRange(1, 1000);
    minorSeekSpin->setSuffix(" ms");
    minorSeekSpin->setValue(playback.minorSeekMs);
    auto *splitGuardSpin = new QSpinBox(editingTab);
    splitGuardSpin->setRange(50, 5000);
    splitGuardSpin->setSuffix(" ms");
    splitGuardSpin->setValue(playback.splitGuardMs);
    auto *minSegmentSpin = new QSpinBox(editingTab);
    minSegmentSpin->setRange(50, 5000);
    minSegmentSpin->setSuffix(" ms");
    minSegmentSpin->setValue(playback.minSegmentDurationMs);
    auto *cropTopSpin = new QDoubleSpinBox(editingTab);
    cropTopSpin->setRange(0.0, 1.0);
    cropTopSpin->setDecimals(3);
    cropTopSpin->setSingleStep(0.01);
    cropTopSpin->setValue(editorSettings.defaultCropTop);
    auto *cropBottomSpin = new QDoubleSpinBox(editingTab);
    cropBottomSpin->setRange(0.0, 1.0);
    cropBottomSpin->setDecimals(3);
    cropBottomSpin->setSingleStep(0.01);
    cropBottomSpin->setValue(editorSettings.defaultCropBottom);
    auto *cropLeftSpin = new QDoubleSpinBox(editingTab);
    cropLeftSpin->setRange(0.0, 1.0);
    cropLeftSpin->setDecimals(3);
    cropLeftSpin->setSingleStep(0.01);
    cropLeftSpin->setValue(editorSettings.defaultCropLeft);
    auto *cropRightSpin = new QDoubleSpinBox(editingTab);
    cropRightSpin->setRange(0.0, 1.0);
    cropRightSpin->setDecimals(3);
    cropRightSpin->setSingleStep(0.01);
    cropRightSpin->setValue(editorSettings.defaultCropRight);
    auto *previewTitleEdit = new QLineEdit(editorSettings.previewPlaceholderTitle, editingTab);
    auto *previewBodyEdit = new QLineEdit(editorSettings.previewPlaceholderBody, editingTab);
    auto *emptyHintEdit = new QLineEdit(editorSettings.emptyTransportHint, editingTab);
    auto *videoHintEdit = new QLineEdit(editorSettings.videoTransportHint, editingTab);
    auto *audioHintEdit = new QLineEdit(editorSettings.audioTransportHint, editingTab);
    editingForm->addRow(makeStyledLabel("Replay / jump step"), majorSeekSpin);
    editingForm->addRow(makeStyledLabel("Frame step"), minorSeekSpin);
    editingForm->addRow(makeStyledLabel("Split edge safety"), splitGuardSpin);
    editingForm->addRow(makeStyledLabel("Minimum segment length"), minSegmentSpin);
    editingForm->addRow(makeStyledLabel("Default crop top"), cropTopSpin);
    editingForm->addRow(makeStyledLabel("Default crop bottom"), cropBottomSpin);
    editingForm->addRow(makeStyledLabel("Default crop left"), cropLeftSpin);
    editingForm->addRow(makeStyledLabel("Default crop right"), cropRightSpin);
    editingForm->addRow(makeStyledLabel("Preview empty title"), previewTitleEdit);
    editingForm->addRow(makeStyledLabel("Preview empty body"), previewBodyEdit);
    editingForm->addRow(makeStyledLabel("No media hint"), emptyHintEdit);
    editingForm->addRow(makeStyledLabel("Video hint"), videoHintEdit);
    editingForm->addRow(makeStyledLabel("Audio hint"), audioHintEdit);
    auto *editingResetBtn = makeResetButton(editingTab);
    editingForm->addRow(editingResetBtn);
    addSettingsPage(editingTab, "Editing");

    auto *appearanceTab = new QWidget(&dialog);
    auto *appearanceForm = new QFormLayout(appearanceTab);
    auto makeColorControl = [&](const QString &value) {
        auto *row = new QWidget(appearanceTab);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(value, row);
        auto *swatch = new QLabel(row);
        swatch->setFixedSize(18, 18);
        auto *pickBtn = new QPushButton("Pick", row);
        auto refreshSwatch = [edit, swatch]() {
            const QColor c(edit->text().trimmed());
            const QString fill = c.isValid() ? c.name(QColor::HexRgb) : QString("#000000");
            swatch->setStyleSheet(QString("background:%1; border:1px solid #223229; border-radius:4px;").arg(fill));
        };
        connect(edit, &QLineEdit::textChanged, row, [refreshSwatch]() { refreshSwatch(); });
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(swatch);
        rowLayout->addWidget(pickBtn);
        connect(pickBtn, &QPushButton::clicked, &dialog, [this, edit]() {
            const QColor chosen = QColorDialog::getColor(QColor(edit->text().trimmed()), this, "Pick Color");
            if (chosen.isValid()) edit->setText(chosen.name(QColor::HexRgb));
        });
        refreshSwatch();
        return qMakePair(row, edit);
    };
    auto timelineAccentControl = makeColorControl(editorSettings.timelineAccentColor);
    auto timelineSecondaryControl = makeColorControl(editorSettings.timelineSecondaryColor);
    auto timelineBackgroundControl = makeColorControl(editorSettings.timelineBackgroundColor);
    auto timelineTrackControl = makeColorControl(editorSettings.timelineTrackColor);
    auto timelineWaveformControl = makeColorControl(editorSettings.timelineWaveformColor);
    auto previewAccentControl = makeColorControl(editorSettings.previewAccentColor);
    auto previewSecondaryControl = makeColorControl(editorSettings.previewSecondaryColor);
    auto previewBackgroundControl = makeColorControl(editorSettings.previewBackgroundColor);
    auto appBackgroundStartControl = makeColorControl(editorSettings.appBackgroundStartColor);
    auto appBackgroundEndControl = makeColorControl(editorSettings.appBackgroundEndColor);
    auto panelSurfaceControl = makeColorControl(editorSettings.panelSurfaceColor);
    auto panelAltSurfaceControl = makeColorControl(editorSettings.panelAltSurfaceColor);
    auto controlSurfaceControl = makeColorControl(editorSettings.controlSurfaceColor);
    auto controlHoverControl = makeColorControl(editorSettings.controlHoverColor);
    auto borderControl = makeColorControl(editorSettings.borderColor);
    auto primaryTextControl = makeColorControl(editorSettings.primaryTextColor);
    auto mutedTextControl = makeColorControl(editorSettings.mutedTextColor);
    auto sectionLabelControl = makeColorControl(editorSettings.sectionLabelColor);
    auto logoPrimaryControl = makeColorControl(editorSettings.logoPrimaryColor);
    auto logoSecondaryControl = makeColorControl(editorSettings.logoSecondaryColor);
    auto *timelineAccentEdit = timelineAccentControl.second;
    auto *timelineSecondaryEdit = timelineSecondaryControl.second;
    auto *timelineBackgroundEdit = timelineBackgroundControl.second;
    auto *timelineTrackEdit = timelineTrackControl.second;
    auto *timelineWaveformEdit = timelineWaveformControl.second;
    auto *previewAccentEdit = previewAccentControl.second;
    auto *previewSecondaryEdit = previewSecondaryControl.second;
    auto *previewBackgroundEdit = previewBackgroundControl.second;
    auto *appBackgroundStartEdit = appBackgroundStartControl.second;
    auto *appBackgroundEndEdit = appBackgroundEndControl.second;
    auto *panelSurfaceEdit = panelSurfaceControl.second;
    auto *panelAltSurfaceEdit = panelAltSurfaceControl.second;
    auto *controlSurfaceEdit = controlSurfaceControl.second;
    auto *controlHoverEdit = controlHoverControl.second;
    auto *borderEdit = borderControl.second;
    auto *primaryTextEdit = primaryTextControl.second;
    auto *mutedTextEdit = mutedTextControl.second;
    auto *sectionLabelEdit = sectionLabelControl.second;
    auto *logoPrimaryColorEdit = logoPrimaryControl.second;
    auto *logoSecondaryColorEdit = logoSecondaryControl.second;
    auto *fontFamilyBox = new QFontComboBox(appearanceTab);
    fontFamilyBox->setCurrentFont(QFont(editorSettings.appFontFamily));
    auto *fontSizeSpin = new QSpinBox(appearanceTab);
    fontSizeSpin->setRange(8, 36);
    fontSizeSpin->setValue(editorSettings.appFontPointSize);
    auto *logoFontSizeSpin = new QSpinBox(appearanceTab);
    logoFontSizeSpin->setRange(8, 48);
    logoFontSizeSpin->setValue(editorSettings.logoFontPointSize);
    auto *badgeFontSizeSpin = new QSpinBox(appearanceTab);
    badgeFontSizeSpin->setRange(7, 30);
    badgeFontSizeSpin->setValue(editorSettings.mediaBadgeFontPointSize);
    auto *metaFontSizeSpin = new QSpinBox(appearanceTab);
    metaFontSizeSpin->setRange(7, 30);
    metaFontSizeSpin->setValue(editorSettings.metaFontPointSize);
    auto *panelRadiusSpin = new QSpinBox(appearanceTab);
    panelRadiusSpin->setRange(0, 40);
    panelRadiusSpin->setValue(editorSettings.panelCornerRadius);
    auto *buttonRadiusSpin = new QSpinBox(appearanceTab);
    buttonRadiusSpin->setRange(0, 40);
    buttonRadiusSpin->setValue(editorSettings.buttonCornerRadius);
    appearanceForm->addRow("Timeline accent", timelineAccentControl.first);
    appearanceForm->addRow("Timeline secondary", timelineSecondaryControl.first);
    appearanceForm->addRow("Timeline background", timelineBackgroundControl.first);
    appearanceForm->addRow("Timeline track", timelineTrackControl.first);
    appearanceForm->addRow("Timeline waveform", timelineWaveformControl.first);
    appearanceForm->addRow("Preview accent", previewAccentControl.first);
    appearanceForm->addRow("Preview secondary", previewSecondaryControl.first);
    appearanceForm->addRow("Preview background", previewBackgroundControl.first);
    appearanceForm->addRow("App background start", appBackgroundStartControl.first);
    appearanceForm->addRow("App background end", appBackgroundEndControl.first);
    appearanceForm->addRow("Panel surface", panelSurfaceControl.first);
    appearanceForm->addRow("Panel alt surface", panelAltSurfaceControl.first);
    appearanceForm->addRow("Control surface", controlSurfaceControl.first);
    appearanceForm->addRow("Control hover", controlHoverControl.first);
    appearanceForm->addRow("Border color", borderControl.first);
    appearanceForm->addRow("Primary text", primaryTextControl.first);
    appearanceForm->addRow("Muted text", mutedTextControl.first);
    appearanceForm->addRow("Section label text", sectionLabelControl.first);
    appearanceForm->addRow("Logo primary text", logoPrimaryControl.first);
    appearanceForm->addRow("Logo secondary text", logoSecondaryControl.first);
    appearanceForm->addRow("Font family", fontFamilyBox);
    appearanceForm->addRow("Font size", fontSizeSpin);
    appearanceForm->addRow("Logo font size", logoFontSizeSpin);
    appearanceForm->addRow("Media badge font size", badgeFontSizeSpin);
    appearanceForm->addRow("Meta font size", metaFontSizeSpin);
    appearanceForm->addRow("Panel corner radius", panelRadiusSpin);
    appearanceForm->addRow("Button corner radius", buttonRadiusSpin);
    auto *appearanceResetBtn = makeResetButton(appearanceTab);
    appearanceForm->addRow(appearanceResetBtn);
    addSettingsPage(appearanceTab, "Appearance");

    auto *keybindTab = new QWidget(&dialog);
    auto *keybindForm = new QFormLayout(keybindTab);
    auto *playPauseKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyPlayPause, QKeySequence::PortableText), keybindTab);
    auto *splitKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keySplit, QKeySequence::PortableText), keybindTab);
    auto *deleteKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyDeleteClip, QKeySequence::PortableText), keybindTab);
    auto *replayKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyReplay, QKeySequence::PortableText), keybindTab);
    auto *forwardKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyForward, QKeySequence::PortableText), keybindTab);
    auto *stepBackKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyStepBack, QKeySequence::PortableText), keybindTab);
    auto *stepForwardKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyStepForward, QKeySequence::PortableText), keybindTab);
    auto *undoKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyUndo, QKeySequence::PortableText), keybindTab);
    auto *redoKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyRedo, QKeySequence::PortableText), keybindTab);
    auto *gifKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyExportGif, QKeySequence::PortableText), keybindTab);
    auto *audioKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyExportAudio, QKeySequence::PortableText), keybindTab);
    auto *videoKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyExportVideo, QKeySequence::PortableText), keybindTab);
    auto *mutedVideoKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyExportMutedVideo, QKeySequence::PortableText), keybindTab);
    auto *cycleAudioKeyEdit = new QKeySequenceEdit(QKeySequence::fromString(editorSettings.keyCycleAudioTrack, QKeySequence::PortableText), keybindTab);
    keybindForm->addRow("Play / pause", playPauseKeyEdit);
    keybindForm->addRow("Split clip", splitKeyEdit);
    keybindForm->addRow("Delete clip", deleteKeyEdit);
    keybindForm->addRow("Replay", replayKeyEdit);
    keybindForm->addRow("Forward", forwardKeyEdit);
    keybindForm->addRow("Step back", stepBackKeyEdit);
    keybindForm->addRow("Step forward", stepForwardKeyEdit);
    keybindForm->addRow("Undo", undoKeyEdit);
    keybindForm->addRow("Redo", redoKeyEdit);
    keybindForm->addRow("Export GIF", gifKeyEdit);
    keybindForm->addRow("Export audio", audioKeyEdit);
    keybindForm->addRow("Export video", videoKeyEdit);
    keybindForm->addRow("Export muted video", mutedVideoKeyEdit);
    keybindForm->addRow("Cycle audio track", cycleAudioKeyEdit);
    auto *keybindResetBtn = makeResetButton(keybindTab);
    keybindForm->addRow(keybindResetBtn);
    addSettingsPage(keybindTab, "Keybinds");

    const auto exportSettings = timeline->getExportSettings();
    auto *exportTab = new QWidget(&dialog);
    auto *exportForm = new QFormLayout(exportTab);
    auto *exportDirRow = new QWidget(exportTab);
    auto *exportDirLayout = new QHBoxLayout(exportDirRow);
    exportDirLayout->setContentsMargins(0, 0, 0, 0);
    auto *exportDirEdit = new QLineEdit(exportSettings.exportDirectory, exportDirRow);
    auto *browseBtn = new QPushButton("Browse", exportDirRow);
    exportDirLayout->addWidget(exportDirEdit, 1);
    exportDirLayout->addWidget(browseBtn);
    auto *gifFpsSpin = new QSpinBox(exportTab);
    gifFpsSpin->setRange(1, 60);
    gifFpsSpin->setValue(exportSettings.gifFps);
    auto *gifWidthSpin = new QSpinBox(exportTab);
    gifWidthSpin->setRange(64, 4096);
    gifWidthSpin->setSuffix(" px");
    gifWidthSpin->setValue(exportSettings.gifWidth);
    auto *audioBitrateSpin = new QSpinBox(exportTab);
    audioBitrateSpin->setRange(32, 512);
    audioBitrateSpin->setSuffix(" kbps");
    audioBitrateSpin->setValue(exportSettings.audioBitrateKbps);
    auto *compressedAudioSpin = new QSpinBox(exportTab);
    compressedAudioSpin->setRange(32, 320);
    compressedAudioSpin->setSuffix(" kbps");
    compressedAudioSpin->setValue(exportSettings.compressedAudioBitrateKbps);
    auto *compressThresholdSpin = new QDoubleSpinBox(exportTab);
    compressThresholdSpin->setRange(1.0, 500.0);
    compressThresholdSpin->setDecimals(1);
    compressThresholdSpin->setSuffix(" MB");
    compressThresholdSpin->setValue(exportSettings.videoCompressionThresholdMB);
    auto *targetSizeSpin = new QDoubleSpinBox(exportTab);
    targetSizeSpin->setRange(1.0, 500.0);
    targetSizeSpin->setDecimals(1);
    targetSizeSpin->setSuffix(" MB");
    targetSizeSpin->setValue(exportSettings.targetCompressedSizeMB);
    auto *fileNamePrefixEdit = new QLineEdit(exportSettings.fileNamePrefix, exportTab);
    auto *includeSourceNameCheck = new QCheckBox("Include original source name in generated filenames", exportTab);
    includeSourceNameCheck->setChecked(exportSettings.includeSourceNameInExport);
    exportForm->addRow("Export directory", exportDirRow);
    exportForm->addRow("GIF FPS", gifFpsSpin);
    exportForm->addRow("GIF width", gifWidthSpin);
    exportForm->addRow("Audio bitrate", audioBitrateSpin);
    exportForm->addRow("Compressed audio bitrate", compressedAudioSpin);
    exportForm->addRow("Compress video above", compressThresholdSpin);
    exportForm->addRow("Compressed video target", targetSizeSpin);
    exportForm->addRow("Generated filename prefix", fileNamePrefixEdit);
    exportForm->addRow(includeSourceNameCheck);
    auto *exportResetBtn = makeResetButton(exportTab);
    exportForm->addRow(exportResetBtn);
    addSettingsPage(exportTab, "Export");

    const auto current = timeline->getAutoCutSettings();
    auto *autoCutTab = new QWidget(&dialog);
    auto *autoCutForm = new QFormLayout(autoCutTab);
    auto *thresholdSpin = new QDoubleSpinBox(autoCutTab);
    thresholdSpin->setRange(-100.0, 0.0);
    thresholdSpin->setDecimals(1);
    thresholdSpin->setSingleStep(1.0);
    thresholdSpin->setSuffix(" dB");
    thresholdSpin->setValue(current.silenceThresholdDb);
    auto *minSilenceSpin = new QDoubleSpinBox(autoCutTab);
    minSilenceSpin->setRange(0.05, 10.0);
    minSilenceSpin->setDecimals(2);
    minSilenceSpin->setSingleStep(0.05);
    minSilenceSpin->setSuffix(" s");
    minSilenceSpin->setValue(current.minimumSilenceDurationSec);
    auto *paddingSpin = new QDoubleSpinBox(autoCutTab);
    paddingSpin->setRange(0.0, 2.0);
    paddingSpin->setDecimals(2);
    paddingSpin->setSingleStep(0.05);
    paddingSpin->setSuffix(" s");
    paddingSpin->setValue(current.paddingSec);
    auto *minClipSpin = new QDoubleSpinBox(autoCutTab);
    minClipSpin->setRange(0.05, 5.0);
    minClipSpin->setDecimals(2);
    minClipSpin->setSingleStep(0.05);
    minClipSpin->setSuffix(" s");
    minClipSpin->setValue(current.minimumClipDurationSec);
    autoCutForm->addRow("Silence threshold", thresholdSpin);
    autoCutForm->addRow("Min silence length", minSilenceSpin);
    autoCutForm->addRow("Keep padding", paddingSpin);
    autoCutForm->addRow("Min kept clip", minClipSpin);
    auto *autoCutResetBtn = makeResetButton(autoCutTab);
    autoCutForm->addRow(autoCutResetBtn);
    addSettingsPage(autoCutTab, "Auto-Cut");

    auto *mediaTab = new QWidget(&dialog);
    auto *mediaLayout = new QVBoxLayout(mediaTab);
    auto *dirList = new QListWidget(mediaTab);
    dirList->addItems(editorSettings.autoLoadDirectories);
    auto *dirButtons = new QHBoxLayout();
    auto *addDirBtn = new QPushButton("ADD FOLDER", mediaTab);
    auto *removeDirBtn = new QPushButton("REMOVE SELECTED", mediaTab);
    addDirBtn->setObjectName("ToolBtn");
    removeDirBtn->setObjectName("ToolBtn");
    dirButtons->addWidget(addDirBtn);
    dirButtons->addWidget(removeDirBtn);
    mediaLayout->addWidget(new QLabel("AUTO-LOAD FOLDERS (SCANNED AT STARTUP):", mediaTab));
    mediaLayout->addWidget(dirList);
    mediaLayout->addLayout(dirButtons);
    addSettingsPage(mediaTab, "Media");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto *importThemeBtn = buttons->addButton("Import Theme", QDialogButtonBox::ActionRole);
    auto *exportThemeBtn = buttons->addButton("Export Theme", QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, &dialog, [this, exportDirEdit]() {
        const QString currentDir = exportDirEdit->text().trimmed().isEmpty() ? defaultExportDirectory() : exportDirEdit->text().trimmed();
        const QString chosen = QFileDialog::getExistingDirectory(this, "Choose Export Directory", currentDir);
        if (!chosen.isEmpty()) exportDirEdit->setText(chosen);
    });
    connect(generalResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const EditorSettings defaults;
        autoPlayCheck->setChecked(defaults.autoPlayOnImport);
        updateCheck->setChecked(defaults.checkForUpdatesOnStartup);
        defaultVolumeSpin->setValue(defaults.defaultVolumePercent);
        recentMediaSpin->setValue(defaults.recentMediaLimit);
        notificationSpin->setValue(defaults.notificationDurationMs);
        notificationPositionBox->setCurrentText(defaults.notificationPosition);
        updateDelaySpin->setValue(defaults.updateCheckDelayMs);
        windowTitleEdit->setText(defaults.windowTitle);
        logoPrimaryEdit->setText(defaults.logoPrimaryText);
        logoSecondaryEdit->setText(defaults.logoSecondaryText);
        importButtonEdit->setText(defaults.importButtonText);
        sidebarWidthSpin->setValue(defaults.sidebarWidth);
        sidebarPositionBox->setCurrentText(defaults.sidebarPosition);
        toolOrderEdit->setText(defaults.toolButtonOrder);
    });
    connect(editingResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const EditorSettings defaults;
        const TimelineWidget::PlaybackSettings playbackDefaults;
        majorSeekSpin->setValue(playbackDefaults.majorSeekMs);
        minorSeekSpin->setValue(playbackDefaults.minorSeekMs);
        splitGuardSpin->setValue(playbackDefaults.splitGuardMs);
        minSegmentSpin->setValue(playbackDefaults.minSegmentDurationMs);
        cropTopSpin->setValue(defaults.defaultCropTop);
        cropBottomSpin->setValue(defaults.defaultCropBottom);
        cropLeftSpin->setValue(defaults.defaultCropLeft);
        cropRightSpin->setValue(defaults.defaultCropRight);
        previewTitleEdit->setText(defaults.previewPlaceholderTitle);
        previewBodyEdit->setText(defaults.previewPlaceholderBody);
        emptyHintEdit->setText(defaults.emptyTransportHint);
        videoHintEdit->setText(defaults.videoTransportHint);
        audioHintEdit->setText(defaults.audioTransportHint);
    });
    connect(appearanceResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const EditorSettings defaults;
        timelineAccentEdit->setText(defaults.timelineAccentColor);
        timelineSecondaryEdit->setText(defaults.timelineSecondaryColor);
        timelineBackgroundEdit->setText(defaults.timelineBackgroundColor);
        timelineTrackEdit->setText(defaults.timelineTrackColor);
        timelineWaveformEdit->setText(defaults.timelineWaveformColor);
        previewAccentEdit->setText(defaults.previewAccentColor);
        previewSecondaryEdit->setText(defaults.previewSecondaryColor);
        previewBackgroundEdit->setText(defaults.previewBackgroundColor);
        appBackgroundStartEdit->setText(defaults.appBackgroundStartColor);
        appBackgroundEndEdit->setText(defaults.appBackgroundEndColor);
        panelSurfaceEdit->setText(defaults.panelSurfaceColor);
        panelAltSurfaceEdit->setText(defaults.panelAltSurfaceColor);
        controlSurfaceEdit->setText(defaults.controlSurfaceColor);
        controlHoverEdit->setText(defaults.controlHoverColor);
        borderEdit->setText(defaults.borderColor);
        primaryTextEdit->setText(defaults.primaryTextColor);
        mutedTextEdit->setText(defaults.mutedTextColor);
        sectionLabelEdit->setText(defaults.sectionLabelColor);
        logoPrimaryColorEdit->setText(defaults.logoPrimaryColor);
        logoSecondaryColorEdit->setText(defaults.logoSecondaryColor);
        fontFamilyBox->setCurrentFont(QFont(defaults.appFontFamily));
        fontSizeSpin->setValue(defaults.appFontPointSize);
        panelRadiusSpin->setValue(defaults.panelCornerRadius);
        buttonRadiusSpin->setValue(defaults.buttonCornerRadius);
    });
    connect(keybindResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const EditorSettings defaults;
        playPauseKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyPlayPause, QKeySequence::PortableText));
        splitKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keySplit, QKeySequence::PortableText));
        deleteKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyDeleteClip, QKeySequence::PortableText));
        replayKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyReplay, QKeySequence::PortableText));
        forwardKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyForward, QKeySequence::PortableText));
        stepBackKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyStepBack, QKeySequence::PortableText));
        stepForwardKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyStepForward, QKeySequence::PortableText));
        undoKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyUndo, QKeySequence::PortableText));
        redoKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyRedo, QKeySequence::PortableText));
        gifKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyExportGif, QKeySequence::PortableText));
        audioKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyExportAudio, QKeySequence::PortableText));
        videoKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyExportVideo, QKeySequence::PortableText));
        mutedVideoKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyExportMutedVideo, QKeySequence::PortableText));
        cycleAudioKeyEdit->setKeySequence(QKeySequence::fromString(defaults.keyCycleAudioTrack, QKeySequence::PortableText));
    });
    connect(exportResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const TimelineWidget::ExportSettings defaults;
        exportDirEdit->setText(defaultExportDirectory());
        gifFpsSpin->setValue(defaults.gifFps);
        gifWidthSpin->setValue(defaults.gifWidth);
        audioBitrateSpin->setValue(defaults.audioBitrateKbps);
        compressedAudioSpin->setValue(defaults.compressedAudioBitrateKbps);
        compressThresholdSpin->setValue(defaults.videoCompressionThresholdMB);
        targetSizeSpin->setValue(defaults.targetCompressedSizeMB);
        fileNamePrefixEdit->setText(defaults.fileNamePrefix);
        includeSourceNameCheck->setChecked(defaults.includeSourceNameInExport);
    });
    connect(autoCutResetBtn, &QPushButton::clicked, &dialog, [=]() {
        const TimelineWidget::AutoCutSettings defaults;
        thresholdSpin->setValue(defaults.silenceThresholdDb);
        minSilenceSpin->setValue(defaults.minimumSilenceDurationSec);
        paddingSpin->setValue(defaults.paddingSec);
        minClipSpin->setValue(defaults.minimumClipDurationSec);
    });
    connect(addDirBtn, &QPushButton::clicked, &dialog, [this, dirList]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Media Folder", QDir::homePath());
        if (!dir.isEmpty()) {
            if (dirList->findItems(dir, Qt::MatchExactly).isEmpty()) {
                dirList->addItem(dir);
            }
        }
    });
    connect(removeDirBtn, &QPushButton::clicked, &dialog, [dirList]() {
        QListWidgetItem *item = dirList->currentItem();
        if (item) delete item;
    });
    connect(importThemeBtn, &QPushButton::clicked, &dialog, [=]() {
        const QString path = QFileDialog::getOpenFileName(this, "Import Theme", QDir::homePath(), "Theme Files (*.json)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject general = root.value("general").toObject();
        const QJsonObject editing = root.value("editing").toObject();
        const QJsonObject appearance = root.value("appearance").toObject();
        const QJsonObject keybinds = root.value("keybinds").toObject();
        const QJsonObject exportObj = root.value("export").toObject();
        const QJsonObject autoCutObj = root.value("autoCut").toObject();

        if (!general.isEmpty()) {
            windowTitleEdit->setText(general.value("windowTitle").toString(windowTitleEdit->text()));
            logoPrimaryEdit->setText(general.value("logoPrimaryText").toString(logoPrimaryEdit->text()));
            logoSecondaryEdit->setText(general.value("logoSecondaryText").toString(logoSecondaryEdit->text()));
            importButtonEdit->setText(general.value("importButtonText").toString(importButtonEdit->text()));
            sidebarWidthSpin->setValue(general.value("sidebarWidth").toInt(sidebarWidthSpin->value()));
            sidebarPositionBox->setCurrentText(general.value("sidebarPosition").toString(sidebarPositionBox->currentText()));
            toolOrderEdit->setText(general.value("toolButtonOrder").toString(toolOrderEdit->text()));
            defaultVolumeSpin->setValue(general.value("defaultVolumePercent").toInt(defaultVolumeSpin->value()));
            recentMediaSpin->setValue(general.value("recentMediaLimit").toInt(recentMediaSpin->value()));
            notificationSpin->setValue(general.value("notificationDurationMs").toInt(notificationSpin->value()));
            notificationPositionBox->setCurrentText(general.value("notificationPosition").toString(notificationPositionBox->currentText()));
            updateDelaySpin->setValue(general.value("updateCheckDelayMs").toInt(updateDelaySpin->value()));
            autoPlayCheck->setChecked(general.value("autoPlayOnImport").toBool(autoPlayCheck->isChecked()));
            updateCheck->setChecked(general.value("checkForUpdatesOnStartup").toBool(updateCheck->isChecked()));
        }
        if (!editing.isEmpty()) {
            majorSeekSpin->setValue(editing.value("majorSeekMs").toInt(majorSeekSpin->value()));
            minorSeekSpin->setValue(editing.value("minorSeekMs").toInt(minorSeekSpin->value()));
            splitGuardSpin->setValue(editing.value("splitGuardMs").toInt(splitGuardSpin->value()));
            minSegmentSpin->setValue(editing.value("minSegmentDurationMs").toInt(minSegmentSpin->value()));
            cropTopSpin->setValue(editing.value("defaultCropTop").toDouble(cropTopSpin->value()));
            cropBottomSpin->setValue(editing.value("defaultCropBottom").toDouble(cropBottomSpin->value()));
            cropLeftSpin->setValue(editing.value("defaultCropLeft").toDouble(cropLeftSpin->value()));
            cropRightSpin->setValue(editing.value("defaultCropRight").toDouble(cropRightSpin->value()));
            previewTitleEdit->setText(editing.value("previewPlaceholderTitle").toString(previewTitleEdit->text()));
            previewBodyEdit->setText(editing.value("previewPlaceholderBody").toString(previewBodyEdit->text()));
            emptyHintEdit->setText(editing.value("emptyTransportHint").toString(emptyHintEdit->text()));
            videoHintEdit->setText(editing.value("videoTransportHint").toString(videoHintEdit->text()));
            audioHintEdit->setText(editing.value("audioTransportHint").toString(audioHintEdit->text()));
        }
        if (!appearance.isEmpty()) {
            timelineAccentEdit->setText(appearance.value("timelineAccentColor").toString(timelineAccentEdit->text()));
            timelineSecondaryEdit->setText(appearance.value("timelineSecondaryColor").toString(timelineSecondaryEdit->text()));
            timelineBackgroundEdit->setText(appearance.value("timelineBackgroundColor").toString(timelineBackgroundEdit->text()));
            timelineTrackEdit->setText(appearance.value("timelineTrackColor").toString(timelineTrackEdit->text()));
            timelineWaveformEdit->setText(appearance.value("timelineWaveformColor").toString(timelineWaveformEdit->text()));
            previewAccentEdit->setText(appearance.value("previewAccentColor").toString(previewAccentEdit->text()));
            previewSecondaryEdit->setText(appearance.value("previewSecondaryColor").toString(previewSecondaryEdit->text()));
            previewBackgroundEdit->setText(appearance.value("previewBackgroundColor").toString(previewBackgroundEdit->text()));
            appBackgroundStartEdit->setText(appearance.value("appBackgroundStartColor").toString(appBackgroundStartEdit->text()));
            appBackgroundEndEdit->setText(appearance.value("appBackgroundEndColor").toString(appBackgroundEndEdit->text()));
            panelSurfaceEdit->setText(appearance.value("panelSurfaceColor").toString(panelSurfaceEdit->text()));
            panelAltSurfaceEdit->setText(appearance.value("panelAltSurfaceColor").toString(panelAltSurfaceEdit->text()));
            controlSurfaceEdit->setText(appearance.value("controlSurfaceColor").toString(controlSurfaceEdit->text()));
            controlHoverEdit->setText(appearance.value("controlHoverColor").toString(controlHoverEdit->text()));
            borderEdit->setText(appearance.value("borderColor").toString(borderEdit->text()));
            primaryTextEdit->setText(appearance.value("primaryTextColor").toString(primaryTextEdit->text()));
            mutedTextEdit->setText(appearance.value("mutedTextColor").toString(mutedTextEdit->text()));
            sectionLabelEdit->setText(appearance.value("sectionLabelColor").toString(sectionLabelEdit->text()));
            logoPrimaryColorEdit->setText(appearance.value("logoPrimaryColor").toString(logoPrimaryColorEdit->text()));
            logoSecondaryColorEdit->setText(appearance.value("logoSecondaryColor").toString(logoSecondaryColorEdit->text()));
            fontFamilyBox->setCurrentFont(QFont(appearance.value("appFontFamily").toString(fontFamilyBox->currentFont().family())));
            fontSizeSpin->setValue(appearance.value("appFontPointSize").toInt(fontSizeSpin->value()));
            logoFontSizeSpin->setValue(appearance.value("logoFontPointSize").toInt(logoFontSizeSpin->value()));
            badgeFontSizeSpin->setValue(appearance.value("mediaBadgeFontPointSize").toInt(badgeFontSizeSpin->value()));
            metaFontSizeSpin->setValue(appearance.value("metaFontPointSize").toInt(metaFontSizeSpin->value()));
            panelRadiusSpin->setValue(appearance.value("panelCornerRadius").toInt(panelRadiusSpin->value()));
            buttonRadiusSpin->setValue(appearance.value("buttonCornerRadius").toInt(buttonRadiusSpin->value()));
        }
        if (!keybinds.isEmpty()) {
            playPauseKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("playPause").toString(), QKeySequence::PortableText));
            splitKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("split").toString(), QKeySequence::PortableText));
            deleteKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("deleteClip").toString(), QKeySequence::PortableText));
            replayKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("replay").toString(), QKeySequence::PortableText));
            forwardKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("forward").toString(), QKeySequence::PortableText));
            stepBackKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("stepBack").toString(), QKeySequence::PortableText));
            stepForwardKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("stepForward").toString(), QKeySequence::PortableText));
            undoKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("undo").toString(), QKeySequence::PortableText));
            redoKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("redo").toString(), QKeySequence::PortableText));
            gifKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("exportGif").toString(), QKeySequence::PortableText));
            audioKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("exportAudio").toString(), QKeySequence::PortableText));
            videoKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("exportVideo").toString(), QKeySequence::PortableText));
            mutedVideoKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("exportMutedVideo").toString(), QKeySequence::PortableText));
            cycleAudioKeyEdit->setKeySequence(QKeySequence::fromString(keybinds.value("cycleAudioTrack").toString(), QKeySequence::PortableText));
        }
        if (!exportObj.isEmpty()) {
            exportDirEdit->setText(exportObj.value("exportDirectory").toString(exportDirEdit->text()));
            gifFpsSpin->setValue(exportObj.value("gifFps").toInt(gifFpsSpin->value()));
            gifWidthSpin->setValue(exportObj.value("gifWidth").toInt(gifWidthSpin->value()));
            audioBitrateSpin->setValue(exportObj.value("audioBitrateKbps").toInt(audioBitrateSpin->value()));
            compressedAudioSpin->setValue(exportObj.value("compressedAudioBitrateKbps").toInt(compressedAudioSpin->value()));
            compressThresholdSpin->setValue(exportObj.value("videoCompressionThresholdMB").toDouble(compressThresholdSpin->value()));
            targetSizeSpin->setValue(exportObj.value("targetCompressedSizeMB").toDouble(targetSizeSpin->value()));
            fileNamePrefixEdit->setText(exportObj.value("fileNamePrefix").toString(fileNamePrefixEdit->text()));
            includeSourceNameCheck->setChecked(exportObj.value("includeSourceNameInExport").toBool(includeSourceNameCheck->isChecked()));
        }
        if (!autoCutObj.isEmpty()) {
            thresholdSpin->setValue(autoCutObj.value("silenceThresholdDb").toDouble(thresholdSpin->value()));
            minSilenceSpin->setValue(autoCutObj.value("minimumSilenceDurationSec").toDouble(minSilenceSpin->value()));
            paddingSpin->setValue(autoCutObj.value("paddingSec").toDouble(paddingSpin->value()));
            minClipSpin->setValue(autoCutObj.value("minimumClipDurationSec").toDouble(minClipSpin->value()));
        }
    });
    connect(exportThemeBtn, &QPushButton::clicked, &dialog, [=]() {
        const QString path = QFileDialog::getSaveFileName(this, "Export Theme", QDir::homePath() + "/potato-editor-theme.json", "Theme Files (*.json)");
        if (path.isEmpty()) return;
        QJsonObject root;
        root["general"] = QJsonObject{
            {"autoPlayOnImport", QJsonValue(autoPlayCheck->isChecked())},
            {"checkForUpdatesOnStartup", QJsonValue(updateCheck->isChecked())},
            {"defaultVolumePercent", QJsonValue(defaultVolumeSpin->value())},
            {"recentMediaLimit", QJsonValue(recentMediaSpin->value())},
            {"notificationDurationMs", QJsonValue(notificationSpin->value())},
            {"notificationPosition", QJsonValue(notificationPositionBox->currentText())},
            {"updateCheckDelayMs", QJsonValue(updateDelaySpin->value())},
            {"windowTitle", QJsonValue(windowTitleEdit->text())},
            {"logoPrimaryText", QJsonValue(logoPrimaryEdit->text())},
            {"logoSecondaryText", QJsonValue(logoSecondaryEdit->text())},
            {"importButtonText", QJsonValue(importButtonEdit->text())},
            {"sidebarWidth", QJsonValue(sidebarWidthSpin->value())},
            {"sidebarPosition", QJsonValue(sidebarPositionBox->currentText())},
            {"toolButtonOrder", QJsonValue(toolOrderEdit->text())}
        };
        root["editing"] = QJsonObject{
            {"majorSeekMs", QJsonValue(majorSeekSpin->value())},
            {"minorSeekMs", QJsonValue(minorSeekSpin->value())},
            {"splitGuardMs", QJsonValue(splitGuardSpin->value())},
            {"minSegmentDurationMs", QJsonValue(minSegmentSpin->value())},
            {"defaultCropTop", QJsonValue(cropTopSpin->value())},
            {"defaultCropBottom", QJsonValue(cropBottomSpin->value())},
            {"defaultCropLeft", QJsonValue(cropLeftSpin->value())},
            {"defaultCropRight", QJsonValue(cropRightSpin->value())},
            {"previewPlaceholderTitle", QJsonValue(previewTitleEdit->text())},
            {"previewPlaceholderBody", QJsonValue(previewBodyEdit->text())},
            {"emptyTransportHint", QJsonValue(emptyHintEdit->text())},
            {"videoTransportHint", QJsonValue(videoHintEdit->text())},
            {"audioHintEdit", QJsonValue(audioHintEdit->text())}
        };
        root["appearance"] = QJsonObject{
            {"timelineAccentColor", timelineAccentEdit->text()},
            {"timelineSecondaryColor", timelineSecondaryEdit->text()},
            {"timelineBackgroundColor", timelineBackgroundEdit->text()},
            {"timelineTrackColor", timelineTrackEdit->text()},
            {"timelineWaveformColor", timelineWaveformEdit->text()},
            {"previewAccentColor", previewAccentEdit->text()},
            {"previewSecondaryColor", previewSecondaryEdit->text()},
            {"previewBackgroundColor", previewBackgroundEdit->text()},
            {"appBackgroundStartColor", appBackgroundStartEdit->text()},
            {"appBackgroundEndColor", appBackgroundEndEdit->text()},
            {"panelSurfaceColor", panelSurfaceEdit->text()},
            {"panelAltSurfaceColor", panelAltSurfaceEdit->text()},
            {"controlSurfaceColor", controlSurfaceEdit->text()},
            {"controlHoverColor", controlHoverEdit->text()},
            {"borderColor", borderEdit->text()},
            {"primaryTextColor", primaryTextEdit->text()},
            {"mutedTextColor", mutedTextEdit->text()},
            {"sectionLabelColor", sectionLabelEdit->text()},
            {"logoPrimaryColor", logoPrimaryColorEdit->text()},
            {"logoSecondaryColor", logoSecondaryColorEdit->text()},
            {"appFontFamily", fontFamilyBox->currentFont().family()},
            {"appFontPointSize", fontSizeSpin->value()},
            {"logoFontPointSize", logoFontSizeSpin->value()},
            {"mediaBadgeFontPointSize", badgeFontSizeSpin->value()},
            {"metaFontPointSize", metaFontSizeSpin->value()},
            {"panelCornerRadius", panelRadiusSpin->value()},
            {"buttonCornerRadius", buttonRadiusSpin->value()}
        };
        root["keybinds"] = QJsonObject{
            {"playPause", playPauseKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"split", splitKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"deleteClip", deleteKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"replay", replayKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"forward", forwardKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"stepBack", stepBackKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"stepForward", stepForwardKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"undo", undoKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"redo", redoKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"exportGif", gifKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"exportAudio", audioKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"exportVideo", videoKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"exportMutedVideo", mutedVideoKeyEdit->keySequence().toString(QKeySequence::PortableText)},
            {"cycleAudioTrack", cycleAudioKeyEdit->keySequence().toString(QKeySequence::PortableText)}
        };
        root["export"] = QJsonObject{
            {"exportDirectory", exportDirEdit->text()},
            {"gifFps", gifFpsSpin->value()},
            {"gifWidth", gifWidthSpin->value()},
            {"audioBitrateKbps", audioBitrateSpin->value()},
            {"compressedAudioBitrateKbps", compressedAudioSpin->value()},
            {"videoCompressionThresholdMB", compressThresholdSpin->value()},
            {"targetCompressedSizeMB", targetSizeSpin->value()},
            {"fileNamePrefix", fileNamePrefixEdit->text()},
            {"includeSourceNameInExport", includeSourceNameCheck->isChecked()}
        };
        root["autoCut"] = QJsonObject{
            {"silenceThresholdDb", thresholdSpin->value()},
            {"minimumSilenceDurationSec", minSilenceSpin->value()},
            {"paddingSec", paddingSpin->value()},
            {"minimumClipDurationSec", minClipSpin->value()}
        };
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    navList->setCurrentRow(0);

    if (dialog.exec() != QDialog::Accepted) return;

    QStringList finalDirs;
    for(int i = 0; i < dirList->count(); ++i) {
        finalDirs << dirList->item(i)->text();
    }
    editorSettings.autoLoadDirectories = finalDirs;

    editorSettings.autoPlayOnImport = autoPlayCheck->isChecked();
    editorSettings.checkForUpdatesOnStartup = updateCheck->isChecked();
    editorSettings.defaultVolumePercent = defaultVolumeSpin->value();
    editorSettings.recentMediaLimit = recentMediaSpin->value();
    editorSettings.notificationDurationMs = notificationSpin->value();
    editorSettings.notificationPosition = notificationPositionBox->currentText();
    editorSettings.updateCheckDelayMs = updateDelaySpin->value();
    editorSettings.windowTitle = windowTitleEdit->text().trimmed().isEmpty() ? QString("Potato Editor") : windowTitleEdit->text().trimmed();
    editorSettings.logoPrimaryText = logoPrimaryEdit->text().trimmed().isEmpty() ? QString("POTATOES") : logoPrimaryEdit->text().trimmed();
    editorSettings.logoSecondaryText = logoSecondaryEdit->text().trimmed().isEmpty() ? QString("QUICK ONE") : logoSecondaryEdit->text().trimmed();
    editorSettings.importButtonText = importButtonEdit->text().trimmed().isEmpty() ? QString("IMPORT MEDIA") : importButtonEdit->text().trimmed();
    editorSettings.sidebarWidth = sidebarWidthSpin->value();
    editorSettings.sidebarPosition = sidebarPositionBox->currentText();
    editorSettings.toolButtonOrder = toolOrderEdit->text().trimmed().isEmpty() ? QString("blur,pixel,blackout,autocut,settings,resetcrop") : toolOrderEdit->text().trimmed();
    editorSettings.defaultCropTop = cropTopSpin->value();
    editorSettings.defaultCropBottom = cropBottomSpin->value();
    editorSettings.defaultCropLeft = cropLeftSpin->value();
    editorSettings.defaultCropRight = cropRightSpin->value();
    editorSettings.previewPlaceholderTitle = previewTitleEdit->text().trimmed().isEmpty() ? QString("Import media to start editing") : previewTitleEdit->text().trimmed();
    editorSettings.previewPlaceholderBody = previewBodyEdit->text().trimmed().isEmpty() ? QString("Video appears here. Audio-only files can still be trimmed, auto-cut, and exported.") : previewBodyEdit->text().trimmed();
    editorSettings.emptyTransportHint = emptyHintEdit->text().trimmed().isEmpty() ? QString("SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT") : emptyHintEdit->text().trimmed();
    editorSettings.videoTransportHint = videoHintEdit->text().trimmed().isEmpty() ? QString("SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT VIDEO") : videoHintEdit->text().trimmed();
    editorSettings.audioTransportHint = audioHintEdit->text().trimmed().isEmpty() ? QString("SPACE PLAY/PAUSE | S SPLIT | CTRL+SHIFT+C EXPORT AUDIO") : audioHintEdit->text().trimmed();
    editorSettings.timelineAccentColor = timelineAccentEdit->text().trimmed().isEmpty() ? QString("#FF875F") : timelineAccentEdit->text().trimmed();
    editorSettings.timelineSecondaryColor = timelineSecondaryEdit->text().trimmed().isEmpty() ? QString("#FF6B4A") : timelineSecondaryEdit->text().trimmed();
    editorSettings.timelineBackgroundColor = timelineBackgroundEdit->text().trimmed().isEmpty() ? QString("#14181D") : timelineBackgroundEdit->text().trimmed();
    editorSettings.timelineTrackColor = timelineTrackEdit->text().trimmed().isEmpty() ? QString("#272C34") : timelineTrackEdit->text().trimmed();
    editorSettings.timelineWaveformColor = timelineWaveformEdit->text().trimmed().isEmpty() ? QString("#7D5F56") : timelineWaveformEdit->text().trimmed();
    editorSettings.previewAccentColor = previewAccentEdit->text().trimmed().isEmpty() ? QString("#FF875F") : previewAccentEdit->text().trimmed();
    editorSettings.previewSecondaryColor = previewSecondaryEdit->text().trimmed().isEmpty() ? QString("#FF6B4A") : previewSecondaryEdit->text().trimmed();
    editorSettings.previewBackgroundColor = previewBackgroundEdit->text().trimmed().isEmpty() ? QString("#0F1115") : previewBackgroundEdit->text().trimmed();
    editorSettings.appBackgroundStartColor = appBackgroundStartEdit->text().trimmed().isEmpty() ? QString("#111317") : appBackgroundStartEdit->text().trimmed();
    editorSettings.appBackgroundEndColor = appBackgroundEndEdit->text().trimmed().isEmpty() ? QString("#0F1115") : appBackgroundEndEdit->text().trimmed();
    editorSettings.panelSurfaceColor = panelSurfaceEdit->text().trimmed().isEmpty() ? QString("#1A1D23") : panelSurfaceEdit->text().trimmed();
    editorSettings.panelAltSurfaceColor = panelAltSurfaceEdit->text().trimmed().isEmpty() ? QString("#1D2128") : panelAltSurfaceEdit->text().trimmed();
    editorSettings.controlSurfaceColor = controlSurfaceEdit->text().trimmed().isEmpty() ? QString("#22272F") : controlSurfaceEdit->text().trimmed();
    editorSettings.controlHoverColor = controlHoverEdit->text().trimmed().isEmpty() ? QString("#2D333D") : controlHoverEdit->text().trimmed();
    editorSettings.borderColor = borderEdit->text().trimmed().isEmpty() ? QString("#2A3038") : borderEdit->text().trimmed();
    editorSettings.primaryTextColor = primaryTextEdit->text().trimmed().isEmpty() ? QString("#F1F4F7") : primaryTextEdit->text().trimmed();
    editorSettings.mutedTextColor = mutedTextEdit->text().trimmed().isEmpty() ? QString("#C6CDD5") : mutedTextEdit->text().trimmed();
    editorSettings.sectionLabelColor = sectionLabelEdit->text().trimmed().isEmpty() ? QString("#D6DCE3") : sectionLabelEdit->text().trimmed();
    editorSettings.logoPrimaryColor = logoPrimaryColorEdit->text().trimmed().isEmpty() ? QString("#F4F6F8") : logoPrimaryColorEdit->text().trimmed();
    editorSettings.logoSecondaryColor = logoSecondaryColorEdit->text().trimmed().isEmpty() ? QString("#FF9B77") : logoSecondaryColorEdit->text().trimmed();
    editorSettings.appFontFamily = fontFamilyBox->currentFont().family();
    editorSettings.appFontPointSize = fontSizeSpin->value();
    editorSettings.logoFontPointSize = logoFontSizeSpin->value();
    editorSettings.mediaBadgeFontPointSize = badgeFontSizeSpin->value();
    editorSettings.metaFontPointSize = metaFontSizeSpin->value();
    editorSettings.panelCornerRadius = panelRadiusSpin->value();
    editorSettings.buttonCornerRadius = buttonRadiusSpin->value();
    editorSettings.keyPlayPause = playPauseKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keySplit = splitKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyDeleteClip = deleteKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyReplay = replayKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyForward = forwardKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyStepBack = stepBackKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyStepForward = stepForwardKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyUndo = undoKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyRedo = redoKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyExportGif = gifKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyExportAudio = audioKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyExportVideo = videoKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyExportMutedVideo = mutedVideoKeyEdit->keySequence().toString(QKeySequence::PortableText);
    editorSettings.keyCycleAudioTrack = cycleAudioKeyEdit->keySequence().toString(QKeySequence::PortableText);

    TimelineWidget::PlaybackSettings updatedPlayback = timeline->getPlaybackSettings();
    updatedPlayback.majorSeekMs = majorSeekSpin->value();
    updatedPlayback.minorSeekMs = minorSeekSpin->value();
    updatedPlayback.splitGuardMs = splitGuardSpin->value();
    updatedPlayback.minSegmentDurationMs = minSegmentSpin->value();
    timeline->setPlaybackSettings(updatedPlayback);

    TimelineWidget::ExportSettings updatedExport = timeline->getExportSettings();
    updatedExport.exportDirectory = exportDirEdit->text().trimmed().isEmpty() ? defaultExportDirectory() : exportDirEdit->text().trimmed();
    updatedExport.gifFps = gifFpsSpin->value();
    updatedExport.gifWidth = gifWidthSpin->value();
    updatedExport.audioBitrateKbps = audioBitrateSpin->value();
    updatedExport.compressedAudioBitrateKbps = compressedAudioSpin->value();
    updatedExport.videoCompressionThresholdMB = compressThresholdSpin->value();
    updatedExport.targetCompressedSizeMB = targetSizeSpin->value();
    updatedExport.fileNamePrefix = fileNamePrefixEdit->text().trimmed().isEmpty() ? QString("clip") : fileNamePrefixEdit->text().trimmed();
    updatedExport.includeSourceNameInExport = includeSourceNameCheck->isChecked();
    timeline->setExportSettings(updatedExport);

    TimelineWidget::AutoCutSettings updatedAutoCut = timeline->getAutoCutSettings();
    updatedAutoCut.silenceThresholdDb = thresholdSpin->value();
    updatedAutoCut.minimumSilenceDurationSec = minSilenceSpin->value();
    updatedAutoCut.paddingSec = paddingSpin->value();
    updatedAutoCut.minimumClipDurationSec = minClipSpin->value();
    timeline->setAutoCutSettings(updatedAutoCut);

    applyEditorSettings();
    saveEditorSettings();
    statusLabel->setText("SETTINGS SAVED");
}

QStringList MainWindow::collectRecentMediaFiles() const {
    if (!cachedRecentFiles.isEmpty()) return cachedRecentFiles;

    QList<QFileInfo> files;
    QStringList roots = {
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
    };
    roots.append(editorSettings.autoLoadDirectories);

    QStringList nameFilters;
    for (const QString &ext : MediaUtils::knownVideoExtensions()) {
        nameFilters << "*." + ext;
    }
    for (const QString &ext : MediaUtils::knownAudioExtensions()) {
        nameFilters << "*." + ext;
    }

    for (const QString &root : roots) {
        if (root.isEmpty() || !QDir(root).exists()) continue;
        QDir dir(root);
        files.append(dir.entryInfoList(nameFilters, QDir::Files));
    }

    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });

    QStringList uniqueFiles;
    QSet<QString> seen;
    for (const QFileInfo &info : files) {
        QString path = info.absoluteFilePath();
        if (seen.contains(path)) continue;
        seen.insert(path);
        uniqueFiles.append(path);
        if (uniqueFiles.size() >= qMax(1, editorSettings.recentMediaLimit)) break;
    }
    
    const_cast<MainWindow*>(this)->cachedRecentFiles = uniqueFiles;
    return uniqueFiles;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveEditorSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::refreshMediaState() {
    const bool hasMedia = !currentMediaPath.isEmpty();
    const bool hasVideo = hasMedia && timeline->sourceHasVideo();
    const bool hasAudio = hasMedia && timeline->sourceHasAudio();

    for (QPushButton *btn : {playPauseBtn, jumpBackBtn, stepBackBtn, stepFwdBtn, jumpFwdBtn,
                             undoBtn, redoBtn, splitBtn, deleteClipBtn}) {
        btn->setEnabled(hasMedia);
    }
    speedBox->setEnabled(hasMedia);
    muteBtn->setEnabled(hasAudio);
    volSlider->setEnabled(hasAudio);
    snapshotBtn->setEnabled(hasVideo);
    exportBtn->setEnabled(hasMedia);
    exportVideoAction->setEnabled(hasVideo);
    exportMutedAction->setEnabled(hasVideo);
    exportGifAction->setEnabled(hasVideo);
    exportAudioAction->setEnabled(hasAudio);
    textBtn->setEnabled(hasVideo);
    blurBtn->setEnabled(hasVideo);
    pixelBtn->setEnabled(hasVideo);
    solidBtn->setEnabled(hasVideo);
    resetCropBtn->setEnabled(hasVideo);
    fullscreenBtn->setEnabled(hasVideo);
    autoCutBtn->setEnabled(hasAudio);

    statusLabel->clear();
    statusLabel->hide();
    transportHintLabel->hide();

    if (!hasMedia) {
        currentMediaLabel->setText("NO MEDIA LOADED");
        videoWithCrop->setPlaceholderState(editorSettings.previewPlaceholderTitle, editorSettings.previewPlaceholderBody);
        updateSidebar();
        updateTimecodeDisplay();
        updateTimelineChips();
        return;
    }

    const QFileInfo info(currentMediaPath);
    currentMediaLabel->setText(QString("%1  ·  %2")
                                   .arg(info.fileName().toUpper(), buildMediaBadgeText(hasVideo, hasAudio)));

    if (hasVideo) {
        videoWithCrop->setPlaceholderState("Loading preview...", "The first visible frame will appear here.");
    } else {
        videoWithCrop->setPlaceholderState("Audio-only file loaded",
                                           "Trim, auto-cut silence, and export audio from any supported audio format.");
    }

    updateSidebar();
    updateTimelineChips();
}

void MainWindow::updateSidebar() {
    const QStringList recentFiles = collectRecentMediaFiles();
    sidebarCountLabel->setText(QString("%1 ITEMS").arg(recentFiles.size()));
    sidebarEmptyLabel->setVisible(recentFiles.isEmpty());
    sidebarScroll->setVisible(!recentFiles.isEmpty());

    // Reuse existing widgets if possible
    int existingCount = sidebarListLayout->count();
    // Remove the stretch at the end if it exists
    if (existingCount > 0) {
        QLayoutItem* lastItem = sidebarListLayout->itemAt(existingCount - 1);
        if (lastItem->spacerItem()) {
            sidebarListLayout->removeItem(lastItem);
            delete lastItem;
            existingCount--;
        }
    }

    // Update or create widgets
    for (int i = 0; i < recentFiles.size(); ++i) {
        const QString &filePath = recentFiles[i];
        const QFileInfo info(filePath);
        const bool isActive = info.absoluteFilePath() == currentMediaPath;
        
        QWidget* clipContainer = nullptr;
        if (i < existingCount) {
            clipContainer = sidebarListLayout->itemAt(i)->widget();
        }

        if (!clipContainer) {
            clipContainer = new QWidget();
            clipContainer->setObjectName("SidebarItem");
            
            QHBoxLayout *rowContentLayout = new QHBoxLayout(clipContainer);
            rowContentLayout->setContentsMargins(8, 8, 8, 8);
            rowContentLayout->setSpacing(8);

            PreviewLabel *preview = new PreviewLabel(info.absoluteFilePath(), clipContainer);
            preview->setObjectName("SidebarPreview");
            preview->setFixedSize(76, 44);

            QWidget *textInfo = new QWidget(clipContainer);
            textInfo->setAttribute(Qt::WA_TranslucentBackground);
            QVBoxLayout *textLayout = new QVBoxLayout(textInfo);
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(1);

            QLabel *nameLabel = new QLabel();
            nameLabel->setObjectName("SidebarTitle");

            QLabel *metaLabel = new QLabel();
            metaLabel->setObjectName("MetaData");

            textLayout->addWidget(nameLabel);
            textLayout->addWidget(metaLabel);
            textLayout->addStretch();

            rowContentLayout->addWidget(preview);
            rowContentLayout->addWidget(textInfo, 1);

            clipContainer->setCursor(Qt::PointingHandCursor);
            clipContainer->installEventFilter(this);
            sidebarListLayout->addWidget(clipContainer);
        }

        // Update properties and text
        clipContainer->setProperty("active", isActive);
        clipContainer->setProperty("filePath", info.absoluteFilePath());

        // Recycled rows must show the file they now represent, not the one
        // they were created for (this is what made selection look broken).
        if (PreviewLabel* preview = clipContainer->findChild<PreviewLabel*>("SidebarPreview")) {
            preview->setSource(info.absoluteFilePath());
        }

        QLabel* nameLabel = clipContainer->findChild<QLabel*>("SidebarTitle");
        if (nameLabel) {
            QFontMetrics metrics(nameLabel->font());
            nameLabel->setText(metrics.elidedText(info.completeBaseName().toUpper(), Qt::ElideRight, 108));
        }

        QLabel* metaLabel = clipContainer->findChild<QLabel*>("MetaData");
        if (metaLabel) {
            QString sizeStr = QString::number(info.size() / (1024 * 1024.0), 'f', 1) + "MB";
            const QString typeText = MediaUtils::isKnownAudioFile(info.absoluteFilePath()) ? "AUDIO" : "VIDEO";
            metaLabel->setText(QString("%1 | %2").arg(typeText, sizeStr));
        }

        // Style updates for property changes
        clipContainer->style()->unpolish(clipContainer);
        clipContainer->style()->polish(clipContainer);
    }

    // Remove excess widgets
    while (sidebarListLayout->count() > recentFiles.size()) {
        QLayoutItem* item = sidebarListLayout->takeAt(recentFiles.size());
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    sidebarListLayout->addStretch(1);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    // Check if the user clicked one of the recent clip containers
    if (event->type() == QEvent::MouseButtonPress) {
        QString path = obj->property("filePath").toString();
        if (!path.isEmpty()) {
            loadClipDirectly(path);
            return true;
        }
    }

    // Global shortcut routing: keys like S (split) or Space work no matter
    // which panel has focus — but never while the user is typing in a field
    // or interacting with a dialog.
    if (event->type() == QEvent::KeyPress && timeline && !currentMediaPath.isEmpty()) {
        QWidget *focus = QApplication::focusWidget();
        const bool typing = qobject_cast<QLineEdit*>(focus) ||
                            qobject_cast<QAbstractSpinBox*>(focus) ||
                            qobject_cast<QKeySequenceEdit*>(focus) ||
                            qobject_cast<QComboBox*>(focus) ||
                            (focus && focus->inherits("QTextEdit")) ||
                            (focus && focus->inherits("QPlainTextEdit"));
        const bool dialogOpen = QApplication::activeModalWidget() != nullptr;
        const bool timelineHasFocus = focus == timeline; // it handles its own keys

        if (!typing && !dialogOpen && !timelineHasFocus &&
            QApplication::activeWindow() == this) {
            auto *keyEvent = static_cast<QKeyEvent*>(event);
            if (timeline->handleGlobalKey(keyEvent)) return true;
        }
    }

    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::loadInitialVideo() {
    refreshMediaState();
    QTimer::singleShot(0, this, [this]() {
        if (!currentMediaPath.isEmpty()) return;
        const QStringList recentFiles = collectRecentMediaFiles();
        for (const QString &path : recentFiles) {
            if (MediaUtils::isKnownVideoFile(path)) {
                loadClipDirectly(path);
                return;
            }
        }
    });
}
