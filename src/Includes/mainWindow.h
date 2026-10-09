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
#include <QElapsedTimer>
#include <QStringList>
#include <QIcon>
#include "titlebar.h"

class QHBoxLayout;
class QShortcut;
class QMenu;
class QComboBox;
class QAction;
class QProgressBar;
class QVideoWidget;
class QTabWidget;


class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class EditorTests;
public:
    struct EditorSettings {
        bool autoPlayOnImport = true;
        bool loadNewestVideoOnStartup = true;
        bool checkForUpdatesOnStartup = true;
        int defaultVolumePercent = 80;
        int recentMediaLimit = 8;
        int notificationDurationMs = 2000;
        QString notificationPosition = "top-right";
        int updateCheckDelayMs = 2000;
        QString windowTitle = "Potato Studio";
        QString logoPrimaryText = "Potato";
        QString logoSecondaryText = "Studio";
        QString importButtonText = "Import media";
        int sidebarWidth = 260;
        QString sidebarPosition = "left";
        QString toolButtonOrder = "text,blur,pixel,blackout,shape,colorcorrect,autocut,settings,resetcrop,speedramp";
        float defaultCropTop = 0.03f;
        float defaultCropBottom = 0.96f;
        float defaultCropLeft = 0.0f;
        float defaultCropRight = 1.0f;
        QString previewPlaceholderTitle = "Start a new edit";
        QString previewPlaceholderBody = "Drop video or audio here, or choose Import Media.";
        QString emptyTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT";
        QString videoTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+C EXPORT VIDEO";
        QString audioTransportHint = "SPACE PLAY/PAUSE | S SPLIT | CTRL+SHIFT+C EXPORT AUDIO";
        QString timelineAccentColor = "#A78BE0";
        QString timelineSecondaryColor = "#A78BE0";
        QString timelineBackgroundColor = "#141419";
        QString timelineTrackColor = "#24242D";
        QString timelineWaveformColor = "#83BBA6";
        QString previewAccentColor = "#A78BE0";
        QString previewSecondaryColor = "#83BBA6";
        QString previewBackgroundColor = "#09090C";
        QString appBackgroundStartColor = "#121217";
        QString appBackgroundEndColor = "#121217";
        QString panelSurfaceColor = "#1B1B22";
        QString panelAltSurfaceColor = "#17171D";
        QString controlSurfaceColor = "#292932";
        QString controlHoverColor = "#363640";
        QString borderColor = "#494951";
        QString primaryTextColor = "#EDECF2";
        QString mutedTextColor = "#A6A4B2";
        QString sectionLabelColor = "#C8C5D2";
        QString logoPrimaryColor = "#EDECF2";
        QString logoSecondaryColor = "#A78BE0";
        QString appFontFamily = "Noto Sans";
        int appFontPointSize = 10;
        int logoFontPointSize = 13;
        int mediaBadgeFontPointSize = 9;
        int metaFontPointSize = 9;
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
    ~MainWindow() override;
    EditorSettings getEditorSettings() const { return editorSettings; }
    const QString CURRENT_VERSION = "1.3.2";
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
    void showCommandPalette();
    void resetPanelLayout();
    void setLibraryPageVisible(int index, bool visible);
    void updateEditActions();
    QLineEdit *mediaSearch;
    QTabWidget *libraryTabs = nullptr;
    QPushButton *commandBtn = nullptr;
    QPushButton *viewBtn;
    QPushButton *emptyImportBtn;
    QWidget *emptyPreviewPanel;
    QMenu *viewMenu;
    bool exportBusy = false;
    bool recentFilesScanned = false;

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
    QLineEdit* exportInput = nullptr;
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
    QVideoWidget* nativeVideoWidget;
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
    float lastAppliedVolume = -1.0f;
    QElapsedTimer playbackUiClock;
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
