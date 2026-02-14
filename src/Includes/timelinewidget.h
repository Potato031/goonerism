#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QList>
#include <QUrl>
#include <QMap>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QSet>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

class QProcess;

// Helper UI for the top-right progress tracking
class ProgressBarNotification : public QWidget {
    Q_OBJECT
public:
    explicit ProgressBarNotification(const QString &title, QWidget *parent = nullptr) : QWidget(nullptr) {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);

        auto *lay = new QVBoxLayout(this);
        auto *bg = new QWidget(this);
        // --- NEW: Remove setStyleSheet, add ObjectName ---
        bg->setObjectName("ProgressNotificationBg");

        auto *inner = new QVBoxLayout(bg);
        label = new QLabel(title, this);
        label->setObjectName("ProgressNotificationLabel");

        bar = new QProgressBar(this);
        bar->setObjectName("ProgressNotificationBar");
        bar->setRange(0, 100);

        inner->addWidget(label);
        inner->addWidget(bar);
        lay->addWidget(bg);
        adjustSize();
        QScreen *screen = QGuiApplication::primaryScreen();
        move(screen->availableGeometry().topRight() - QPoint(width() + 20, -20));
    }
    void setProgress(int value) { bar->setValue(value); }

private:
    QProgressBar *bar;
    QLabel *label;
};

class TimelineWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor)
   Q_PROPERTY(QColor secondaryColor MEMBER m_secondaryColor)
   Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)
   Q_PROPERTY(QColor trackColor MEMBER m_trackColor)
   Q_PROPERTY(QColor waveformColor MEMBER m_waveformColor)
    Q_PROPERTY(QColor trackColor MEMBER m_trackColor) // Add this line!

public:
    // 1. Move Segment inside the class to fix scoping errors
    struct Segment {
        qint64 startMs;
        qint64 endMs;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool muted = false;
    };

    struct TextSegment {
        QString text;
        qint64 startMs;
        qint64 durationMs; // Length of the text clip
        int trackIndex = 0; // In case you want multiple text layers
    };

    QList<TextSegment> textSegments;
    int selectedTextIdx = -1;
    bool isDraggingText = false;
    int textTrackY = 40;
    int textTrackHeight = 30;
    int dragStartMouseX = 0;
    qint64 dragStartMs = 0;
    qint64 dragStartDurationMs = 0;

    explicit TimelineWidget(QWidget* parent = nullptr);

    void setMediaSource(const QUrl &url);
    void setDuration(qint64 ms);
    void setCurrentPosition(qint64 ms) { currentPosMs = ms; update(); }

    int currentAudioTrack = 0;
    int totalAudioTracks = 1;
    qint64 durationMs = 0;
    qint64 currentPosMs = 0;
    float audioGain = 1.0f;
    float verticalCropLimit = 1.0f;
    float cropTop = 0.0f, cropBottom = 1.0f, cropLeft = 0.0f, cropRight = 1.0f;

    float filterL = 0.1f;
    float filterR = 0.3f;
    float filterT = 0.1f;
    float filterB = 0.3f;
    int currentFilter = 0;
    bool filterEnabled = false;
    void undo();
    void redo();
    void splitAtPlayhead();
    void deleteSelectedSegment();
    void validatePlayheadPosition();
    void autoCutSilence();
    qint64 getStartLimit() const;

    qint64 getEndLimit() const;
    void forceFitToDuration();
    void copyTrimmedVideo();
    void copyTrimmedAudio();
    void copyTrimmedGif();
    void copyTrimmedVideoMuted();
    QString customExportName;

    QColor m_accentColor = QColor("#3D5AFE"); // Defaults in case QSS fails
    QColor m_secondaryColor = QColor("#FF3232");
    QColor m_backgroundColor = QColor("#080809");
    QColor m_trackColor = QColor("#1A1A1C");
    QColor m_waveformColor = QColor("#88888E");

signals:
    void playheadMoved(qint64 pos);
    void requestTogglePlayback();
    void clipTrimmed();
    void audioGainChanged(float gain);
    void audioTrackChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;


    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent *event) override;

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        this->style()->unpolish(this);
        this->style()->polish(this);
    }
    bool isAnySelectedMuted();

private:
    struct TimelineState {
        QList<Segment> segments;
    };

    QList<Segment> segments;
    QList<TimelineState> undoStack;
    QList<TimelineState> redoStack;
    const int MAX_STACK_SIZE = 50;

    void saveState();

    QMediaPlayer* thumbPlayer;
    QVideoSink* videoSink;
    QMap<int, QImage> thumbnailCache;
    QVector<float> audioSamples;
    QUrl currentFileUrl;
    qint64 originalFileSize = 0;
    float maxAmplitude = 0.01f;

    double zoomFactor = 1.0;
    int scrollOffset = 0;
    const int sidebarWidth = 0;
    const int rulerHeight = 30;
    const int trackHeight = 60;

    int selectedSegmentIdx = -1;
    enum Edge { None, Start, End };
    Edge activeEdge = None;
    int activeSegmentIdx = -1;


    float pulseAlpha = 1.0f;
    bool pulseIncreasing = false;
    QTimer* pulseTimer;
    bool isSelecting = false;
    QRect selectionRect;
    QSet<int> selectedSegmentIndices;
    QSet<int> preSelectSnapshot;
    bool isExporting = false;
    bool isScrubbing = false;

    QStringList trackNames = {
        "All audio", "All discord audio + mic", "Only discord audio",
        "Chromium + mic", "WEBRTC VoiceEngine + mic", "everything excluding discord"
    };

    double getTotalSegmentsDuration();
    void loadAudioFast(const QString &path);
    void processVideoFrame(const QVideoFrame &frame);


    static void showNotification(const QString &message);
    void showProgressNotification(QProcess* process, qint64 totalMs);
    void detectAudioTracks(const QString &path);


    QString generateClippedName(const QString &extension) const;
};

#endif // TIMELINEWIDGET_H