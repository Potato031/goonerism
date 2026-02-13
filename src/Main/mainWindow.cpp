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
#include <QTimer>

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

    // --- FOOTER SECTION ---
    auto* footer = new QFrame();
    footer->setFixedHeight(100);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(30, 0, 30, 20);

    // Version label in the far left
    auto* versionLabel = new QLabel(CURRENT_VERSION);
    versionLabel->setObjectName("VersionLabel");
    // Faded white so it doesn't distract from the UI
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

    // Assemble the footer
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

                qDebug() << "Timeline Width during load:" << timeline->width();
            });
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
        statusLabel->setText(fileList.first().fileName().toUpper());
    }
}


void MainWindow::checkForUpdates() {
    // 1. Strict guard to prevent double windows
    if (isUpdating) return;

    auto* manager = new QNetworkAccessManager(this);
    QUrl url("https://api.github.com/repos/Potato031/goonerism/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PotatoEditor-Updater");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    connect(manager, &QNetworkAccessManager::finished, [this, manager](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QString latestTag = obj.value("tag_name").toString();

            // Only proceed if versions don't match AND we aren't already updating
            if (!latestTag.isEmpty() && latestTag != CURRENT_VERSION && !isUpdating) {
                QJsonArray assets = obj.value("assets").toArray();
                QString downloadUrl;

                for (const QJsonValue& asset : assets) {
                    QString name = asset.toObject().value("name").toString();
#ifdef Q_OS_WIN
                    if (name.endsWith(".zip")) downloadUrl = asset.toObject().value("browser_download_url").toString();
#else
                    if (name.endsWith(".AppImage")) downloadUrl = asset.toObject().value("browser_download_url").toString();
#endif
                }

                if (!downloadUrl.isEmpty()) {
                    isUpdating = true;

                    auto res = QMessageBox::question(this, "Update Available",
                        "A new version (" + latestTag + ") is available. Update now?",
                        QMessageBox::Yes | QMessageBox::No);

                    if (res == QMessageBox::Yes) {
                        if (player) player->pause();
                        downloadUpdate(downloadUrl);
                    } else {
                        isUpdating = false;
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

    connect(reply, &QNetworkReply::finished, [this, reply, progress, manager]() {
        progress->close();
        if (reply->error() == QNetworkReply::NoError) {
            QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
            QString fileName = appDir + "/update.zip";
#else
            QString fileName = appDir + "/update.AppImage";
#endif
            QFile file(fileName);
            if (file.open(QFile::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                finalizeUpdate();
            } else {
                QMessageBox::critical(this, "Update Error", "Could not save update file to: " + fileName);
                isUpdating = false;
            }
        } else {
            isUpdating = false;
        }
        reply->deleteLater();
        manager->deleteLater();
    });

    // Optional: Connect progress bar
    connect(reply, &QNetworkReply::downloadProgress, [progress](qint64 received, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>((received * 100) / total));
    });
}

void MainWindow::finalizeUpdate() {
#ifdef Q_OS_WIN
    QFile batchFile("update.bat");
    if (batchFile.open(QFile::WriteOnly)) {
        QTextStream out(&batchFile);
        out << "@echo off\n"
            << "title POTATO UPDATE ENGINE\n"
            << "echo Waiting for Editor to close...\n"
            << ":loop\n"
            << "taskkill /f /im PotatoEditor.exe >nul 2>&1\n"
            << "timeout /t 1 /nobreak >nul\n"
            << "tasklist /fi \"imagename eq PotatoEditor.exe\" | find /i \"PotatoEditor.exe\" >nul\n"
            << "if not errorlevel 1 goto loop\n"
            << "echo Extracting...\n"
            << "powershell -windowstyle hidden -command \"Expand-Archive -Path 'update.zip' -DestinationPath 'temp_update' -Force\"\n"
            << "echo Installing...\n"
            << "for /d %%d in (\"temp_update\\*\") do ( robocopy \"%%d\" \".\" /S /E /MOVE >nul )\n"
            << "robocopy \"temp_update\" \".\" /S /E /MOVE >nul\n"
            << "rd /s /q \"temp_update\"\n"
            << "del /f /q update.zip\n"
            << "start \"\" \"PotatoEditor.exe\"\n"
            << "del \"%~f0\"\n";
        batchFile.close();
        QProcess::startDetached("cmd.exe", {"/c", "update.bat"});
        qApp->quit();
    }
#else
    // Get the absolute path to the folder where the current AppImage is located
    QString appDir = QCoreApplication::applicationDirPath();
    QString shPath = appDir + "/update.sh";
    QString oldAppPath = appDir + "/PotatoEditor_linux.AppImage";
    QString newAppPath = appDir + "/update.AppImage";

    QFile shFile(shPath);
    if (shFile.open(QFile::WriteOnly)) {
        QTextStream out(&shFile);
        out << "#!/bin/bash\n"
            << "sleep 2\n"
            // Change directory to where the AppImage actually lives
            << "cd \"" << appDir << "\"\n"
            << "chmod +x update.AppImage\n"
            << "mv update.AppImage PotatoEditor_linux.AppImage\n"
            << "chmod +x PotatoEditor_linux.AppImage\n"
            << "./PotatoEditor_linux.AppImage &\n"
            << "rm -- \"$0\"\n";
        shFile.close();

        QProcess::execute("chmod", {"+x", shPath});
        QProcess::startDetached("/bin/bash", {shPath});
        qApp->quit();
    }
#endif
}