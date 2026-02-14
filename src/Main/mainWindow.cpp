#include "../Includes/mainWindow.h"
#include <iostream>
#include "../Includes/resizeFilter.h"
#include "../Includes/dropFilter.h"
#include "../Includes/timelinewidget.h"
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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    this->setAcceptDrops(true);
    DropFilter* filter = new DropFilter(player, timeline, statusLabel);
    qApp->installEventFilter(filter);
    setupConnections();
    checkForUpdates();
    loadInitialVideo();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setAcceptDrops(true);
    centralWidget->setObjectName("centralWidget");
    setCentralWidget(centralWidget);

    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    toolbar = new QFrame();
    toolbar->setFixedHeight(60);
    toolbar->setObjectName("toolbar");
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(30, 0, 30, 0);

    auto* logoContainer = new QWidget();
    auto* logoLayout = new QHBoxLayout(logoContainer);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(5);
    auto* logoBold = new QLabel("POTATOES");
    logoBold->setObjectName("LogoBold");
    auto* logoLight = new QLabel("QUICK ONE");
    logoLight->setObjectName("LogoLight");
    logoLayout->addWidget(logoBold);
    logoLayout->addWidget(logoLight);

    auto* inputLabel = new QLabel("CLIP NAME:");
    inputLabel->setObjectName("InputLabel");
    exportInput = new QLineEdit();
    exportInput->setObjectName("ExportNameInput");
    exportInput->setPlaceholderText("NAME YOUR CLIP...");
    exportInput->setFixedWidth(250);

    auto* openBtn = new QPushButton("IMPORT MEDIA");
    openBtn->setCursor(Qt::PointingHandCursor);

    toolbarLayout->addWidget(logoContainer);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(inputLabel);
    toolbarLayout->addWidget(exportInput);
    toolbarLayout->addSpacing(20);
    toolbarLayout->addWidget(openBtn);
    mainLayout->addWidget(toolbar);

    workspace = new QFrame();
    workspace->setObjectName("workspace");
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(25, 5, 25, 10);

    auto* videoContainer = new QFrame();
    videoContainer->setObjectName("VideoContainer");
    videoContainer->setMinimumHeight(400);
    auto* videoLayout = new QGridLayout(videoContainer);
    videoLayout->setContentsMargins(0, 0, 0, 0);

    videoWithCrop = new VideoWithCropWidget(videoContainer);
    videoContainer->installEventFilter(new ResizeFilter(videoWithCrop));
    videoLayout->addWidget(videoWithCrop, 0, 0);

    fullscreenBtn = new QPushButton("⛶");
    fullscreenBtn->setFixedSize(40, 40);
    fullscreenBtn->setCursor(Qt::PointingHandCursor);
    fullscreenBtn->setObjectName("FullscreenBtn");
    fullscreenBtn->setStyleSheet("QPushButton#FullscreenBtn { background-color: rgba(0, 0, 0, 150); color: white; border-radius: 5px; font-size: 20px; margin: 15px; } QPushButton#FullscreenBtn:hover { background-color: #ff3c00; }");
    videoLayout->addWidget(fullscreenBtn, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    timelineTools = new QWidget();
    auto* toolsLayout = new QHBoxLayout(timelineTools);
    toolsLayout->setContentsMargins(0, 10, 0, 5);

    auto* autoCutBtn = new QPushButton("AUTO-CUT SILENCE");
    autoCutBtn->setProperty("class", "ToolBtn");
    autoCutBtn->setFixedSize(160, 30);

    auto* resetCropBtn = new QPushButton("RESET CROP");
    resetCropBtn->setProperty("class", "ToolBtn");
    resetCropBtn->setFixedSize(100, 30);

    blurBtn = new QPushButton("SPAWN BLUR");
    blurBtn->setProperty("class", "ToolBtn");
    blurBtn->setFixedSize(120, 30);

    pixelBtn = new QPushButton("SPAWN PIXEL");
    pixelBtn->setProperty("class", "ToolBtn");
    pixelBtn->setFixedSize(120, 30);

    solidBtn = new QPushButton("SPAWN BLACKOUT");
    solidBtn->setProperty("class", "ToolBtn");
    solidBtn->setFixedSize(140, 30);

    toolsLayout->addWidget(blurBtn);
    toolsLayout->addWidget(pixelBtn);
    toolsLayout->addWidget(solidBtn);
    toolsLayout->addSpacing(20);
    toolsLayout->addWidget(autoCutBtn);
    toolsLayout->addWidget(resetCropBtn);
    toolsLayout->addStretch();

    timeline = new TimelineWidget(this);
    timeline->setFixedHeight(180);
    timeline->cropTop = 0.03f;
    timeline->cropBottom = 0.96f;

    workspaceLayout->addWidget(videoContainer, 1);
    workspaceLayout->addWidget(timelineTools);
    workspaceLayout->addWidget(timeline);
    mainLayout->addWidget(workspace);

    footer = new QFrame();
    footer->setObjectName("footer");
    footer->setFixedHeight(100);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(30, 0, 30, 20);

    auto* versionLabel = new QLabel(CURRENT_VERSION);
    versionLabel->setObjectName("VersionLabel");
    versionLabel->setStyleSheet("color: rgba(255, 255, 255, 0.3); font-size: 10px; font-weight: bold;");

    volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100);
    volSlider->setValue(80);
    volSlider->setFixedWidth(110);

    playPauseBtn = new QPushButton("PLAY");
    playPauseBtn->setObjectName("ActionBtn");
    playPauseBtn->setFixedSize(140, 44);

    statusLabel = new QLabel("READY");
    statusLabel->setObjectName("MetaData");

    footerLayout->addWidget(versionLabel, 0, Qt::AlignBottom);
    footerLayout->addSpacing(15);
    footerLayout->addWidget(new QLabel("VOL"));
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

    this->setProperty("autoCutBtn", QVariant::fromValue((void*)autoCutBtn));
    this->setProperty("resetBtn", QVariant::fromValue((void*)resetCropBtn));
    this->setProperty("exportInput", QVariant::fromValue((void*)exportInput));
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::importMedia);
}

void MainWindow::setupConnections() {
    connect(volSlider, &QSlider::valueChanged, this, &MainWindow::updateVolume);
    connect(timeline, &TimelineWidget::audioGainChanged, this, &MainWindow::updateVolume);
    connect(player, &QMediaPlayer::playbackStateChanged, this, &MainWindow::handlePlaybackState);

    connect(fullscreenBtn, &QPushButton::clicked, [this]() {
        isVideoFullscreen = !isVideoFullscreen;
        toolbar->setVisible(!isVideoFullscreen);
        footer->setVisible(!isVideoFullscreen);
        timelineTools->setVisible(!isVideoFullscreen);
        timeline->setVisible(!isVideoFullscreen);
        if (isVideoFullscreen) {
            workspace->layout()->setContentsMargins(0, 0, 0, 0);
            this->showFullScreen();
            fullscreenBtn->setText("❐");
        } else {
            workspace->layout()->setContentsMargins(25, 5, 25, 10);
            this->showNormal();
            fullscreenBtn->setText("⛶");
        }
    });

    connect(playPauseBtn, &QPushButton::clicked, [this]() {
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });

    connect(player, &QMediaPlayer::positionChanged, [this](qint64 pos) {
        timeline->currentPosMs = pos;
        if (player->playbackState() == QMediaPlayer::PlayingState) {
            timeline->validatePlayheadPosition();
            if (timeline->currentPosMs != pos) player->setPosition(timeline->currentPosMs);
            if (timeline->currentPosMs >= timeline->getEndLimit()) player->setPosition(timeline->getStartLimit());
        }
        timeline->update();
    });

    // Spawning logic
    connect(blurBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(0); });
    connect(pixelBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(1); });
    connect(solidBtn, &QPushButton::clicked, [this]() { videoWithCrop->addFilter(2); });

    // Multi-filter export sync
    connect(videoWithCrop, &VideoWithCropWidget::filtersChanged, [this](QList<VideoWithCropWidget::FilterObject> filters) {
        // Here you would normally update the timeline's filter list for FFmpeg string building
        // For now, it keeps the UI in sync
    });

    connect(player, &QMediaPlayer::durationChanged, [this](qint64 d) {
        if (d > 0) {
            QApplication::processEvents();
            QTimer::singleShot(100, this, [this, d]() {
                timeline->setDuration(d);
                timeline->updateGeometry();
                timeline->forceFitToDuration();
                player->setPosition(0);
                player->play();
                timeline->update();
            });
        }
    });

    connect(timeline, &TimelineWidget::requestAudioTrackChange, [this](int trackIndex) {
    // This is the magic line that changes what you actually hear in the editor
    player->setActiveAudioTrack(trackIndex);

    // Optional: Log it to ensure it's working
    qDebug() << "Editor switching to audio track:" << trackIndex;
});

    connect(videoWithCrop, &VideoWithCropWidget::cropsChanged, timeline, &TimelineWidget::updateCropValues);
    connect(timeline, &TimelineWidget::clipTrimmed, [this]() {
        videoWithCrop->cropT = timeline->cropTop; videoWithCrop->cropB = timeline->cropBottom;
        videoWithCrop->cropL = timeline->cropLeft; videoWithCrop->cropR = timeline->cropRight;
        videoWithCrop->update();
    });

    connect(timeline, &TimelineWidget::playheadMoved, player, &QMediaPlayer::setPosition);

    auto* autoCutBtn = (QPushButton*)this->property("autoCutBtn").value<void*>();
    if(autoCutBtn) connect(autoCutBtn, &QPushButton::clicked, [this]() { timeline->autoCutSilence(); });

    auto* resetBtn = (QPushButton*)this->property("resetBtn").value<void*>();
    if(resetBtn) connect(resetBtn, &QPushButton::clicked, [this]() {
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

void MainWindow::handlePlaybackState(QMediaPlayer::PlaybackState state) {
    playPauseBtn->setText(state == QMediaPlayer::PlayingState ? "PAUSE" : "PLAY");
}

void MainWindow::updateVolume() {
    audio->setVolume((volSlider->value() / 100.0f) * timeline->audioGain);
}

void MainWindow::importMedia() {
    QString file = QFileDialog::getOpenFileName(this, "Import Media", "", "Videos (*.mp4 *.mkv *.mov)");
    if (!file.isEmpty()) {
        player->setSource(QUrl::fromLocalFile(file));
        timeline->setMediaSource(QUrl::fromLocalFile(file));
        player->play();
        statusLabel->setText(QFileInfo(file).fileName().toUpper());
    }
}

void MainWindow::loadInitialVideo() {
    QString videoPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QDir dir(videoPath);
    QFileInfoList fileList = dir.entryInfoList({"*.mp4", "*.mkv", "*.mov"}, QDir::Files, QDir::Time);
    if (!fileList.isEmpty()) {
        QString newest = fileList.first().absoluteFilePath();
        player->setSource(QUrl::fromLocalFile(newest));
        timeline->setMediaSource(QUrl::fromLocalFile(newest));
        statusLabel->setText(fileList.first().fileName().toUpper());
    }
}