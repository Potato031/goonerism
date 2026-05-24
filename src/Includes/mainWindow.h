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
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>
#include <QDialog>
#include <QList>
#include <QSplitter>
#include <QCloseEvent>
#include <QStringList>

class QHBoxLayout;
class QShortcut;


class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    struct EditorSettings {
        bool autoPlayOnImport = true;
        bool checkForUpdatesOnStartup = true;
        int defaultVolumePercent = 80;
        int recentMediaLimit = 8;
        int notificationDurationMs = 2000;
        QString notificationPosition = "top-right";
        int updateCheckDelayMs = 2000;
        QString windowTitle = "Potato Editor";
        QString logoPrimaryText = "POTATOES";
        QString logoSecondaryText = "QUICK ONE";
        QString importButtonText = "IMPORT MEDIA";
        int sidebarWidth = 260;
        QString sidebarPosition = "left";
        QString toolButtonOrder = "blur,pixel,blackout,autocut,settings,resetcrop";
        float defaultCropTop = 0.03f;
        float defaultCropBottom = 0.96f;
        float defaultCropLeft = 0.0f;
        float defaultCropRight = 1.0f;
        QString previewPlaceholderTitle = "Import media to start editing";
        QString previewPlaceholderBody = "Video appears here. Audio-only files can still be trimmed, auto-cut, and exported.";
        QString emptyTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT";
        QString videoTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT VIDEO";
        QString audioTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+SHIFT+C EXPORT AUDIO";
        QString timelineAccentColor = "#FF875F";
        QString timelineSecondaryColor = "#FF6B4A";
        QString timelineBackgroundColor = "#14181D";
        QString timelineTrackColor = "#272C34";
        QString timelineWaveformColor = "#7D5F56";
        QString previewAccentColor = "#FF875F";
        QString previewSecondaryColor = "#FF6B4A";
        QString previewBackgroundColor = "#0F1115";
        QString appBackgroundStartColor = "#111317";
        QString appBackgroundEndColor = "#0F1115";
        QString panelSurfaceColor = "#1A1D23";
        QString panelAltSurfaceColor = "#1D2128";
        QString controlSurfaceColor = "#22272F";
        QString controlHoverColor = "#2D333D";
        QString borderColor = "#2A3038";
        QString primaryTextColor = "#F1F4F7";
        QString mutedTextColor = "#C6CDD5";
        QString sectionLabelColor = "#D6DCE3";
        QString logoPrimaryColor = "#F4F6F8";
        QString logoSecondaryColor = "#FF9B77";
        QString appFontFamily = "Sans Serif";
        int appFontPointSize = 13;
        int logoFontPointSize = 20;
        int mediaBadgeFontPointSize = 10;
        int metaFontPointSize = 11;
        int panelCornerRadius = 12;
        int buttonCornerRadius = 6;
        QString keyPlayPause = "Space";
        QString keySplit = "S";
        QString keyDeleteClip = "Delete";
        QString keyReplay = "Q";
        QString keyForward = "W";
        QString keyStepBack = "Left";
        QString keyStepForward = "Right";
        QString keyUndo = "Ctrl+Z";
        QString keyRedo = "Ctrl+Shift+Z";
        QString keyExportGif = "Ctrl+G";
        QString keyExportAudio = "Ctrl+Shift+C";
        QString keyExportVideo = "Ctrl+C";
        QString keyExportMutedVideo = "Ctrl+Alt+C";
        QString keyCycleAudioTrack = "Alt+A";
        QStringList autoLoadDirectories;
    };

    MainWindow(QWidget *parent = nullptr);
    EditorSettings getEditorSettings() const { return editorSettings; }
    const QString CURRENT_VERSION = "v1.1.2";
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
    void setupToolbar();
    void setupSidebar();
    void setupWorkspace();
    void setupTimeline();
    void setupConnections();
    void loadInitialVideo();
    void toggleVideoFullscreen();
    void restoreVideoFromFullscreen();
    void refreshMediaState();
    QStringList collectRecentMediaFiles() const;
    void openSettingsDialog();
    void loadEditorSettings();
    void saveEditorSettings() const;
    void applyEditorSettings();
    void applyStoredFilters();
    void applyToolButtonOrder();
    QString buildAppStyleSheet() const;
    QLineEdit* exportInput;
    QVBoxLayout* mainLayout;
    QFrame* toolbar;
    QFrame* footer;
    QWidget* timelineTools;
    QVBoxLayout* timelineToolsLayout;
    TimelineWidget* timeline;
    QFrame* timelineShell;
    QSplitter* mainSplitter;
    QSplitter* topPaneSplitter;
    QFrame* workspace;
    QHBoxLayout* workspaceContentLayout;
    QVBoxLayout* stageColumnLayout;
    QFrame* previewHeader;
    QFrame* videoContainer;
    QWidget* videoFullscreenPlaceholder;
    QDialog* videoFullscreenDialog;
    VideoWithCropWidget* videoWithCrop;
    QPushButton* fullscreenBtn;
    QPushButton* importBtn;
    QPushButton* playPauseBtn;
    QPushButton* autoCutBtn;
    QPushButton* settingsBtn;
    QPushButton* resetCropBtn;
    QLabel* toolHeaderLabel;
    QLabel* logoBoldLabel;
    QLabel* logoLightLabel;
    QLabel* statusLabel;
    QLabel* currentMediaLabel;
    QLabel* transportHintLabel;
    QLabel* sidebarCountLabel;
    QLabel* sidebarEmptyLabel;
    QSlider* volSlider;
    // Media
    QMediaPlayer* player;
    QAudioOutput* audio;
    bool isUpdating = false;
    QPushButton *toggleFilterBtn;
    QPushButton *blurBtn;
    QPushButton *pixelBtn;
    QPushButton *solidBtn;
    QFrame* clipSidebar;
    QScrollArea* sidebarScroll;
    QWidget* sidebarContent;
    QVBoxLayout* sidebarListLayout;
    QString currentMediaPath;
    QStringList cachedRecentFiles;
    QList<VideoWithCropWidget::FilterObject> persistentFilters;
    EditorSettings editorSettings;
    QShortcut* playPauseShortcut;
    // --- NEW HELPER FUNCTIONS ---
    void loadClipDirectly(const QString &filePath);
    void updateSidebar();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override; // RIGHT
    void closeEvent(QCloseEvent *event) override;
};

#endif //SIMPLEVIDEOEDITOR_MAINWINDOW_H
