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
#include <QString>
#include <QQueue>
#include "mediaSource.h"

class QProcess;

class TimelineWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor)
   Q_PROPERTY(QColor secondaryColor MEMBER m_secondaryColor)
   Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor)
   Q_PROPERTY(QColor trackColor MEMBER m_trackColor)
   Q_PROPERTY(QColor waveformColor MEMBER m_waveformColor)
public slots:
    void updateCropValues(float t, float b, float l, float r);
public:
    struct AutoCutSettings {
        double silenceThresholdDb = -45.0;
        double minimumSilenceDurationSec = 0.3;
        double paddingSec = 0.15;
        double minimumClipDurationSec = 0.1;
    };

    struct PlaybackSettings {
        int majorSeekMs = 2000;
        int minorSeekMs = 16;
        int splitGuardMs = 200;
        int minSegmentDurationMs = 100;
    };

    struct ExportSettings {
        QString exportDirectory;
        int gifFps = 12;
        int gifWidth = 480;
        int audioBitrateKbps = 192;
        int compressedAudioBitrateKbps = 128;
        double videoCompressionThresholdMB = 8.0;
        double targetCompressedSizeMB = 7.1;
        QString fileNamePrefix = "clip";
        bool includeSourceNameInExport = true;
    };

    // 1. Move Segment inside the class to fix scoping errors
    struct Segment {
        qint64 startMs;         // timeline time
        qint64 endMs;           // timeline time
        int sourceIdx = 0;      // which entry in `sources` this segment plays from
        float volume = 1.0f;
        float pitch = 1.0f;
        bool muted = false;
        float gain = 1.0f;
        float cropTop = 0.0f;
        float cropBottom = 1.0f;
        float cropLeft = 0.0f;
        float cropRight = 1.0f;
    };

    // A media file placed on the timeline. sources[0] is the primary file;
    // additional files appended by dropping them onto the timeline follow it.
    struct SourceClip {
        QString path;
        qint64 offsetMs = 0;    // where this source's t=0 sits on the timeline
        qint64 durationMs = 0;
        qint64 fileSizeBytes = 0;
        bool hasVideo = true;
        bool hasAudio = true;
    };

    // A time-ranged effect clip that lives on its own lane above the video
    // track (blur / pixelate / blackout region, or a text overlay).
    struct OverlayClip {
        int type = 0;           // 0 blur, 1 pixelate, 2 blackout, 3 text
        qint64 startMs = 0;     // timeline time
        qint64 endMs = 0;
        float l = 0.4f, t = 0.4f, r = 0.6f, b = 0.6f; // region on the video, normalized
        QString text;           // only for type 3
    };

    QList<SourceClip> sources;
    QList<OverlayClip> overlays;
    int selectedOverlayIdx = -1;

    explicit TimelineWidget(QWidget* parent = nullptr);

    void setMediaSource(const QUrl &url);
    void setDuration(qint64 ms);
    void setCurrentPosition(qint64 ms);

    int currentAudioTrack = 0;
    int totalAudioTracks = 1;
    qint64 durationMs = 0;
    qint64 currentPosMs = 0;
    float audioGain = 1.0f;
    float verticalCropLimit = 1.0f;
    float cropTop = 0.0f, cropBottom = 1.0f, cropLeft = 0.0f, cropRight = 1.0f;
    void undo();
    void redo();
    void splitAtPlayhead();
    void requestSplit() { saveState(); splitAtPlayhead(); }
    void deleteSelectedSegment();
    void deleteActiveSelection();
    void validatePlayheadPosition();
    void autoCutSilence();
    bool handleGlobalKey(QKeyEvent *event);

    // --- Overlay clips (effect/text lanes above the video track) ---
    void addOverlayAt(int type, qint64 timeMs);
    void addOverlayAtPlayhead(int type) { addOverlayAt(type, currentPosMs); }
    void deleteSelectedOverlay();
    QList<int> overlaysAtTime(qint64 timeMs) const;
    QVector<int> computeOverlayLanes() const;
    int overlayLaneCount() const;

    // --- Multi-source timeline ---
    void appendMediaSource(const QString &path);
    int sourceIndexForTimelineTime(qint64 timeMs) const;
    qint64 sourceOffsetMs(int sourceIdx) const {
        return (sourceIdx >= 0 && sourceIdx < sources.size()) ? sources[sourceIdx].offsetMs : 0;
    }

    // Metadata for the header chips
    double estimatedExportSizeMB() const;
    QString currentAudioTrackName() const {
        return (currentAudioTrack < trackNames.size())
                   ? trackNames[currentAudioTrack]
                   : QString("Track %1").arg(currentAudioTrack + 1);
    }

    // Content height for the surrounding scroll area (grows with overlay lanes)
    void relayout();
    qint64 getStartLimit() const;
    QString getMediaFilePath() const { return currentFileUrl.toLocalFile(); }
    qint64 getEndLimit() const;
    void forceFitToDuration();
    double getZoomFactor() const { return zoomFactor; }
    void setZoomFactor(double z);
    void resetZoomView();
    void copyTrimmedVideo();
    void copyTrimmedAudio();
    void copyTrimmedGif();
    void copyTrimmedVideoMuted();
    QString customExportName;
    double getTotalSegmentsDuration();
    bool sourceHasVideo() const { return hasVideoStream; }
    bool sourceHasAudio() const { return hasAudioStream; }
    AutoCutSettings getAutoCutSettings() const { return autoCutSettings; }
    void setAutoCutSettings(const AutoCutSettings &settings) { autoCutSettings = settings; }
    PlaybackSettings getPlaybackSettings() const { return playbackSettings; }
    void setPlaybackSettings(const PlaybackSettings &settings) { playbackSettings = settings; }
    ExportSettings getExportSettings() const { return exportSettings; }
    void setExportSettings(const ExportSettings &settings) { exportSettings = settings; }
    void applyCurrentVisualsToSelection(bool allSegments);
    void clearVisualsForSelection(bool allSegments);
    bool visualStateForCurrentContext(float &t, float &b, float &l, float &r) const;
    static void showNotification(const QString &message);
    QColor m_accentColor = QColor("#3D5AFE"); // Defaults in case QSS fails
    QColor m_secondaryColor = QColor("#FF3232");
    QColor m_backgroundColor = QColor("#080809");
    QColor m_trackColor = QColor("#1A1A1C");
    QColor m_waveformColor = QColor("#88888E");

    float getGainAtPos(qint64 posMs) {
        for (const auto& seg : segments) {
            if (posMs >= seg.startMs && posMs <= seg.endMs) {
                return seg.muted ? 0.0f : seg.gain;
            }
        }
        return 1.0f; // Default if not inside a segment
    }

signals:
    void playheadMoved(qint64 pos);
    void requestTogglePlayback();
    void clipTrimmed();
    void audioGainChanged(float gain);
    void audioTrackChanged(int index);
    void requestAudioTrackChange(int index);
    void mediaProbingFinished();
    void visualStateChanged(float t, float b, float l, float r);
    void zoomChanged(double zoomFactor);
    void overlaysChanged();
    void requestEditTextOverlay(int index);
    void sourceAppended(const QString &path);
    void exportStarted(const QString &label);
    void exportProgress(int percent);
    void exportFinished(bool success, const QString &message);
protected:
    void paintEvent(QPaintEvent* event) override;


    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        this->style()->unpolish(this);
        this->style()->polish(this);
    }
    bool isAnySelectedMuted();




    void updateEditorVolume();

private:
    struct TimelineState {
        QList<Segment> segments;
        QList<OverlayClip> overlays;
    };

    QList<Segment> segments;
    QList<TimelineState> undoStack;
    QList<TimelineState> redoStack;
    const int MAX_STACK_SIZE = 50;

    // Overlay lane geometry + drag state
    static constexpr int overlayLaneHeight = 20;
    static constexpr int overlayLaneGap = 3;
    int overlayLanesTop() const { return rulerHeight + 8; }
    int videoTrackTop() const;
    enum OverlayDragMode { OvNone, OvMove, OvStart, OvEnd };
    OverlayDragMode overlayDrag = OvNone;
    int overlayDragIdx = -1;
    qint64 overlayDragGrabOffsetMs = 0;
    int overlayIndexAt(const QPoint &pos, OverlayDragMode *edge = nullptr) const;

    void saveState();

    QMediaPlayer* thumbPlayer;
    QVideoSink* videoSink;
    QMap<int, QImage> thumbnailCache;
    // Filmstrip images (10 tiled frames) for appended sources, keyed by source index
    QMap<int, QImage> sourceFilmstrips;
    void ensureSourceFilmstrip(int sourceIdx);
    QQueue<int> thumbnailRequestQueue;
    bool thumbnailRequestActive = false;
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
        "Chromium + mic", "WEBRTC VoiceEngine + mic", "everything excluding discord", "Chromium only", "WEBRTC VoiceEngine only"
    };

    void loadAudioFast(const QString &path);
    void appendAudioWaveform(const QString &path);
    void processVideoFrame(const QVideoFrame &frame);
    void requestTimelineThumbnails();
    void requestNextTimelineThumbnail();
    int segmentIndexAtTime(qint64 timeMs) const;
    int activeVisualSegmentIndex() const;
    QSet<int> targetVisualSegments() const;
    void emitVisualStateForCurrentContext();
    void showClipContextMenu(const QPoint &globalPos, qint64 clickTime, int clickedIdx);


    void showProgressNotification(QProcess* process, qint64 totalMs, bool showCompletionToast = true);
    void detectAudioTracks(const QString &path);
    void resetMediaState();


    QString generateClippedName(const QString &extension) const;
    bool hasVideoStream = true;
    bool hasAudioStream = true;
    AutoCutSettings autoCutSettings;
    PlaybackSettings playbackSettings;
    ExportSettings exportSettings;
};

#endif // TIMELINEWIDGET_H
