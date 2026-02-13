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


class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    const QString CURRENT_VERSION = "v1.0.5"; // Update this per release
    void downloadUpdate(const QString &url);
    void finalizeUpdate();
    void checkForUpdates();

private slots:
    void importMedia();
    void updateVolume();
    void handlePlaybackState(QMediaPlayer::PlaybackState state);

private:
    void setupUi();
    void setupConnections();
    void loadInitialVideo();

    QNetworkReply *downloadReply = nullptr;

    // UI Elements
    TimelineWidget* timeline;
    VideoWithCropWidget* videoWithCrop;
    QMediaPlayer* player;
    QAudioOutput* audio;

    QPushButton* playPauseBtn;
    QLabel* statusLabel;
    QSlider* volSlider;

};

#endif //SIMPLEVIDEOEDITOR_MAINWINDOW_H