#include "../Includes/timelinewidget.h"
#include "../Includes/mediautils.h"
#include <QPainter>
#include <QStyle>
#include <QFile>
#include <QVideoFrame>
#include <QTimer>
#include <QProcess>
#include <QCoreApplication>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QDir>
#include <algorithm>

namespace {
QString formatTimelineDuration(qint64 ms) {
    ms = qMax<qint64>(0, ms);
    if (ms >= 60000) {
        const qint64 totalSeconds = ms / 1000;
        return QString("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QString("%1.%2s").arg(ms / 1000).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
}

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    videoSink = new QVideoSink(this);
    thumbPlayer = new QMediaPlayer(this);
    thumbPlayer->setVideoOutput(videoSink);

    audioGain = 1.0f;
    selectedSegmentIdx = -1;
    zoomFactor = 1.0;
    scrollOffset = 0;
    durationMs = 0;

    this->setAttribute(Qt::WA_StyledBackground, true);
    this->ensurePolished();
    this->style()->unpolish(this);
    this->style()->polish(this);

    connect(videoSink, &QVideoSink::videoFrameChanged, this, &TimelineWidget::processVideoFrame);
}

void TimelineWidget::setCurrentPosition(qint64 ms) {
    const int oldSegment = segmentIndexAtTime(currentPosMs);
    currentPosMs = ms;
    const int newSegment = segmentIndexAtTime(currentPosMs);
    if (newSegment >= 0 && oldSegment != newSegment) {
        selectedSegmentIdx = newSegment;
        selectedSegmentIndices.clear();
        emitVisualStateForCurrentContext();
    }
    update();
}

void TimelineWidget::splitAtPlayhead() {
    const int splitGuard = playbackSettings.splitGuardMs;
    for (int i = 0; i < segments.size(); ++i) {
        if (currentPosMs > segments[i].startMs + splitGuard && currentPosMs < segments[i].endMs - splitGuard) {
            Segment splitSegment = segments[i];
            qint64 originalEnd = splitSegment.endMs;
            segments[i].endMs = currentPosMs;
            splitSegment.startMs = currentPosMs;
            splitSegment.endMs = originalEnd;
            segments.insert(i + 1, splitSegment);
            selectedSegmentIdx = i + 1;
            showNotification("CLIP SPLIT ✂️");
            emit clipTrimmed();
            emitVisualStateForCurrentContext();
            update();
            return;
        }
    }
    // Nothing was split — tell the user why instead of failing silently.
    const int nearIdx = segmentIndexAtTime(currentPosMs);
    if (nearIdx >= 0) {
        showNotification("TOO CLOSE TO A CLIP EDGE TO SPLIT");
    } else {
        showNotification("MOVE THE PLAYHEAD INSIDE A CLIP TO SPLIT");
    }
}

// ============================ Overlay clips ============================

void TimelineWidget::addOverlayAt(int type, qint64 timeMs) {
    if (durationMs <= 0) return;
    saveState("Add overlay");

    OverlayClip clip;
    clip.type = type;
    clip.startMs = qBound<qint64>(0, timeMs, qMax<qint64>(0, durationMs - 500));
    clip.endMs = qMin(durationMs, clip.startMs + 3000);
    if (type == 3) {
        clip.l = 0.30f; clip.t = 0.40f; clip.r = 0.70f; clip.b = 0.55f;
        clip.text = "Your text";
    } else if (type == 4) {
        clip.l = 0.30f; clip.t = 0.30f; clip.r = 0.70f; clip.b = 0.60f;
    } else if (type == 5) {
        clip.l = 0.0f; clip.t = 0.0f; clip.r = 1.0f; clip.b = 1.0f;
    }
    overlays.append(clip);
    selectedOverlayIdx = overlays.size() - 1;

    relayout();
    update();
    emit overlaysChanged();
    if (type == 3) emit requestEditTextOverlay(selectedOverlayIdx);
    else if (type == 4 || type == 5) emit requestEditOverlayProperties(selectedOverlayIdx);
}

void TimelineWidget::deleteSelectedOverlay() {
    if (selectedOverlayIdx < 0 || selectedOverlayIdx >= overlays.size()) return;
    saveState("Delete overlay");
    overlays.removeAt(selectedOverlayIdx);
    selectedOverlayIdx = -1;
    showNotification("OVERLAY DELETED");
    relayout();
    update();
    emit overlaysChanged();
}

void TimelineWidget::toggleMarkerAtPlayhead() {
    if (durationMs <= 0) return;
    const qint64 tolerance = 300;
    for (int i = 0; i < markers.size(); ++i) {
        if (std::abs(markers[i] - currentPosMs) <= tolerance) {
            saveState("Remove marker");
            markers.removeAt(i);
            showNotification("MARKER REMOVED");
            update();
            return;
        }
    }
    saveState("Add marker");
    markers.append(currentPosMs);
    std::sort(markers.begin(), markers.end());
    showNotification("MARKER ADDED");
    update();
}

// Snaps a candidate time to the nearest playhead/marker/segment-edge within a
// small pixel tolerance, so dragging a trim handle or overlay edge feels
// magnetic near those reference points instead of requiring pixel-perfect aim.
qint64 TimelineWidget::snappedTime(qint64 t, double pxPerMs) const {
    const qint64 toleranceMs = static_cast<qint64>(8.0 / qMax(0.0001, pxPerMs));
    qint64 best = t;
    qint64 bestDist = toleranceMs + 1;
    auto consider = [&](qint64 candidate) {
        const qint64 dist = std::abs(candidate - t);
        if (dist <= toleranceMs && dist < bestDist) { bestDist = dist; best = candidate; }
    };
    consider(currentPosMs);
    for (qint64 m : markers) consider(m);
    for (const auto &seg : segments) { consider(seg.startMs); consider(seg.endMs); }
    return best;
}

QList<int> TimelineWidget::overlaysAtTime(qint64 timeMs) const {
    QList<int> hits;
    for (int i = 0; i < overlays.size(); ++i) {
        if (timeMs >= overlays[i].startMs && timeMs < overlays[i].endMs) hits.append(i);
    }
    return hits;
}

// Greedy first-fit lane assignment so overlapping overlays stack instead of
// drawing on top of each other (like calendar events).
QVector<int> TimelineWidget::computeOverlayLanes() const {
    QVector<int> lanes(overlays.size(), 0);
    QList<int> order;
    for (int i = 0; i < overlays.size(); ++i) order.append(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return overlays[a].startMs < overlays[b].startMs;
    });

    QList<qint64> laneEnds;
    for (int idx : order) {
        int lane = -1;
        for (int l = 0; l < laneEnds.size(); ++l) {
            if (overlays[idx].startMs >= laneEnds[l]) { lane = l; break; }
        }
        if (lane == -1) {
            laneEnds.append(0);
            lane = laneEnds.size() - 1;
        }
        laneEnds[lane] = overlays[idx].endMs;
        lanes[idx] = lane;
    }
    return lanes;
}

int TimelineWidget::overlayLaneCount() const {
    if (overlays.isEmpty()) return 0;
    const QVector<int> lanes = computeOverlayLanes();
    int maxLane = 0;
    for (int l : lanes) maxLane = qMax(maxLane, l);
    return maxLane + 1;
}

int TimelineWidget::videoTrackTop() const {
    const int lanes = overlayLaneCount();
    return overlayLanesTop() + (lanes > 0 ? lanes * (overlayLaneHeight + overlayLaneGap) + 6 : 2);
}

// Keeps the widget tall enough for every lane + both tracks so the scroll
// area around it can always reach the audio waveform, no matter how many
// overlay lanes exist.
void TimelineWidget::relayout() {
    const int vTop = videoTrackTop() + 8;
    const int contentBottom = vTop + trackHeight + 15 + trackHeight + 18;
    setMinimumHeight(qMax(180, contentBottom));
    updateGeometry();
    update();
}

double TimelineWidget::estimatedExportSizeMB() const {
    if (sources.isEmpty()) return 0.0;
    double estBytes = 0.0;
    for (const auto &seg : segments) {
        const int srcIdx = qBound(0, seg.sourceIdx, static_cast<int>(sources.size()) - 1);
        const auto &src = sources[srcIdx];
        if (src.durationMs <= 0) continue;
        const double timeRatio = static_cast<double>(seg.endMs - seg.startMs) / src.durationMs;
        const double spatial = (seg.cropRight - seg.cropLeft) * (seg.cropBottom - seg.cropTop);
        estBytes += src.fileSizeBytes * timeRatio * qBound(0.0, static_cast<double>(spatial), 1.0);
    }
    return estBytes / (1024.0 * 1024.0);
}

int TimelineWidget::overlayIndexAt(const QPoint &pos, OverlayDragMode *edge) const {
    if (edge) *edge = OvNone;
    if (overlays.isEmpty() || durationMs <= 0) return -1;

    const int viewWidth = width() - sidebarWidth;
    const double pxPerMs = static_cast<double>(viewWidth) * zoomFactor / durationMs;
    const int drawX = pos.x() - sidebarWidth + scrollOffset;
    const QVector<int> lanes = computeOverlayLanes();

    for (int i = overlays.size() - 1; i >= 0; --i) {
        const int laneY = overlayLanesTop() + lanes[i] * (overlayLaneHeight + overlayLaneGap);
        if (pos.y() < laneY || pos.y() > laneY + overlayLaneHeight) continue;
        const int x1 = static_cast<int>(overlays[i].startMs * pxPerMs);
        const int x2 = static_cast<int>(overlays[i].endMs * pxPerMs);
        if (drawX < x1 - 6 || drawX > x2 + 6) continue;
        if (edge) {
            if (qAbs(drawX - x1) < 8) *edge = OvStart;
            else if (qAbs(drawX - x2) < 8) *edge = OvEnd;
            else *edge = OvMove;
        }
        return i;
    }
    return -1;
}

// ============================ Multi-source ============================

// Loads (or generates, via the same ffmpeg tile command the media bin uses)
// a 10-frame filmstrip for an appended source so its timeline clip shows
// thumbnails like the primary clip does.
void TimelineWidget::ensureSourceFilmstrip(int sourceIdx) {
    if (sourceIdx <= 0 || sourceIdx >= sources.size()) return;
    if (sourceFilmstrips.contains(sourceIdx)) return;

    const QString path = sources[sourceIdx].path;
    const QString cachePath = QDir::tempPath() + "/potato_cache_" + QFileInfo(path).baseName() + ".jpg";

    QImage strip;
    if (QFile::exists(cachePath) && strip.load(cachePath)) {
        sourceFilmstrips[sourceIdx] = strip;
        update();
        return;
    }

    auto *ffmpeg = new QProcess(this);
    QStringList args;
    args << "-y" << "-ss" << "0" << "-t" << "10" << "-i" << path
         << "-vf" << "fps=1,scale=160:-1,tile=10x1"
         << "-frames:v" << "1" << "-preset" << "ultrafast" << cachePath;
    connect(ffmpeg, &QProcess::finished, this, [this, ffmpeg, sourceIdx, cachePath]() {
        QImage loaded;
        if (loaded.load(cachePath)) {
            sourceFilmstrips[sourceIdx] = loaded;
            update();
        }
        ffmpeg->deleteLater();
    });
#ifdef Q_OS_WIN
    ffmpeg->start(QCoreApplication::applicationDirPath() + "/ffmpeg.exe", args);
#else
    ffmpeg->start("ffmpeg", args);
#endif
}

int TimelineWidget::sourceIndexForTimelineTime(qint64 timeMs) const {
    for (int i = sources.size() - 1; i >= 0; --i) {
        if (timeMs >= sources[i].offsetMs) return i;
    }
    return 0;
}

void TimelineWidget::appendMediaSource(const QString &path) {
    if (sources.isEmpty() || durationMs <= 0) {
        showNotification("LOAD A CLIP FIRST");
        return;
    }
    for (const auto &src : sources) {
        if (src.path == path) {
            showNotification("THAT FILE IS ALREADY ON THE TIMELINE");
            return;
        }
    }

    showNotification("ADDING CLIP TO TIMELINE…");
    auto *probe = new QProcess(this);
    QStringList args;
    args << "-v" << "error"
         << "-show_entries" << "format=duration"
         << "-show_entries" << "stream=codec_type"
         << "-of" << "default=noprint_wrappers=1" << path;

    connect(probe, &QProcess::finished, this, [this, probe, path](int exitCode) {
        probe->deleteLater();
        const QString out = probe->readAllStandardOutput();
        double durationSec = 0.0;
        bool srcHasVideo = false, srcHasAudio = false;
        for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
            if (line.startsWith("duration=")) durationSec = line.mid(9).toDouble();
            else if (line.trimmed() == "codec_type=video") srcHasVideo = true;
            else if (line.trimmed() == "codec_type=audio") srcHasAudio = true;
        }
        if (exitCode != 0 || durationSec <= 0.05 || !srcHasVideo) {
            showNotification("COULDN'T ADD THAT FILE (NEEDS A VIDEO STREAM)");
            return;
        }

        saveState("Add media source");
        SourceClip src;
        src.path = path;
        src.offsetMs = durationMs;
        src.durationMs = static_cast<qint64>(durationSec * 1000.0);
        src.fileSizeBytes = QFileInfo(path).size();
        src.hasVideo = srcHasVideo;
        src.hasAudio = srcHasAudio;
        sources.append(src);
        ensureSourceFilmstrip(sources.size() - 1);

        Segment seg;
        seg.startMs = src.offsetMs;
        seg.endMs = src.offsetMs + src.durationMs;
        seg.sourceIdx = sources.size() - 1;
        seg.cropTop = 0.0f; seg.cropBottom = 1.0f;
        seg.cropLeft = 0.0f; seg.cropRight = 1.0f;
        segments.append(seg);

        durationMs += src.durationMs;
        if (src.hasAudio) appendAudioWaveform(path);

        resetZoomView();
        showNotification("CLIP ADDED TO TIMELINE 🎬");
        emit sourceAppended(path);
        emit clipTrimmed();
        relayout();
    });

#ifdef Q_OS_WIN
    probe->start(QCoreApplication::applicationDirPath() + "/ffprobe.exe", args);
#else
    probe->start("ffprobe", args);
#endif
}

void TimelineWidget::deleteSelectedSegment() {
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) {
        saveState("Delete clip");
        segments.removeAt(selectedSegmentIdx);
        selectedSegmentIdx = -1;
        showNotification("CLIP DELETED 🗑️");
        validatePlayheadPosition();
        emit clipTrimmed();
        update();
    }
}

void TimelineWidget::deleteActiveSelection() {
    QSet<int> toDelete = selectedSegmentIndices;
    if (selectedSegmentIdx != -1) toDelete.insert(selectedSegmentIdx);
    if (toDelete.isEmpty()) return;

    saveState("Delete selection");

    QList<int> sortedIndices = toDelete.values();
    std::sort(sortedIndices.begin(), sortedIndices.end(), std::greater<int>());
    for (int idx : sortedIndices) {
        if (idx >= 0 && idx < static_cast<int>(segments.size())) {
            segments.erase(segments.begin() + idx);
        }
    }

    selectedSegmentIndices.clear();
    selectedSegmentIdx = -1;
    showNotification(QString("DELETED %1 CLIPS").arg(sortedIndices.size()));
    validatePlayheadPosition();
    emit clipTrimmed();
    update();
}

void TimelineWidget::validatePlayheadPosition() {
    if (segments.isEmpty()) return;

    int currentClipIdx = -1;
    for (int i = 0; i < segments.size(); ++i) {
        if (currentPosMs >= segments[i].startMs && currentPosMs < segments[i].endMs) {
            currentClipIdx = i;
            break;
        }
    }

    if (currentClipIdx == -1) {
        qint64 nextStart = -1;
        for (const auto& clip : segments) {
            if (clip.startMs >= currentPosMs) {
                nextStart = clip.startMs;
                break;
            }
        }

        if (nextStart != -1) {
            currentPosMs = nextStart;
        } else {
            currentPosMs = segments.first().startMs;
        }
        emitVisualStateForCurrentContext();
        emit playheadMoved(currentPosMs);
    }
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get Device Pixel Ratio for Windows High DPI Scaling
    double dpr = this->devicePixelRatioF();

    QColor accent = m_accentColor;
    QColor secondary = m_secondaryColor;
    QColor bg = m_backgroundColor;

    // ALWAYS draw background so widget is never invisible
    painter.fillRect(rect(), bg);

    int viewWidth = width() - sidebarWidth;
    int contentWidth = viewWidth * zoomFactor;
    const int vTop = videoTrackTop() + 8;
    const int aTop = vTop + trackHeight + 15;

    // Safety check: if no duration, don't draw clips
    if (durationMs <= 0 || segments.isEmpty()) return;

    painter.save();
    painter.setClipRect(sidebarWidth, 0, width() - sidebarWidth, height());
    painter.translate(sidebarWidth - scrollOffset, 0);

    double pxPerMs = static_cast<double>(contentWidth) / durationMs;

    // --- Lane bands: separate video/audio lanes like an NLE timeline ---
    painter.fillRect(QRectF(0, vTop - 6, contentWidth, trackHeight + 12), m_trackColor.lighter(112));
    if (!audioSamples.empty()) {
        painter.fillRect(QRectF(0, aTop - 6, contentWidth, trackHeight + 12), m_trackColor);
    }

    // --- Overlay lanes (effects / text) above the video track ---
    if (!overlays.isEmpty()) {
        const QVector<int> lanes = computeOverlayLanes();
        const int laneCount = overlayLaneCount();
        for (int l = 0; l < laneCount; ++l) {
            const int y = overlayLanesTop() + l * (overlayLaneHeight + overlayLaneGap);
            painter.fillRect(QRectF(0, y, contentWidth, overlayLaneHeight), m_trackColor.darker(108));
        }

        QFont ovFont = painter.font();
        ovFont.setPointSizeF(7.5);
        ovFont.setBold(true);
        painter.setFont(ovFont);

        for (int i = 0; i < overlays.size(); ++i) {
            const auto &ov = overlays[i];
            const int y = overlayLanesTop() + lanes[i] * (overlayLaneHeight + overlayLaneGap);
            QRectF r(ov.startMs * pxPerMs, y, qMax(6.0, (ov.endMs - ov.startMs) * pxPerMs), overlayLaneHeight);

            // Per-type hue so lanes read at a glance
            QColor base;
            QString label;
            switch (ov.type) {
                case 0:  base = QColor("#5B8DEF"); label = "BLUR"; break;
                case 1:  base = QColor("#9B6BE8"); label = "PIXEL"; break;
                case 2:  base = QColor("#666B75"); label = "BLACK"; break;
                default: base = QColor("#3FB68B");
                         label = ov.text.isEmpty() ? "TEXT" : ov.text.left(24).toUpper(); break;
            }
            const bool sel = (i == selectedOverlayIdx);
            QColor fill = base; fill.setAlpha(sel ? 200 : 120);
            painter.setPen(sel ? QPen(Qt::white, 1.4) : QPen(base.lighter(115), 1));
            painter.setBrush(fill);
            painter.drawRoundedRect(r, 6, 6);

            // Trim handles on the selected overlay
            if (sel) {
                painter.setBrush(Qt::white);
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(QRectF(r.left() + 2, r.top() + 4, 3, r.height() - 8), 1.5, 1.5);
                painter.drawRoundedRect(QRectF(r.right() - 5, r.top() + 4, 3, r.height() - 8), 1.5, 1.5);
            }

            painter.setPen(QColor(255, 255, 255, sel ? 255 : 210));
            painter.drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             painter.fontMetrics().elidedText(label, Qt::ElideRight, static_cast<int>(r.width()) - 14));
        }
    }

    // --- Ruler: tick marks + timecodes along the top, like an NLE ruler ---
    {
        static const qint64 kNiceIntervalsMs[] = {40, 100, 200, 500, 1000, 2000, 5000, 10000, 30000, 60000, 120000, 300000, 600000};
        qint64 interval = kNiceIntervalsMs[0];
        for (qint64 candidate : kNiceIntervalsMs) {
            interval = candidate;
            if (candidate * pxPerMs >= 76) break;
        }

        QFont tickFont = painter.font();
        tickFont.setPointSizeF(7.5);
        painter.setFont(tickFont);

        for (qint64 t = 0; t <= durationMs; t += interval) {
            const int x = static_cast<int>(t * pxPerMs);
            painter.setPen(QPen(QColor(255, 255, 255, 55), 1));
            painter.drawLine(x, rulerHeight - 8, x, rulerHeight);
            painter.setPen(QColor(255, 255, 255, 115));
            painter.drawText(QRect(x + 4, 1, 100, rulerHeight - 2), Qt::AlignLeft | Qt::AlignVCenter, formatTimelineDuration(t));
        }
        painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
        painter.drawLine(0, rulerHeight, contentWidth, rulerHeight);
    }

    // --- Markers: small triangles in the ruler, snap targets for trims/overlays ---
    if (!markers.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        for (qint64 m : markers) {
            const int x = static_cast<int>(m * pxPerMs);
            QPolygon tri;
            tri << QPoint(x - 5, rulerHeight - 1) << QPoint(x + 5, rulerHeight - 1) << QPoint(x, rulerHeight - 9);
            painter.drawPolygon(tri);
        }
    }

    for (int i = 0; i < segments.size(); ++i) {
        QRectF clipRect(segments[i].startMs * pxPerMs, vTop, (segments[i].endMs - segments[i].startMs) * pxPerMs, trackHeight);
        bool isSel = (i == selectedSegmentIdx) || selectedSegmentIndices.contains(i);

        if (isSel) {
            painter.setPen(QPen(accent, 2.5));
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        } else {
            painter.setPen(QPen(accent.darker(160), 1.0));
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 25));
        }

        painter.drawRoundedRect(clipRect, 6, 6);

        if (!thumbnailCache.isEmpty() && segments[i].sourceIdx == 0) {
            painter.save();
            painter.setClipRect(clipRect.adjusted(2, 2, -2, -2));
            painter.setOpacity(isSel ? 0.62 : 0.42);
            const int thumbW = 88;
            for (int x = static_cast<int>(clipRect.left()) + 4; x < clipRect.right(); x += thumbW) {
                const qint64 timeAtX = qBound<qint64>(segments[i].startMs,
                    static_cast<qint64>(x / pxPerMs), segments[i].endMs);
                const int sec = static_cast<int>(timeAtX / 1000);
                if (!thumbnailCache.contains(sec)) continue;
                QRect target(x, static_cast<int>(clipRect.top()) + 3, thumbW - 4, trackHeight - 6);
                painter.drawImage(target, thumbnailCache.value(sec));
            }
            painter.restore();
        }

        // Appended clips: filmstrip slices (10 frames over the source's first
        // 10 s) + the source filename as a label.
        if (segments[i].sourceIdx > 0 && segments[i].sourceIdx < sources.size()) {
            const int srcIdx = segments[i].sourceIdx;
            if (sourceFilmstrips.contains(srcIdx)) {
                const QImage &strip = sourceFilmstrips[srcIdx];
                const int frameW = qMax(1, strip.width() / 10);
                painter.save();
                painter.setClipRect(clipRect.adjusted(2, 2, -2, -2));
                painter.setOpacity(isSel ? 0.62 : 0.42);
                const int thumbW = 88;
                for (int x = static_cast<int>(clipRect.left()) + 4; x < clipRect.right(); x += thumbW) {
                    const qint64 timeAtX = qBound<qint64>(segments[i].startMs,
                        static_cast<qint64>(x / pxPerMs), segments[i].endMs);
                    const int localSec = static_cast<int>((timeAtX - sources[srcIdx].offsetMs) / 1000);
                    const int frameIdx = qBound(0, localSec, 9);
                    QRect target(x, static_cast<int>(clipRect.top()) + 3, thumbW - 4, trackHeight - 6);
                    painter.drawImage(target, strip, QRect(frameIdx * frameW, 0, frameW, strip.height()));
                }
                painter.restore();
            }
            painter.save();
            QFont srcFont = painter.font();
            srcFont.setPointSizeF(8);
            srcFont.setBold(true);
            painter.setFont(srcFont);
            painter.setPen(QColor(255, 255, 255, 190));
            const QString name = QFileInfo(sources[segments[i].sourceIdx].path).fileName();
            painter.drawText(clipRect.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             painter.fontMetrics().elidedText(name, Qt::ElideMiddle, static_cast<int>(clipRect.width()) - 20));
            painter.restore();
        }

        if (segments[i].cropTop > 0.001f || segments[i].cropBottom < 0.999f ||
            segments[i].cropLeft > 0.001f || segments[i].cropRight < 0.999f) {
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 95));
            painter.drawRoundedRect(QRectF(clipRect.left() + 6, clipRect.top() + 6, 36, 14), 4, 4);
            QFont fxFont = painter.font();
            fxFont.setPointSizeF(7);
            fxFont.setBold(true);
            painter.setFont(fxFont);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(clipRect.left() + 6, clipRect.top() + 5, 36, 15), Qt::AlignCenter, "CROP");
            painter.restore();
        }

        // Draw Gain Label (Visual Feedback)
        if (segments[i].gain != 1.0f) {
            painter.save();
            painter.setPen(Qt::white);
            QFont labelFont = painter.font(); labelFont.setPointSizeF(7);
            painter.setFont(labelFont);
            painter.drawText(clipRect.adjusted(5, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                             QString("%1x").arg(segments[i].gain, 0, 'f', 1));
            painter.restore();
        }

        // Waveforms using segment-specific gain
        if (!audioSamples.empty()) {
            QColor currentWaveColor = isSel ? accent : accent.darker(180);
            painter.setPen(QPen(currentWaveColor, 1));

            int startIdx = (segments[i].startMs * audioSamples.size()) / durationMs;
            int endIdx = (segments[i].endMs * audioSamples.size()) / durationMs;

            // PERFORMANCE OPTIMIZATION: Only draw one line per pixel to save CPU/GPU cycles
            // especially important on 4K displays where the timeline can be very wide.
            double samplesPerPixel = (double)audioSamples.size() / contentWidth;
            int step = qMax(1, (int)samplesPerPixel);

            for (int s = startIdx; s < endIdx && s < (int)audioSamples.size(); s += step) {
                int x = (s * (double)contentWidth) / audioSamples.size();
                
                // Find max in this pixel range for a better visual representation
                float maxInStep = 0.0f;
                for (int j = 0; j < step && (s + j) < endIdx && (s + j) < (int)audioSamples.size(); ++j) {
                    maxInStep = qMax(maxInStep, audioSamples[s + j]);
                }

                float norm = (maxInStep / maxAmplitude) * segments[i].gain;
                int h = qMin((float)trackHeight, norm * (trackHeight - 10));
                painter.drawLine(x, aTop + (trackHeight/2) - h/2, x, aTop + (trackHeight/2) + h/2);
            }
        }
    }

    // Playhead
    int playheadX = currentPosMs * pxPerMs;
    QColor pulseColor = secondary;
    pulseColor.setAlphaF(pulseAlpha);

    painter.setPen(Qt::NoPen);
    painter.setBrush(pulseColor);
    QPolygon arrow;
    arrow << QPoint(playheadX - 8, 0) << QPoint(playheadX + 8, 0) << QPoint(playheadX, 12);
    painter.drawPolygon(arrow);

    painter.setPen(QPen(pulseColor, 2));
    painter.drawLine(playheadX, 12, playheadX, height());
    painter.restore();

}

void TimelineWidget::updateCropValues(float t, float b, float l, float r) {
    cropTop = t;
    cropBottom = b;
    cropLeft = l;
    cropRight = r;
    for (int idx : targetVisualSegments()) {
        if (idx >= 0 && idx < segments.size()) {
            segments[idx].cropTop = cropTop;
            segments[idx].cropBottom = cropBottom;
            segments[idx].cropLeft = cropLeft;
            segments[idx].cropRight = cropRight;
        }
    }
    update();
}

int TimelineWidget::segmentIndexAtTime(qint64 timeMs) const {
    for (int i = 0; i < segments.size(); ++i) {
        if (timeMs >= segments[i].startMs && timeMs <= segments[i].endMs) return i;
    }
    return -1;
}

int TimelineWidget::activeVisualSegmentIndex() const {
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) return selectedSegmentIdx;
    if (!selectedSegmentIndices.isEmpty()) return *selectedSegmentIndices.constBegin();
    return segmentIndexAtTime(currentPosMs);
}

QSet<int> TimelineWidget::targetVisualSegments() const {
    QSet<int> targets = selectedSegmentIndices;
    if (selectedSegmentIdx >= 0 && selectedSegmentIdx < segments.size()) targets.insert(selectedSegmentIdx);
    if (targets.isEmpty()) {
        const int activeIdx = segmentIndexAtTime(currentPosMs);
        if (activeIdx >= 0) targets.insert(activeIdx);
    }
    return targets;
}

void TimelineWidget::applyCurrentVisualsToSelection(bool allSegments) {
    saveState("Apply crop");
    QSet<int> targets;
    if (allSegments) {
        for (int i = 0; i < segments.size(); ++i) targets.insert(i);
    } else {
        targets = targetVisualSegments();
    }

    for (int idx : targets) {
        if (idx < 0 || idx >= segments.size()) continue;
        segments[idx].cropTop = cropTop;
        segments[idx].cropBottom = cropBottom;
        segments[idx].cropLeft = cropLeft;
        segments[idx].cropRight = cropRight;
    }

    showNotification(allSegments ? "CROP APPLIED TO ALL CLIPS" : "CROP APPLIED TO CLIP");
    emit clipTrimmed();
    update();
}

void TimelineWidget::clearVisualsForSelection(bool allSegments) {
    saveState("Clear crop");
    QSet<int> targets;
    if (allSegments) {
        for (int i = 0; i < segments.size(); ++i) targets.insert(i);
    } else {
        targets = targetVisualSegments();
    }

    for (int idx : targets) {
        if (idx < 0 || idx >= segments.size()) continue;
        segments[idx].cropTop = 0.0f;
        segments[idx].cropBottom = 1.0f;
        segments[idx].cropLeft = 0.0f;
        segments[idx].cropRight = 1.0f;
    }

    emitVisualStateForCurrentContext();
    showNotification(allSegments ? "CLEARED ALL CROPS" : "CLEARED CLIP CROP");
    emit clipTrimmed();
    update();
}

void TimelineWidget::applySpeedRampToSelection(float speedStart, float speedEnd, bool allSegments) {
    saveState("Apply speed ramp");
    QSet<int> targets;
    if (allSegments) {
        for (int i = 0; i < segments.size(); ++i) targets.insert(i);
    } else {
        targets = targetVisualSegments();
    }

    for (int idx : targets) {
        if (idx < 0 || idx >= segments.size()) continue;
        segments[idx].speedStart = qMax(0.1f, speedStart);
        segments[idx].speedEnd = qMax(0.1f, speedEnd);
    }

    showNotification(allSegments ? "SPEED RAMP APPLIED TO ALL CLIPS" : "SPEED RAMP APPLIED TO CLIP");
    emit clipTrimmed();
    update();
}

bool TimelineWidget::visualStateForCurrentContext(float &t, float &b, float &l, float &r) const {
    const int idx = activeVisualSegmentIndex();
    if (idx < 0 || idx >= segments.size()) return false;
    const auto &seg = segments[idx];
    t = seg.cropTop;
    b = seg.cropBottom;
    l = seg.cropLeft;
    r = seg.cropRight;
    return true;
}

void TimelineWidget::emitVisualStateForCurrentContext() {
    float t, b, l, r;
    if (visualStateForCurrentContext(t, b, l, r)) {
        cropTop = t;
        cropBottom = b;
        cropLeft = l;
        cropRight = r;
        emit visualStateChanged(t, b, l, r);
    }
}

void TimelineWidget::setZoomFactor(double z) {
    const double oldZoom = zoomFactor;
    zoomFactor = qBound(1.0, z, 100.0);
    const int viewWidth = width() - sidebarWidth;
    scrollOffset = qBound(0, static_cast<int>(scrollOffset / qMax(0.0001, oldZoom) * zoomFactor),
                          qMax(0, static_cast<int>(viewWidth * zoomFactor) - viewWidth));
    emit zoomChanged(zoomFactor);
    update();
}

void TimelineWidget::resetZoomView() {
    zoomFactor = 1.0;
    scrollOffset = 0;
    emit zoomChanged(zoomFactor);
    update();
}

void TimelineWidget::forceFitToDuration() {
    if (durationMs > 0) {
        this->zoomFactor = 1.0;
        this->scrollOffset = 0;

        if (!segments.isEmpty()) {
            segments[0].startMs = 0;
            segments[0].endMs = durationMs;
        }

        emit clipTrimmed();
        this->update();
    }
}

// ============================ Drag & drop ============================
// Media files dropped on the timeline are APPENDED as extra clips (unlike
// dropping anywhere else in the window, which replaces the loaded media).
// Effect buttons dragged from the inspector land as overlay clips at the
// drop position.

void TimelineWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-potato-overlay") ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-potato-overlay") ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void TimelineWidget::dropEvent(QDropEvent *event) {
    const double pxPerMs = static_cast<double>(width() - sidebarWidth) * zoomFactor / qMax<qint64>(1, durationMs);
    const qint64 dropTime = qBound<qint64>(0,
        static_cast<qint64>((event->position().x() - sidebarWidth + scrollOffset) / qMax(0.0001, pxPerMs)),
        durationMs);

    if (event->mimeData()->hasFormat("application/x-potato-overlay")) {
        const int type = event->mimeData()->data("application/x-potato-overlay").toInt();
        addOverlayAt(type, dropTime);
        event->acceptProposedAction();
        return;
    }

    if (event->mimeData()->hasUrls()) {
        const QString path = event->mimeData()->urls().first().toLocalFile();
        if (!path.isEmpty() && MediaUtils::isSupportedMediaFile(path)) {
            appendMediaSource(path);
        }
        event->acceptProposedAction();
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    OverlayDragMode edge;
    const int idx = overlayIndexAt(event->pos(), &edge);
    if (idx != -1 && overlays[idx].type == 3) {
        selectedOverlayIdx = idx;
        update();
        emit overlaysChanged();
        emit requestEditTextOverlay(idx);
        return;
    }
    if (idx != -1 && (overlays[idx].type == 4 || overlays[idx].type == 5)) {
        selectedOverlayIdx = idx;
        update();
        emit overlaysChanged();
        emit requestEditOverlayProperties(idx);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
