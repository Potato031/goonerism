//
// Created by potato on 2/12/26.
//

#ifndef SIMPLEVIDEOEDITOR_MAINWINDOW_H
#define SIMPLEVIDEOEDITOR_MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include "timelinewidget.h"
#include "mediaSource.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>


class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    const QString CURRENT_VERSION = "v1.0.26";
    void downloadUpdate(const QString &url);
    void finalizeUpdate();
    void checkForUpdates();

private slots:
    void importMedia();
    void updateVolume();
    void handlePlaybackState(QMediaPlayer::PlaybackState state);
private:
    bool isVideoFullscreen = false;
    void setupUi();
    void setupConnections();
    void loadInitialVideo();
    QLineEdit* exportInput;
    QVBoxLayout* mainLayout;
    QFrame* toolbar;
    QFrame* footer;
    QWidget* timelineTools;
    TimelineWidget* timeline;
    QFrame* workspace;
    VideoWithCropWidget* videoWithCrop;
    QPushButton* fullscreenBtn;
    QPushButton* playPauseBtn;
    QLabel* statusLabel;
    QSlider* volSlider;
    // Media
    QMediaPlayer* player;
    QAudioOutput* audio;
    bool isUpdating = false;
};

#endif //SIMPLEVIDEOEDITOR_MAINWINDOW_H