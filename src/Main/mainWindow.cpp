#include "../Includes/mainWindow.h"
#include "../Includes/resizeFilter.h"
#include "../Includes/dropFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDir>
#include <QStyle>
#include <QFileInfo>
#include <QShortcut>
#include <QLineEdit>
#include <QApplication>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    setupConnections();
    loadInitialVideo();
}

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    centralWidget->setAcceptDrops(true);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* toolbar = new QFrame();
    toolbar->setFixedHeight(60);
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

    auto* exportInput = new QLineEdit();
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

    auto* workspace = new QFrame();
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(25, 5, 25, 10);

    auto* videoContainer = new QFrame();
    videoContainer->setObjectName("VideoContainer");
    videoContainer->setMinimumHeight(400);
    videoWithCrop = new VideoWithCropWidget(videoContainer);
    videoContainer->installEventFilter(new ResizeFilter(videoWithCrop));

    auto* timelineTools = new QWidget();
    auto* toolsLayout = new QHBoxLayout(timelineTools);
    toolsLayout->setContentsMargins(0, 10, 0, 5);

    auto* autoCutBtn = new QPushButton("AUTO-CUT SILENCE");
    autoCutBtn->setProperty("class", "ToolBtn");
    autoCutBtn->setFixedSize(160, 30);

    auto* resetCropBtn = new QPushButton("RESET CROP");
    resetCropBtn->setProperty("class", "ToolBtn");
    resetCropBtn->setFixedSize(100, 30);

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

    auto* footer = new QFrame();
    footer->setFixedHeight(100);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(30, 0, 30, 20);

    volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100);
    volSlider->setValue(80);
    volSlider->setFixedWidth(110);

    playPauseBtn = new QPushButton("PLAY");
    playPauseBtn->setObjectName("ActionBtn");
    playPauseBtn->setFixedSize(140, 44);

    statusLabel = new QLabel("READY");
    statusLabel->setObjectName("MetaData");

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

    connect(player, &QMediaPlayer::durationChanged, timeline, [this](qint64 d) {
        if (d > 0) {
            timeline->setDuration(d);
            timeline->update();
        }
    });

    connect(timeline, &TimelineWidget::playheadMoved, player, &QMediaPlayer::setPosition);
    connect(timeline, &TimelineWidget::audioTrackChanged, player, &QMediaPlayer::setActiveAudioTrack);

    auto* autoCutBtn = (QPushButton*)this->property("autoCutBtn").value<void*>();
    if(autoCutBtn) connect(autoCutBtn, &QPushButton::clicked, [this]() { timeline->autoCutSilence(); });

    auto* resetBtn = (QPushButton*)this->property("resetBtn").value<void*>();
    if(resetBtn) connect(resetBtn, &QPushButton::clicked, [this]() {
        videoWithCrop->cropT = 0.03f; videoWithCrop->cropB = 0.96f;
        videoWithCrop->cropL = 0.0f; videoWithCrop->cropR = 1.0f;
        timeline->cropTop = 0.03f; timeline->cropBottom = 0.96f;
        videoWithCrop->update();
        timeline->update();
    });

    auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(spaceShortcut, &QShortcut::activated, [this]() {
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });

    auto* exportInput = (QLineEdit*)this->property("exportInput").value<void*>();
    if (exportInput) {
        connect(exportInput, &QLineEdit::textChanged, [this](const QString &text) {
            timeline->customExportName = text.trimmed().replace(" ", "_");
        });
    }
}

void MainWindow::handlePlaybackState(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::PlayingState) {
        playPauseBtn->setText("PAUSE");
    } else {
        playPauseBtn->setText("PLAY");
    }
    playPauseBtn->style()->unpolish(playPauseBtn);
    playPauseBtn->style()->polish(playPauseBtn);
}

void MainWindow::updateVolume() {
    audio->setVolume((volSlider->value() / 100.0f) * timeline->audioGain);
}

void MainWindow::importMedia() {
    QString file = QFileDialog::getOpenFileName(this, "Import Media", "", "Video Files (*.mp4 *.mkv *.mov *.avi *.webm);;All Files (*.*)");
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
        player->play();
        statusLabel->setText(fileList.first().fileName().toUpper());
    }
}


void MainWindow::checkForUpdates() {
    auto* manager = new QNetworkAccessManager(this);
    QUrl url("https://github.com/Potato031/goonerism/releases/tag/latest");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PotatoEditor-Updater");

    connect(manager, &QNetworkAccessManager::finished, [this, manager](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            QString latestTag = json.object().value("tag_name").toString();

            if (latestTag != CURRENT_VERSION) {
                auto assets = json.object().value("assets").toArray();
                if (!assets.isEmpty()) {
                    QString downloadUrl = assets.at(0).toObject().value("browser_download_url").toString();

                    auto res = QMessageBox::question(this, "Update Available",
                        "A new version (" + latestTag + ") is available. Update now?",
                        QMessageBox::Yes | QMessageBox::No);

                    if (res == QMessageBox::Yes) {
                        downloadUpdate(downloadUrl);
                    }
                }
            }
        }
        reply->deleteLater();
        manager->deleteLater();
    });
    manager->get(request);
}

void MainWindow::downloadUpdate(const QString &url) {
    auto* manager = new QNetworkAccessManager(this);
    QNetworkReply* reply = manager->get(QNetworkRequest(QUrl(url)));

    auto* progress = new QProgressDialog("Downloading update...", "Cancel", 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);

    connect(reply, &QNetworkReply::downloadProgress, [progress](qint64 received, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>((received * 100) / total));
    });

    connect(reply, &QNetworkReply::finished, [this, reply, progress, manager]() {
        progress->close();
        if (reply->error() == QNetworkReply::NoError) {
            QFile file("update.zip");
            if (file.open(QFile::WriteOnly)) {
                file.write(reply->readAll());
                file.close();

                // PowerShell command for Windows extraction
                QProcess* unzip = new QProcess(this);
                unzip->start("powershell", {"-Command", "Expand-Archive -Path update.zip -DestinationPath temp_update -Force"});
                connect(unzip, &QProcess::finished, [this]() {
                    finalizeUpdate();
                });
            }
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

void MainWindow::finalizeUpdate() {
    QFile batchFile("update.bat");
    if (batchFile.open(QFile::WriteOnly)) {
        QTextStream out(&batchFile);
        out << "@echo off\n"
            << "timeout /t 1 /nobreak > nul\n"
            << "taskkill /f /im PotatoEditor.exe > nul 2>&1\n"
            << "xcopy /s /y \"temp_update\\*\" \".\\\"\n"
            << "rd /s /q \"temp_update\"\n"
            << "del update.zip\n"
            << "start PotatoEditor.exe\n"
            << "del \"%~f0\"\n";
        batchFile.close();

        QProcess::startDetached("cmd.exe", {"/c", "update.bat"});
        qApp->quit();
    }
}