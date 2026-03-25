#include "../Includes/mainWindow.h"
#include <iostream>
#include "../Includes/resizeFilter.h"
#include "../Includes/dropFilter.h"
#include "../Includes/timelinewidget.h"
#include "../Includes/previewLabel.h"
#include "../Includes/mediautils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDir>
#include <QStyle>
#include <QFileInfo>
#include <QShortcut>
#include <QLineEdit>
#include <QApplication>
#include <QGraphicsBlurEffect>
#include <QStandardPaths>
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
#include <algorithm>

namespace {

QString buildMediaBadgeText(bool hasVideo, bool hasAudio) {
    if (hasVideo && hasAudio) return "VIDEO + AUDIO";
    if (hasVideo) return "VIDEO ONLY";
    if (hasAudio) return "AUDIO ONLY";
    return "MEDIA";
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    this->setAcceptDrops(true);
    DropFilter* filter = new DropFilter([this](const QString &path) {
        loadClipDirectly(path);
    }, this);
    qApp->installEventFilter(filter);
    setupConnections();
    loadInitialVideo();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setAcceptDrops(true);
    centralWidget->setObjectName("centralWidget");
    setCentralWidget(centralWidget);
    setObjectName("MainCanvas");
    setWindowTitle("Potato Editor");

    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(16);

    toolbar = new QFrame();
    toolbar->setObjectName("toolbar");
    toolbar->setMinimumHeight(88);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(22, 18, 22, 18);
    toolbarLayout->setSpacing(18);

    auto* logoContainer = new QWidget();
    auto* logoLayout = new QHBoxLayout(logoContainer);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(8);
    auto* logoBold = new QLabel("POTATOES");
    logoBold->setObjectName("LogoBold");
    auto* logoLight = new QLabel("QUICK ONE");
    logoLight->setObjectName("LogoLight");
    logoLayout->addWidget(logoBold);
    logoLayout->addWidget(logoLight);

    auto* titleStack = new QWidget();
    auto* titleStackLayout = new QVBoxLayout(titleStack);
    titleStackLayout->setContentsMargins(0, 0, 0, 0);
    titleStackLayout->setSpacing(4);
    titleStackLayout->addWidget(logoContainer);

    currentMediaLabel = new QLabel("NO MEDIA LOADED");
    currentMediaLabel->setObjectName("CurrentMediaPill");
    titleStackLayout->addWidget(currentMediaLabel, 0, Qt::AlignLeft);

    auto* inputLabel = new QLabel("EXPORT NAME");
    inputLabel->setObjectName("InputLabel");
    exportInput = new QLineEdit();
    exportInput->setObjectName("ExportNameInput");
    exportInput->setPlaceholderText("leave blank to auto-name exports");
    exportInput->setMinimumWidth(280);

    auto* exportField = new QWidget();
    auto* exportFieldLayout = new QVBoxLayout(exportField);
    exportFieldLayout->setContentsMargins(0, 0, 0, 0);
    exportFieldLayout->setSpacing(6);
    exportFieldLayout->addWidget(inputLabel);
    exportFieldLayout->addWidget(exportInput);

    importBtn = new QPushButton("IMPORT MEDIA");
    importBtn->setObjectName("PrimaryGhostBtn");
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setMinimumHeight(44);

    toolbarLayout->addWidget(titleStack, 1);
    toolbarLayout->addWidget(exportField);
    toolbarLayout->addWidget(importBtn);
    mainLayout->addWidget(toolbar);

    workspace = new QFrame();
    workspace->setObjectName("workspace");
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(22, 22, 22, 22);
    workspaceLayout->setSpacing(16);

    auto* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(16);
    stageColumnLayout = nullptr;
    previewHeader = nullptr;
    videoContainer = nullptr;
    videoFullscreenPlaceholder = nullptr;
    videoFullscreenDialog = nullptr;

    clipSidebar = new QFrame();
    clipSidebar->setFixedWidth(260);
    clipSidebar->setObjectName("clipSidebar");

    auto* sidebarLayout = new QVBoxLayout(clipSidebar);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(12);

    auto* sideHeader = new QHBoxLayout();
    auto* sideTitle = new QLabel("RECENT MEDIA");
    sideTitle->setObjectName("SectionLabel");
    sidebarCountLabel = new QLabel("0 ITEMS");
    sidebarCountLabel->setObjectName("MiniBadge");
    sideHeader->addWidget(sideTitle);
    sideHeader->addStretch();
    sideHeader->addWidget(sidebarCountLabel);

    sidebarEmptyLabel = new QLabel("Recent media from your Movies and Music folders will appear here.");
    sidebarEmptyLabel->setObjectName("EmptyStateLabel");
    sidebarEmptyLabel->setWordWrap(true);

    sidebarScroll = new QScrollArea();
    sidebarScroll->setWidgetResizable(true);
    sidebarScroll->setFrameShape(QFrame::NoFrame);
    sidebarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    sidebarContent = new QWidget();
    sidebarListLayout = new QVBoxLayout(sidebarContent);
    sidebarListLayout->setContentsMargins(0, 0, 0, 0);
    sidebarListLayout->setSpacing(10);

    sidebarScroll->setWidget(sidebarContent);
    sidebarLayout->addLayout(sideHeader);
    sidebarLayout->addWidget(sidebarEmptyLabel);
    sidebarLayout->addWidget(sidebarScroll, 1);

    stageColumnLayout = new QVBoxLayout();
    stageColumnLayout->setSpacing(14);

    previewHeader = new QFrame();
    previewHeader->setObjectName("PanelHeader");
    auto* previewHeaderLayout = new QHBoxLayout(previewHeader);
    previewHeaderLayout->setContentsMargins(16, 12, 16, 12);

    auto* previewTitle = new QLabel("PREVIEW");
    previewTitle->setObjectName("SectionLabel");
    transportHintLabel = new QLabel("SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT");
    transportHintLabel->setObjectName("SubtleHint");
    previewHeaderLayout->addWidget(previewTitle);
    previewHeaderLayout->addStretch();
    previewHeaderLayout->addWidget(transportHintLabel, 0, Qt::AlignRight);

    videoContainer = new QFrame();
    videoContainer->setObjectName("VideoContainer");
    videoContainer->setMinimumHeight(400);
    auto* videoLayout = new QGridLayout(videoContainer);
    videoLayout->setContentsMargins(12, 12, 12, 12);

    videoWithCrop = new VideoWithCropWidget(videoContainer);
    videoWithCrop->setObjectName("VideoSurface");
    videoWithCrop->setPlaceholderState("Import media to start editing",
                                       "Video appears here. Audio-only files can still be trimmed, auto-cut, and exported.");
    videoLayout->addWidget(videoWithCrop, 0, 0);

    fullscreenBtn = new QPushButton("⛶");
    fullscreenBtn->setFixedSize(40, 40);
    fullscreenBtn->setCursor(Qt::PointingHandCursor);
    fullscreenBtn->setObjectName("FullscreenBtn");
    videoLayout->addWidget(fullscreenBtn, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    stageColumnLayout->addWidget(previewHeader);
    stageColumnLayout->addWidget(videoContainer, 1);

    contentLayout->addWidget(clipSidebar);
    contentLayout->addLayout(stageColumnLayout, 1);
    workspaceLayout->addLayout(contentLayout, 1);

    timelineTools = new QWidget();
    timelineTools->setObjectName("timelineTools");
    auto* toolsLayout = new QHBoxLayout(timelineTools);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(10);

    auto* toolLabel = new QLabel("TOOLS");
    toolLabel->setObjectName("SectionLabel");
    toolsLayout->addWidget(toolLabel);

    autoCutBtn = new QPushButton("AUTO-CUT SILENCE");
    autoCutBtn->setProperty("class", "ToolBtn");
    autoCutBtn->setMinimumHeight(34);

    resetCropBtn = new QPushButton("RESET CROP");
    resetCropBtn->setProperty("class", "ToolBtn");
    resetCropBtn->setMinimumHeight(34);

    blurBtn = new QPushButton("SPAWN BLUR");
    blurBtn->setProperty("class", "ToolBtn");
    blurBtn->setMinimumHeight(34);

    pixelBtn = new QPushButton("SPAWN PIXEL");
    pixelBtn->setProperty("class", "ToolBtn");
    pixelBtn->setMinimumHeight(34);

    solidBtn = new QPushButton("SPAWN BLACKOUT");
    solidBtn->setProperty("class", "ToolBtn");
    solidBtn->setMinimumHeight(34);

    toolsLayout->addWidget(blurBtn);
    toolsLayout->addWidget(pixelBtn);
    toolsLayout->addWidget(solidBtn);
    toolsLayout->addSpacing(12);
    toolsLayout->addWidget(autoCutBtn);
    toolsLayout->addWidget(resetCropBtn);
    toolsLayout->addStretch();

    timeline = new TimelineWidget(this);
    timeline->setMinimumHeight(200);
    timeline->cropTop = 0.03f;
    timeline->cropBottom = 0.96f;

    workspaceLayout->addWidget(timelineTools);
    workspaceLayout->addWidget(timeline);
    mainLayout->addWidget(workspace);

    footer = new QFrame();
    footer->setObjectName("footer");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(22, 16, 22, 16);
    footerLayout->setSpacing(16);

    auto* versionLabel = new QLabel(CURRENT_VERSION);
    versionLabel->setObjectName("VersionLabel");

    volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100);
    volSlider->setValue(80);
    volSlider->setFixedWidth(140);

    playPauseBtn = new QPushButton("PLAY");
    playPauseBtn->setObjectName("ActionBtn");
    playPauseBtn->setFixedSize(140, 44);

    statusLabel = new QLabel("READY FOR MEDIA");
    statusLabel->setObjectName("MetaData");

    auto* volumeLabel = new QLabel("VOLUME");
    volumeLabel->setObjectName("InputLabel");

    footerLayout->addWidget(versionLabel);
    footerLayout->addWidget(volumeLabel);
    footerLayout->addWidget(volSlider);
    footerLayout->addStretch();
    footerLayout->addWidget(playPauseBtn);
    footerLayout->addStretch();
    footerLayout->addWidget(statusLabel);
    mainLayout->addWidget(footer);

    player = new QMediaPlayer(this);
    audio = new QAudioOutput(this);
    player->setAudioOutput(audio);
    player->setVideoSink(videoWithCrop->sink);
    audio->setVolume(0.8);

    connect(importBtn, &QPushButton::clicked, this, &MainWindow::importMedia);
}

void MainWindow::setupConnections() {
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
        timeline->currentPosMs = pos;
        updateVolume();
        if (player->playbackState() == QMediaPlayer::PlayingState) {
            timeline->validatePlayheadPosition();
            if (timeline->currentPosMs != pos) player->setPosition(timeline->currentPosMs);
            if (timeline->currentPosMs >= timeline->getEndLimit()) player->setPosition(timeline->getStartLimit());
        }
        timeline->update();
    });

    connect(blurBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(0); });
    connect(pixelBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(1); });
    connect(solidBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(2); });

    connect(player, &QMediaPlayer::durationChanged, [this](qint64 d) {
        if (d > 0) {
            QApplication::processEvents();
            QTimer::singleShot(100, this, [this, d]() {
                timeline->setDuration(d);
                timeline->updateGeometry();
                timeline->forceFitToDuration();
                player->setPosition(0);
                player->play();
                refreshMediaState();
                timeline->update();
            });
        }
    });

    connect(timeline, &TimelineWidget::requestAudioTrackChange, [this](int trackIndex) {
        player->setActiveAudioTrack(trackIndex);
    });

    connect(videoWithCrop, &VideoWithCropWidget::cropsChanged, timeline, &TimelineWidget::updateCropValues);
    connect(timeline, &TimelineWidget::clipTrimmed, [this]() {
        videoWithCrop->cropT = timeline->cropTop; videoWithCrop->cropB = timeline->cropBottom;
        videoWithCrop->cropL = timeline->cropLeft; videoWithCrop->cropR = timeline->cropRight;
        videoWithCrop->update();
    });

    connect(timeline, &TimelineWidget::playheadMoved, player, &QMediaPlayer::setPosition);
    connect(autoCutBtn, &QPushButton::clicked, [this]() { timeline->autoCutSilence(); });
    connect(resetCropBtn, &QPushButton::clicked, [this]() {
        videoWithCrop->cropT = 0.03f; videoWithCrop->cropB = 0.96f;
        videoWithCrop->cropL = 0.0f; videoWithCrop->cropR = 1.0f;
        videoWithCrop->filterObjects.clear();
        timeline->cropTop = 0.03f; timeline->cropBottom = 0.96f;
        videoWithCrop->update();
        timeline->update();
    });

    auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, [this]() {
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
    fullscreenBtn->setText("❐");

    videoFullscreenPlaceholder = new QWidget(workspace);
    videoFullscreenPlaceholder->setMinimumSize(videoContainer->size());
    const int videoIndex = stageColumnLayout->indexOf(videoContainer);
    if (videoIndex >= 0) {
        stageColumnLayout->insertWidget(videoIndex, videoFullscreenPlaceholder, 1);
    }

    stageColumnLayout->removeWidget(videoContainer);
    videoContainer->setParent(nullptr);

    videoFullscreenDialog = new QDialog(this, Qt::FramelessWindowHint);
    videoFullscreenDialog->setModal(false);
    videoFullscreenDialog->setObjectName("VideoFullscreenDialog");
    videoFullscreenDialog->setStyleSheet("QDialog#VideoFullscreenDialog { background-color: #000000; }");

    auto *layout = new QVBoxLayout(videoFullscreenDialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(videoContainer, 1);

    connect(videoFullscreenDialog, &QDialog::finished, this, [this](int) {
        restoreVideoFromFullscreen();
    });

    videoFullscreenDialog->showFullScreen();
}

void MainWindow::restoreVideoFromFullscreen() {
    if (!isVideoFullscreen || !stageColumnLayout || !videoContainer) return;

    isVideoFullscreen = false;
    fullscreenBtn->setText("⛶");

    if (videoFullscreenDialog) {
        videoFullscreenDialog->blockSignals(true);
        videoFullscreenDialog->hide();
    }

    if (videoContainer->parentWidget() != nullptr) {
        videoContainer->setParent(nullptr);
    }

    const int placeholderIndex = videoFullscreenPlaceholder ? stageColumnLayout->indexOf(videoFullscreenPlaceholder) : -1;
    if (placeholderIndex >= 0) {
        stageColumnLayout->insertWidget(placeholderIndex, videoContainer, 1);
        stageColumnLayout->removeWidget(videoFullscreenPlaceholder);
    } else {
        stageColumnLayout->addWidget(videoContainer, 1);
    }

    if (videoFullscreenPlaceholder) {
        videoFullscreenPlaceholder->deleteLater();
        videoFullscreenPlaceholder = nullptr;
    }

    if (videoFullscreenDialog) {
        videoFullscreenDialog->deleteLater();
        videoFullscreenDialog = nullptr;
    }

    videoContainer->show();
    videoContainer->updateGeometry();
}

void MainWindow::handlePlaybackState(QMediaPlayer::PlaybackState state) {
    playPauseBtn->setText(state == QMediaPlayer::PlayingState ? "PAUSE" : "PLAY");
    playPauseBtn->setProperty("playing", state == QMediaPlayer::PlayingState);
    playPauseBtn->style()->unpolish(playPauseBtn);
    playPauseBtn->style()->polish(playPauseBtn);
}

void MainWindow::updateVolume() {
    // 1. Get the current clip-specific gain from the timeline
    float segmentGain = 1.0f;

    // We need a way to ask the timeline: "What is the gain at this exact millisecond?"
    // Let's assume we add a helper 'getGainAtPos' to TimelineWidget
    segmentGain = timeline->getGainAtPos(player->position());

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

    currentMediaPath = filePath;
    videoWithCrop->lastFrame = QImage();
    videoWithCrop->filterObjects.clear();
    videoWithCrop->selectedFilterIdx = -1;
    videoWithCrop->adjustingFilter = false;
    videoWithCrop->setPlaceholderState("Loading media...", "Preparing timeline, waveform, and preview.");
    videoWithCrop->update();

    player->setSource(QUrl::fromLocalFile(filePath));
    timeline->setMediaSource(QUrl::fromLocalFile(filePath));
    player->play();
    refreshMediaState();
}

QStringList MainWindow::collectRecentMediaFiles() const {
    QList<QFileInfo> files;
    const QStringList roots = {
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
    };

    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        QDir dir(root);
        const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Time);
        for (const QFileInfo &entry : entries) {
            if (MediaUtils::isSupportedMediaFile(entry.absoluteFilePath())) {
                files.append(entry);
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });

    QStringList uniqueFiles;
    QSet<QString> seen;
    for (const QFileInfo &info : files) {
        if (seen.contains(info.absoluteFilePath())) continue;
        seen.insert(info.absoluteFilePath());
        uniqueFiles.append(info.absoluteFilePath());
        if (uniqueFiles.size() >= 8) break;
    }
    return uniqueFiles;
}

void MainWindow::refreshMediaState() {
    if (currentMediaPath.isEmpty()) {
        currentMediaLabel->setText("NO MEDIA LOADED");
        statusLabel->setText("READY FOR MEDIA");
        transportHintLabel->setText("SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT");
        blurBtn->setEnabled(false);
        pixelBtn->setEnabled(false);
        solidBtn->setEnabled(false);
        resetCropBtn->setEnabled(false);
        autoCutBtn->setEnabled(false);
        fullscreenBtn->setEnabled(false);
        updateSidebar();
        return;
    }

    const QFileInfo info(currentMediaPath);
    const bool hasVideo = timeline->sourceHasVideo();
    const bool hasAudio = timeline->sourceHasAudio();

    currentMediaLabel->setText(QString("%1 - %2")
                                   .arg(info.fileName().toUpper(), buildMediaBadgeText(hasVideo, hasAudio)));
    statusLabel->setText(QString("%1 READY").arg(buildMediaBadgeText(hasVideo, hasAudio)));
    transportHintLabel->setText(hasVideo
        ? "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT VIDEO"
        : "SPACE PLAY/PAUSE | S SPLIT | CTRL+SHIFT+C EXPORT AUDIO");

    blurBtn->setEnabled(hasVideo);
    pixelBtn->setEnabled(hasVideo);
    solidBtn->setEnabled(hasVideo);
    resetCropBtn->setEnabled(hasVideo);
    fullscreenBtn->setEnabled(hasVideo);
    autoCutBtn->setEnabled(hasAudio);

    if (hasVideo) {
        videoWithCrop->setPlaceholderState("Loading preview...", "The first visible frame will appear here.");
    } else {
        videoWithCrop->setPlaceholderState("Audio-only file loaded",
                                           "Trim, auto-cut silence, and export audio from any supported audio format.");
    }

    updateSidebar();
}

void MainWindow::updateSidebar() {
    QLayoutItem *child;
    while ((child = sidebarListLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    const QStringList recentFiles = collectRecentMediaFiles();
    sidebarCountLabel->setText(QString("%1 ITEMS").arg(recentFiles.size()));
    sidebarEmptyLabel->setVisible(recentFiles.isEmpty());
    sidebarScroll->setVisible(!recentFiles.isEmpty());

    for (const QString &filePath : recentFiles) {
        const QFileInfo info(filePath);
        QWidget *clipContainer = new QWidget();
        clipContainer->setObjectName("SidebarItem");
        clipContainer->setProperty("active", info.absoluteFilePath() == currentMediaPath);

        QHBoxLayout *rowLayout = new QHBoxLayout(clipContainer);
        rowLayout->setContentsMargins(10, 10, 10, 10);
        rowLayout->setSpacing(10);

        PreviewLabel *preview = new PreviewLabel(info.absoluteFilePath(), clipContainer);
        preview->setFixedSize(92, 56);

        QWidget *textInfo = new QWidget();
        textInfo->setAttribute(Qt::WA_TranslucentBackground);
        QVBoxLayout *textLayout = new QVBoxLayout(textInfo);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(2);

        QLabel *nameLabel = new QLabel();
        nameLabel->setObjectName("SidebarTitle");
        QFontMetrics metrics(nameLabel->font());
        QString elidedName = metrics.elidedText(info.completeBaseName().toUpper(), Qt::ElideRight, 120);
        nameLabel->setText(elidedName);

        QString sizeStr = QString::number(info.size() / (1024 * 1024.0), 'f', 1) + "MB";
        const QString typeText = MediaUtils::isKnownAudioFile(info.absoluteFilePath()) ? "AUDIO" : "VIDEO";
        QLabel *metaLabel = new QLabel(QString("%1 | %2").arg(typeText, sizeStr));
        metaLabel->setObjectName("MetaData");

        textLayout->addWidget(nameLabel);
        textLayout->addWidget(metaLabel);
        textLayout->addStretch();

        rowLayout->addWidget(preview);
        rowLayout->addWidget(textInfo, 1);

        clipContainer->setCursor(Qt::PointingHandCursor);
        clipContainer->installEventFilter(this);
        clipContainer->setProperty("filePath", info.absoluteFilePath());

        sidebarListLayout->addWidget(clipContainer);
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
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::loadInitialVideo() {
    // Keep startup responsive by showing the empty editor first instead of
    // probing and decoding the newest media file during window construction.
    refreshMediaState();
}
