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
#include <QResizeEvent>
#include <QStringList>
#include <QIcon>
#include "titlebar.h"

class QHBoxLayout;
class QShortcut;
class QMenu;
class QComboBox;
class QAction;
class QProgressBar;


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
        QString windowTitle = "Potato Studio";
        QString logoPrimaryText = "POTATO";
        QString logoSecondaryText = "STUDIO";
        QString importButtonText = "IMPORT";
        int sidebarWidth = 260;
        QString sidebarPosition = "left";
        QString toolButtonOrder = "text,blur,pixel,blackout,shape,colorcorrect,autocut,settings,resetcrop,speedramp";
        float defaultCropTop = 0.03f;
        float defaultCropBottom = 0.96f;
        float defaultCropLeft = 0.0f;
        float defaultCropRight = 1.0f;
        QString previewPlaceholderTitle = "Import media to start editing";
        QString previewPlaceholderBody = "Video appears here. Audio-only files can still be trimmed, auto-cut, and exported.";
        QString emptyTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT";
        QString videoTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT VIDEO";
        QString audioTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+SHIFT+C EXPORT AUDIO";
        QString timelineAccentColor = "#FF7A50";
        QString timelineSecondaryColor = "#FF5C33";
        QString timelineBackgroundColor = "#121217";
        QString timelineTrackColor = "#26262E";
        QString timelineWaveformColor = "#7A8B99";
        QString previewAccentColor = "#FF7A50";
        QString previewSecondaryColor = "#FF5C33";
        QString previewBackgroundColor = "#08080A";
        QString appBackgroundStartColor = "#0F0F13";
        QString appBackgroundEndColor = "#0D0D11";
        QString panelSurfaceColor = "#17171C";
        QString panelAltSurfaceColor = "#1D1D24";
        QString controlSurfaceColor = "#25252D";
        QString controlHoverColor = "#2E2E38";
        QString borderColor = "#3C3C4A";
        QString primaryTextColor = "#EFEFF4";
        QString mutedTextColor = "#9C9CA8";
        QString sectionLabelColor = "#8B8B97";
        QString logoPrimaryColor = "#FFFFFF";
        QString logoSecondaryColor = "#FF7A50";
        QString appFontFamily = "Sans Serif";
        int appFontPointSize = 12;
        int logoFontPointSize = 18;
        int mediaBadgeFontPointSize = 9;
        int metaFontPointSize = 11;
        int panelCornerRadius = 10;
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
        QString keyAddMarker = "M";
        QStringList autoLoadDirectories;
    };

    MainWindow(QWidget *parent = nullptr);
    EditorSettings getEditorSettings() const { return editorSettings; }
    const QString CURRENT_VERSION = "1.2.0";
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
    void setupTitleBar();
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
    void applyToolButtonOrder();
    void applyIcons();
    void saveSnapshot();
    void showShortcutsDialog();
    QString buildAppStyleSheet() const;
    // Overlay clip <-> preview sync (regions shown/edited on the video)
    void syncOverlaysToPreview();
    void editTextOverlay(int index);
    void editOverlayProperties(int index);
    void openSpeedRampDialog();
    void showHistoryMenu();
    // Multi-source playback: seek in timeline time, switching files as needed
    void seekTimeline(qint64 timelinePosMs);
    void updateTimelineChips();
    QLineEdit* exportInput;
    QVBoxLayout* mainLayout;
    TitleBar* titleBar;
    QList<ResizeGrip*> resizeGrips;
    void updateMaximizedState();
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
    // Transport / toolbar controls
    QFrame* transportBar;
    QPushButton* jumpBackBtn;
    QPushButton* stepBackBtn;
    QPushButton* stepFwdBtn;
    QPushButton* jumpFwdBtn;
    QPushButton* muteBtn;
    QPushButton* snapshotBtn;
    QComboBox* speedBox;
    QPushButton* exportBtn;
    QMenu* exportMenu;
    QAction* exportVideoAction;
    QAction* exportMutedAction;
    QAction* exportAudioAction;
    QAction* exportGifAction;
    QPushButton* helpBtn;
    QPushButton* sidebarImportBtn;
    QPushButton* undoBtn;
    QPushButton* redoBtn;
    QPushButton* historyBtn;
    QPushButton* splitBtn;
    QPushButton* deleteClipBtn;
    // Cached icons that swap at runtime
    QIcon playIcon, pauseIcon, volumeIcon, volumeMutedIcon, fullscreenIcon, exitFullscreenIcon;
    QLabel* toolHeaderLabel;
    QLabel* redactGroupLabel;
    QLabel* actionsGroupLabel;
    QLabel* logoBoldLabel;
    QLabel* logoLightLabel;
    QLabel* statusLabel;
    QLabel* audioTrackChip;
    QLabel* estSizeChip;
    QLabel* currentMediaLabel;
    QLabel* transportHintLabel;
    QLabel* sidebarCountLabel;
    QLabel* sidebarEmptyLabel;
    QSlider* volSlider;
    QLabel* timecodeLabel;
    QSlider* timelineZoomSlider;
    QPushButton* timelineFitBtn;
    void updateTimecodeDisplay();
    // Media
    QMediaPlayer* player;
    QAudioOutput* audio;
    bool isUpdating = false;
    QPushButton *toggleFilterBtn;
    QPushButton *blurBtn;
    QPushButton *pixelBtn;
    QPushButton *solidBtn;
    QPushButton *textBtn;
    QPushButton *shapeBtn;
    QPushButton *colorCorrectBtn;
    QPushButton *speedRampBtn;
    QFrame* clipSidebar;
    QScrollArea* sidebarScroll;
    QWidget* sidebarContent;
    QVBoxLayout* sidebarListLayout;
    QString currentMediaPath;
    QStringList cachedRecentFiles;
    EditorSettings editorSettings;
    QShortcut* playPauseShortcut;
    // Export progress (inline, in the timeline header)
    QProgressBar* exportProgressBar;
    // Which overlay each preview region maps to (index into timeline->overlays)
    QList<int> previewOverlayMap;
    bool syncingPreview = false;
    // Multi-source playback state
    int activeSourceIdx = 0;
    bool switchingSource = false;
    qint64 pendingSeekLocalPos = -1;
    // --- NEW HELPER FUNCTIONS ---
    void loadClipDirectly(const QString &filePath);
    void updateSidebar();
protected:
    bool eventFilter(QObject *obj, QEvent *event) override; // RIGHT
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
};

#endif //SIMPLEVIDEOEDITOR_MAINWINDOW_H
