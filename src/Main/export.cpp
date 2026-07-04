#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTime>
#include <QImage>
#include <QPainter>
#include <functional>
#include <cmath>

#include "../Includes/timelinewidget.h"
#include "../Includes/mediaSource.h"
#include "../Includes/appsettings.h"
#include "../Includes/overlayShapes.h"

static QString getFFmpegPath() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}

static bool hasNvidiaEncoder() {
    QProcess probe;
    probe.start(getFFmpegPath(), {"-encoders"});
    probe.waitForFinished(2000);
    return probe.readAllStandardOutput().contains("h264_nvenc");
}

static QString getExportDir() {
    QSettings settings = makeAppSettings();
    QString path = settings.value("export/exportDirectory",
                                  QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Edited").toString();
    QDir().mkpath(path);
    return path;
}

static QString escapeDrawtext(QString text) {
    text.replace('\\', "\\\\");
    // A typographic apostrophe avoids the ffmpeg quote-escaping maze entirely.
    text.replace('\'', QString::fromUtf8("’"));
    text.replace(':', "\\:");
    text.replace('%', "\\%");
    return text;
}

// Builds the effect chain for one timeline segment. Every overlay clip that
// intersects the segment is applied with enable='between(t,a,b)' so it only
// shows for its own time range (t is segment-local after trim+setpts).
static QString buildOverlayChain(const QString &inputLabel,
                                 const QString &outputLabel,
                                 const QString &prefix,
                                 int vidW,
                                 int vidH,
                                 qint64 segStartMs,
                                 qint64 segEndMs,
                                 const QList<TimelineWidget::OverlayClip> &overlays) {
    QString chain;
    QString lastOutput = inputLabel;
    int step = 0;

    for (const auto &ov : overlays) {
        const qint64 isectStart = qMax(ov.startMs, segStartMs);
        const qint64 isectEnd = qMin(ov.endMs, segEndMs);
        if (isectEnd <= isectStart) continue;

        const double a = (isectStart - segStartMs) / 1000.0;
        const double b = (isectEnd - segStartMs) / 1000.0;
        const QString enable = QString("enable='between(t,%1,%2)'")
                                   .arg(a, 0, 'f', 3).arg(b, 0, 'f', 3);

        // Even numbers required for yuv420p subsampling.
        const int absX = qRound(vidW * ov.l) & ~1;
        const int absY = qRound(vidH * ov.t) & ~1;
        const int absW = qRound(vidW * (ov.r - ov.l)) & ~1;
        const int absH = qRound(vidH * (ov.b - ov.t)) & ~1;
        if (ov.type != 3 && (absW <= 0 || absH <= 0)) continue;

        const QString cur = QString("[%1_s%2]").arg(prefix).arg(step);

        if (ov.type == 0 || ov.type == 1) {
            const QString base = QString("[%1_b%2]").arg(prefix).arg(step);
            const QString mask = QString("[%1_m%2]").arg(prefix).arg(step);
            const QString fx = QString("[%1_f%2]").arg(prefix).arg(step);
            const QString effect = ov.type == 0
                ? QString("boxblur=20")
                : QString("scale=iw/30:-1,scale=%1:%2:flags=neighbor").arg(absW).arg(absH);
            chain += lastOutput + "split=2" + base + mask + ";"
                   + mask + QString("crop=%1:%2:%3:%4,").arg(absW).arg(absH).arg(absX).arg(absY) + effect + fx + ";"
                   + base + fx + QString("overlay=%1:%2:").arg(absX).arg(absY) + enable + cur + ";";
        } else if (ov.type == 2) {
            chain += lastOutput
                   + QString("drawbox=x=%1:y=%2:w=%3:h=%4:color=black:t=fill:").arg(absX).arg(absY).arg(absW).arg(absH)
                   + enable + cur + ";";
        } else if (ov.type == 5) { // color correction: same crop/process/overlay shape as blur/pixelate
            const QString base = QString("[%1_b%2]").arg(prefix).arg(step);
            const QString mask = QString("[%1_m%2]").arg(prefix).arg(step);
            const QString fx = QString("[%1_f%2]").arg(prefix).arg(step);
            const QString eq = QString("eq=brightness=%1:contrast=%2:saturation=%3")
                                    .arg(ov.brightness, 0, 'f', 3).arg(ov.contrast, 0, 'f', 3).arg(ov.saturation, 0, 'f', 3);
            chain += lastOutput + "split=2" + base + mask + ";"
                   + mask + QString("crop=%1:%2:%3:%4,").arg(absW).arg(absH).arg(absX).arg(absY) + eq + fx + ";"
                   + base + fx + QString("overlay=%1:%2:").arg(absX).arg(absY) + enable + cur + ";";
        } else if (ov.type == 4) { // shape/arrow: no native ffmpeg ellipse/arrow filter, so bake one
            // frame's worth of the shape to a transparent PNG (matching the live
            // preview's QPainter rendering exactly) and overlay that image.
            QImage shapeImg(vidW, vidH, QImage::Format_ARGB32_Premultiplied);
            shapeImg.fill(Qt::transparent);
            {
                QPainter sp(&shapeImg);
                OverlayShapes::paint(sp, QRectF(absX, absY, absW, absH), ov.shapeKind, ov.shapeColor, ov.shapeThickness);
            }
            const QString shapePath = QDir::toNativeSeparators(
                QDir::tempPath() + QString("/potato_shape_%1_%2.png").arg(prefix).arg(step));
            shapeImg.save(shapePath, "PNG");
            QString moviePath = shapePath;
            moviePath.replace('\\', '/').replace(":", "\\:").replace("'", "\\'");

            const QString shapeSrc = QString("[%1_shp%2]").arg(prefix).arg(step);
            chain += QString("movie='%1'").arg(moviePath) + shapeSrc + ";";
            chain += lastOutput + shapeSrc + "overlay=0:0:" + enable + cur + ";";
        } else { // text
            const int fontSize = qMax(14, qRound(vidH * (ov.b - ov.t) * 0.6));
            const double cx = (ov.l + ov.r) / 2.0;
            const double cy = (ov.t + ov.b) / 2.0;
            QString dt = QString("drawtext=text='%1':fontsize=%2:fontcolor=white:borderw=%3:bordercolor=black@0.65:"
                                 "x=%4*w-text_w/2:y=%5*h-text_h/2:")
                             .arg(escapeDrawtext(ov.text))
                             .arg(fontSize)
                             .arg(qMax(1, fontSize / 18))
                             .arg(cx, 0, 'f', 4)
                             .arg(cy, 0, 'f', 4);
#ifdef Q_OS_WIN
            dt += "fontfile='C\\:/Windows/Fonts/arial.ttf':";
#endif
            chain += lastOutput + dt + enable + cur + ";";
        }
        lastOutput = cur;
        ++step;
    }

    if (step == 0) return QString("%1null%2;").arg(inputLabel, outputLabel);
    chain += lastOutput + "copy" + outputLabel + ";";
    return chain;
}

// Video retime for a (possibly ramping) segment speed. Constant speed is an
// exact setpts scale. A genuine ramp treats speed as a function of source
// time T (0..D): speed(T) = s0 + k*T where k = (s1-s0)/D. Since output time
// advances at 1/speed(T) per unit of source time, the output timestamp for a
// frame at source time T is the closed-form integral out(T) = (1/k)*ln((s0+k*T)/s0).
static QString buildSpeedSetptsExpr(double speedStart, double speedEnd, double durationSec) {
    if (qFuzzyCompare(speedStart, speedEnd)) {
        return QString("setpts=PTS/%1").arg(speedStart, 0, 'f', 6);
    }
    const double D = qMax(0.001, durationSec);
    const double k = (speedEnd - speedStart) / D;
    return QString("setpts=(%1)*log((%2+(%3)*T)/%2)/TB")
        .arg(1.0 / k, 0, 'f', 6)
        .arg(speedStart, 0, 'f', 6)
        .arg(k, 0, 'f', 6);
}

// Matches buildSpeedSetptsExpr's math so the silence-fill branch (no source
// audio) can produce exactly as much silence as the retimed video needs.
static double retimedDurationSec(double durationSec, double speedStart, double speedEnd) {
    if (qFuzzyCompare(speedStart, speedEnd)) return durationSec / speedStart;
    return (durationSec / (speedEnd - speedStart)) * std::log(speedEnd / speedStart);
}

// A single atempo stage only accepts 0.5..2.0, so extreme speeds are chained.
static QString chainedAtempo(double speed) {
    QStringList stages;
    double remaining = qBound(0.02, speed, 50.0);
    while (remaining > 2.0) { stages << "atempo=2.0"; remaining /= 2.0; }
    while (remaining < 0.5) { stages << "atempo=0.5"; remaining /= 0.5; }
    stages << QString("atempo=%1").arg(remaining, 0, 'f', 6);
    return stages.join(",");
}

static QString cropScaleFilter(float cropRight, float cropLeft, float cropBottom, float cropTop, int vidW, int vidH) {
    return QString("crop=trunc(iw*(%1-%2)/2)*2:trunc(ih*(%3-%4)/2)*2:trunc(iw*%2/2)*2:trunc(ih*%4/2)*2,scale=%5:%6,setsar=1,format=yuv420p")
        .arg(cropRight).arg(cropLeft).arg(cropBottom).arg(cropTop).arg(vidW).arg(vidH);
}

namespace {
struct ExportGeometry {
    int vidW = 1920;
    int vidH = 1080;
};

ExportGeometry resolveExportGeometry(const QWidget *timelineWidget) {
    ExportGeometry geo;
    const QWidget *topWindow = timelineWidget->window();
    if (const QObject *videoContainer = topWindow->findChild<QObject*>("VideoContainer")) {
        auto *vwc = videoContainer->findChild<VideoWithCropWidget*>();
        if (vwc) {
            const QVariant vW = vwc->property("actualWidth");
            const QVariant vH = vwc->property("actualHeight");
            if (vW.isValid() && vW.toInt() > 0) geo.vidW = vW.toInt();
            if (vH.isValid() && vH.toInt() > 0) geo.vidH = vH.toInt();
        }
    }
    return geo;
}
}

// Builds the video (and optionally audio) filter graph for all segments across
// all timeline sources, ending in [outv] / [outa].
QString buildSegmentsGraph(const QList<TimelineWidget::Segment> &segments,
                           const QList<TimelineWidget::SourceClip> &sources,
                           const QList<TimelineWidget::OverlayClip> &overlays,
                           int vidW, int vidH,
                           bool withAudio,
                           bool primaryHasAudio,
                           int primaryAudioTrack,
                           double seekStart,
                           const QString &prefix) {
    QString filter;
    for (int i = 0; i < segments.size(); ++i) {
        const auto &seg = segments[i];
        const int srcIdx = qBound(0, seg.sourceIdx, static_cast<int>(sources.size()) - 1);
        const auto &src = sources[srcIdx];
        const double sLocal = qMax(0.0, (seg.startMs - src.offsetMs) / 1000.0 - (srcIdx == 0 ? seekStart : 0.0));
        const double d = (seg.endMs - seg.startMs) / 1000.0;
        const bool hasSpeedChange = !(qFuzzyCompare(seg.speedStart, 1.0f) && qFuzzyCompare(seg.speedEnd, 1.0f));
        const double dOut = hasSpeedChange ? retimedDurationSec(d, seg.speedStart, seg.speedEnd) : d;

        filter += QString("[%1:v]trim=start=%2:duration=%3,setpts=PTS-STARTPTS[%4_seg%5];")
                      .arg(srcIdx).arg(sLocal).arg(d).arg(prefix).arg(i);
        // Overlays are applied here, against the segment's original (pre-retime)
        // timestamps, so their enable='between(t,a,b)' windows stay correct —
        // the speed change happens afterward, warping the already-composited stream.
        filter += buildOverlayChain(QString("[%1_seg%2]").arg(prefix).arg(i),
                                    QString("[%1_v%2]").arg(prefix).arg(i),
                                    QString("%1%2").arg(prefix).arg(i),
                                    vidW, vidH,
                                    seg.startMs, seg.endMs,
                                    overlays);
        QString videoLabel = QString("[%1_v%2]").arg(prefix).arg(i);
        if (hasSpeedChange) {
            const QString spedLabel = QString("[%1_sp%2]").arg(prefix).arg(i);
            filter += videoLabel + buildSpeedSetptsExpr(seg.speedStart, seg.speedEnd, d) + spedLabel + ";";
            videoLabel = spedLabel;
        }
        filter += videoLabel + cropScaleFilter(seg.cropRight, seg.cropLeft, seg.cropBottom, seg.cropTop, vidW, vidH)
                + QString("[%1_vx%2];").arg(prefix).arg(i);

        if (withAudio) {
            const bool segHasAudio = (srcIdx == 0) ? primaryHasAudio : src.hasAudio;
            if (segHasAudio) {
                const int track = (srcIdx == 0) ? primaryAudioTrack : 0;
                if (!hasSpeedChange) {
                    filter += QString("[%1:a:%2]atrim=start=%3:duration=%4,asetpts=PTS-STARTPTS,volume=%5,"
                                      "aresample=async=1,aformat=sample_rates=48000:channel_layouts=stereo[%6_a%7];")
                                  .arg(srcIdx).arg(track).arg(sLocal).arg(d).arg(seg.gain).arg(prefix).arg(i);
                } else if (qFuzzyCompare(seg.speedStart, seg.speedEnd)) {
                    filter += QString("[%1:a:%2]atrim=start=%3:duration=%4,asetpts=PTS-STARTPTS,volume=%5,%6,"
                                      "aresample=async=1,aformat=sample_rates=48000:channel_layouts=stereo[%7_a%8];")
                                  .arg(srcIdx).arg(track).arg(sLocal).arg(d).arg(seg.gain)
                                  .arg(chainedAtempo(seg.speedStart)).arg(prefix).arg(i);
                } else {
                    // Ramp: ffmpeg has no variable-rate atempo, so approximate with
                    // a handful of constant-atempo sub-slices sampling the curve.
                    const int N = 6;
                    QString sub;
                    for (int k = 0; k < N; ++k) {
                        const double t0 = d * k / N, t1 = d * (k + 1) / N;
                        const double sMid = seg.speedStart + (seg.speedEnd - seg.speedStart) * ((k + 0.5) / N);
                        sub += QString("[%1:a:%2]atrim=start=%3:duration=%4,asetpts=PTS-STARTPTS,volume=%5,%6,"
                                      "aresample=async=1,aformat=sample_rates=48000:channel_layouts=stereo[%7_a%8_%9];")
                                   .arg(srcIdx).arg(track).arg(sLocal + t0).arg(t1 - t0).arg(seg.gain)
                                   .arg(chainedAtempo(sMid)).arg(prefix).arg(i).arg(k);
                    }
                    for (int k = 0; k < N; ++k) sub += QString("[%1_a%2_%3]").arg(prefix).arg(i).arg(k);
                    sub += QString("concat=n=%1:v=0:a=1[%2_a%3];").arg(N).arg(prefix).arg(i);
                    filter += sub;
                }
            } else {
                filter += QString("aevalsrc=0:channel_layout=stereo:sample_rate=48000:d=%1[%2_a%3];")
                              .arg(dOut).arg(prefix).arg(i);
            }
        }
    }

    for (int i = 0; i < segments.size(); ++i) {
        filter += QString("[%1_vx%2]").arg(prefix).arg(i);
        if (withAudio) filter += QString("[%1_a%2]").arg(prefix).arg(i);
    }
    filter += QString("concat=n=%1:v=1:a=%2[outv]").arg(segments.size()).arg(withAudio ? 1 : 0);
    if (withAudio) filter += "[outa]";
    return filter;
}

void TimelineWidget::copyTrimmedVideo() {
    if (segments.empty() || isExporting) return;
    if (!hasVideoStream) {
        showNotification("VIDEO EXPORT NEEDS A VIDEO TRACK");
        return;
    }
    if (!hasAudioStream) {
        copyTrimmedVideoMuted();
        return;
    }

    const ExportGeometry geo = resolveExportGeometry(this);
    const int vidW = geo.vidW;
    const int vidH = geo.vidH;

    const QString outputDir = getExportDir();
    QString finalPath = QDir::toNativeSeparators(outputDir + "/" + generateClippedName("mp4"));

    qint64 totalMs = 0;
    for (const auto& seg : segments) totalMs += (seg.endMs - seg.startMs);
    const double durationSec = qMax(0.1, totalMs / 1000.0);
    const auto exportSettings = this->exportSettings;

    isExporting = true;
    const bool multiSource = sources.size() > 1;
    const double seekStart = multiSource ? 0.0 : qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    const QString filter = buildSegmentsGraph(segments, sources, overlays, vidW, vidH,
                                              /*withAudio=*/true, hasAudioStream, currentAudioTrack,
                                              seekStart, "s");

    double originalBitrateKbps = (originalFileSize * 8.0) / (qMax<qint64>(1, durationMs) / 1000.0) / 1000.0;
    double estimatedSizeMB = (originalBitrateKbps * durationSec) / 8192.0;
    bool shouldCompress = (estimatedSizeMB >= exportSettings.videoCompressionThresholdMB);
    const bool nv = hasNvidiaEncoder();
    const double targetMB = exportSettings.targetCompressedSizeMB;

    // SAFETY MARGIN: one-pass bitrate targeting always has some variance (scene
    // complexity, muxing/container overhead, encoder rate-control accuracy), so aiming
    // exactly at the configured target reliably overshoots it. Aim under it instead,
    // and verify+retry below to guarantee the final file never exceeds the target.
    auto buildArgs = [=](double videoBitrateKbps) {
        QStringList a;
        a << "-y";
        if (!multiSource && seekStart > 0.0) a << "-ss" << QString::number(seekStart);
        for (const auto &src : sources) a << "-i" << QDir::toNativeSeparators(src.path);
        a << "-filter_complex" << filter;
        a << "-map" << "[outv]" << "-map" << "[outa]";

        if (shouldCompress) {
            if (nv) {
                a << "-c:v" << "h264_nvenc" << "-preset" << "p4" << "-tune" << "hq" << "-rc" << "vbr";
            } else {
                a << "-c:v" << "libx264" << "-preset" << "veryfast";
            }
            a << "-b:v" << QString("%1k").arg(qRound(videoBitrateKbps))
              << "-maxrate" << QString("%1k").arg(qRound(videoBitrateKbps * 1.15))
              << "-bufsize" << QString("%1k").arg(qRound(videoBitrateKbps * 1.3));
            a << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(exportSettings.compressedAudioBitrateKbps);
        } else {
            int targetK = static_cast<int>(originalBitrateKbps);
            if (nv) {
                a << "-c:v" << "h264_nvenc" << "-preset" << "p7" << "-rc" << "vbr"
                  << "-b:v" << QString("%1k").arg(targetK) << "-maxrate" << QString("%1k").arg(targetK * 2);
            } else {
                a << "-c:v" << "libx264" << "-preset" << "slow" << "-crf" << "18"
                  << "-maxrate" << QString("%1k").arg(targetK) << "-bufsize" << QString("%1k").arg(targetK * 2);
            }
            a << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(exportSettings.audioBitrateKbps);
        }

        a << "-pix_fmt" << "yuv420p" << "-movflags" << "+faststart" << "-progress" << "pipe:1"
          << QDir::toNativeSeparators(finalPath);
        return a;
    };

    double initialVideoBitrateKbps = 0.0;
    if (shouldCompress) {
        const double targetSizeBytes = targetMB * 1024 * 1024 * 0.93;
        const double audioBitrateBps = exportSettings.compressedAudioBitrateKbps * 1000.0;
        initialVideoBitrateKbps = qBound(150.0, (targetSizeBytes * 8 / durationSec - audioBitrateBps) / 1000.0, 12000.0);
    }

    const int maxAttempts = 3;
    auto runAttempt = QSharedPointer<std::function<void(double, int)>>::create();
    *runAttempt = [this, runAttempt, buildArgs, finalPath, shouldCompress, targetMB, maxAttempts, totalMs](double videoBitrateKbps, int attempt) {
        auto *ffmpeg = new QProcess(this);
        QSharedPointer<QString> ffmpegLog(new QString());

        ffmpeg->setProcessChannelMode(QProcess::MergedChannels);
        connect(ffmpeg, &QProcess::readyRead, [ffmpeg, ffmpegLog]() {
            ffmpegLog->append(ffmpeg->peek(ffmpeg->bytesAvailable()));
        });

        showProgressNotification(ffmpeg, totalMs);

        connect(ffmpeg, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, finalPath, ffmpeg, ffmpegLog, shouldCompress, targetMB, maxAttempts, videoBitrateKbps, attempt, runAttempt](int exitCode) {
            if (exitCode != 0) {
                isExporting = false;
                qDebug() << "FFMPEG FAILURE LOG:\n" << *ffmpegLog;
                emit exportFinished(false, "EXPORT FAILED");
                QMessageBox::critical(this->window(), "Export Failed",
                    "FFmpeg Details:\n\n" + (*ffmpegLog).right(600));
                update();
                ffmpeg->deleteLater();
                return;
            }

            const double actualMB = QFileInfo(finalPath).size() / (1024.0 * 1024.0);
            if (shouldCompress && actualMB > targetMB && attempt < maxAttempts && videoBitrateKbps > 160.0) {
                // Still over budget: scale the bitrate down by the actual overshoot ratio
                // (plus a little extra headroom) and re-encode until it fits.
                const double ratio = qBound(0.4, (targetMB / qMax(0.1, actualMB)) * 0.92, 0.97);
                const double nextBitrate = qMax(150.0, videoBitrateKbps * ratio);
                ffmpeg->deleteLater();
                (*runAttempt)(nextBitrate, attempt + 1);
                return;
            }

            isExporting = false;
            auto m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            emit exportFinished(true, QString("VIDEO EXPORTED · %1 MB · COPIED TO CLIPBOARD").arg(actualMB, 0, 'f', 1));
            update();
            ffmpeg->deleteLater();
        });

        ffmpeg->start(getFFmpegPath(), buildArgs(videoBitrateKbps));
    };

    (*runAttempt)(initialVideoBitrateKbps, 0);
}

void TimelineWidget::copyTrimmedVideoMuted() {
    if (segments.empty() || isExporting) return;
    if (!hasVideoStream) {
        showNotification("NO VIDEO TRACK AVAILABLE");
        return;
    }

    const ExportGeometry geo = resolveExportGeometry(this);
    const int vidW = geo.vidW;
    const int vidH = geo.vidH;

    QString outputDir = getExportDir();
    QString finalPath = outputDir + "/MUTED_" + generateClippedName("mp4");

    qint64 totalMs = 0;
    for (const auto& seg : segments) totalMs += (seg.endMs - seg.startMs);
    const double durationSec = qMax(0.1, totalMs / 1000.0);
    const auto exportSettings = this->exportSettings;

    const double timeRatio = static_cast<double>(totalMs) / static_cast<double>(qMax<qint64>(1, durationMs));
    double weightedSpatialRatio = 0.0;
    for (const auto &seg : segments) {
        const double segDuration = qMax<qint64>(1, seg.endMs - seg.startMs);
        weightedSpatialRatio += segDuration * ((seg.cropRight - seg.cropLeft) * (seg.cropBottom - seg.cropTop));
    }
    const double spatialRatio = totalMs > 0 ? weightedSpatialRatio / totalMs : 1.0;
    const double estMb = (originalFileSize * timeRatio * spatialRatio) / (1024.0 * 1024.0);

    isExporting = true;
    const bool multiSource = sources.size() > 1;
    const double seekStart = multiSource ? 0.0 : qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    const QString filter = buildSegmentsGraph(segments, sources, overlays, vidW, vidH,
                                              /*withAudio=*/false, hasAudioStream, currentAudioTrack,
                                              seekStart, "m");

    const bool nv = hasNvidiaEncoder();
    const bool shouldCompress = estMb > exportSettings.videoCompressionThresholdMB;
    const double targetMB = exportSettings.targetCompressedSizeMB;

    auto buildArgs = [=](double videoBitrateKbps) {
        QStringList a;
        a << "-y";
        if (!multiSource && seekStart > 0.0) a << "-ss" << QString::number(seekStart);
        for (const auto &src : sources) a << "-i" << QDir::toNativeSeparators(src.path);
        a << "-filter_complex" << filter
          << "-map" << "[outv]"
          << "-an";

        if (nv) {
            a << "-c:v" << "h264_nvenc" << "-preset" << "p1" << "-tune" << "ull" << "-zerolatency" << "1";
        } else {
            a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-tune" << "zerolatency";
        }

        a << "-pix_fmt" << "yuv420p";

        if (shouldCompress) {
            a << "-r" << "25" << "-b:v" << QString("%1k").arg(qRound(videoBitrateKbps))
              << "-maxrate" << QString("%1k").arg(qRound(videoBitrateKbps * 1.15))
              << "-bufsize" << QString("%1k").arg(qRound(videoBitrateKbps * 1.3));
        } else {
            if (nv) a << "-rc" << "constqp" << "-qp" << "23";
            else a << "-crf" << "23";
        }

        a << "-progress" << "pipe:1" << QDir::toNativeSeparators(finalPath);
        return a;
    };

    double initialVideoBitrateKbps = 0.0;
    if (shouldCompress) {
        const double targetSizeBytes = targetMB * 1024 * 1024 * 0.93;
        initialVideoBitrateKbps = qBound(150.0, (targetSizeBytes * 8 / durationSec), 15000.0);
    }

    const int maxAttempts = 3;
    auto runAttempt = QSharedPointer<std::function<void(double, int)>>::create();
    *runAttempt = [this, runAttempt, buildArgs, finalPath, shouldCompress, targetMB, maxAttempts, totalMs](double videoBitrateKbps, int attempt) {
        auto *ffmpeg = new QProcess(this);
        showProgressNotification(ffmpeg, totalMs);

        connect(ffmpeg, &QProcess::finished, this,
                [this, finalPath, ffmpeg, shouldCompress, targetMB, maxAttempts, videoBitrateKbps, attempt, runAttempt](int exitCode) {
            if (exitCode != 0) {
                isExporting = false;
                emit exportFinished(false, "MUTED EXPORT FAILED");
                update();
                ffmpeg->deleteLater();
                return;
            }

            const double actualMB = QFileInfo(finalPath).size() / (1024.0 * 1024.0);
            if (shouldCompress && actualMB > targetMB && attempt < maxAttempts && videoBitrateKbps > 160.0) {
                const double ratio = qBound(0.4, (targetMB / qMax(0.1, actualMB)) * 0.92, 0.97);
                const double nextBitrate = qMax(150.0, videoBitrateKbps * ratio);
                ffmpeg->deleteLater();
                (*runAttempt)(nextBitrate, attempt + 1);
                return;
            }

            isExporting = false;
            auto *m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            emit exportFinished(true, QString("MUTED VIDEO EXPORTED · %1 MB · COPIED").arg(actualMB, 0, 'f', 1));
            update();
            ffmpeg->deleteLater();
        });

        ffmpeg->start(getFFmpegPath(), buildArgs(videoBitrateKbps));
    };

    (*runAttempt)(initialVideoBitrateKbps, 0);
}

void TimelineWidget::copyTrimmedGif() {
    if (segments.empty() || isExporting) return;
    if (!hasVideoStream) {
        showNotification("GIF EXPORT NEEDS VIDEO");
        return;
    }

    const ExportGeometry geo = resolveExportGeometry(this);
    const int vidW = geo.vidW;
    const int vidH = geo.vidH;

    const QString outputDir = getExportDir();
    QString finalPath = outputDir + "/" + generateClippedName("gif");
    const auto exportSettings = this->exportSettings;

    qint64 totalMs = 0;
    for (const auto &seg : segments) totalMs += (seg.endMs - seg.startMs);

    // The whole composition (every segment, every source, overlays with their
    // time ranges) goes into the GIF — same graph as the video exports.
    QString filter = buildSegmentsGraph(segments, sources, overlays, vidW, vidH,
                                        /*withAudio=*/false, hasAudioStream, currentAudioTrack,
                                        /*seekStart=*/0.0, "g");
    filter += QString(";[outv]fps=%1,scale=%2:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse[gif]")
                  .arg(exportSettings.gifFps).arg(exportSettings.gifWidth);

    QStringList args;
    args << "-y";
    for (const auto &src : sources) args << "-i" << QDir::toNativeSeparators(src.path);
    args << "-filter_complex" << filter << "-map" << "[gif]" << "-threads" << "0"
         << "-progress" << "pipe:1"
         << QDir::toNativeSeparators(finalPath);

    isExporting = true;
    QProcess *ffmpeg = new QProcess(this);
    showProgressNotification(ffmpeg, totalMs);
    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg](int exitCode) {
        isExporting = false;
        if (exitCode == 0) {
            QMimeData *m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            emit exportFinished(true, "GIF EXPORTED · COPIED TO CLIPBOARD");
        } else {
            emit exportFinished(false, "GIF EXPORT FAILED");
        }
        ffmpeg->deleteLater();
    });
    ffmpeg->start(getFFmpegPath(), args);
}

void TimelineWidget::copyTrimmedAudio() {
    if (segments.empty() || isExporting) return;
    if (!hasAudioStream) {
        showNotification("NO AUDIO TRACK AVAILABLE");
        return;
    }

    const QString outputDir = getExportDir();
    QString finalPath = outputDir + "/" + generateClippedName("mp3");
    const auto exportSettings = this->exportSettings;

    qint64 totalMs = 0;
    for (const auto& seg : segments) totalMs += (seg.endMs - seg.startMs);

    isExporting = true;
    const bool multiSource = sources.size() > 1;
    const double seekStart = multiSource ? 0.0 : qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    QString filter;
    for (int i = 0; i < segments.size(); ++i) {
        const auto &seg = segments[i];
        const int srcIdx = qBound(0, seg.sourceIdx, static_cast<int>(sources.size()) - 1);
        const auto &src = sources[srcIdx];
        const double sLocal = qMax(0.0, (seg.startMs - src.offsetMs) / 1000.0 - (srcIdx == 0 ? seekStart : 0.0));
        const double d = (seg.endMs - seg.startMs) / 1000.0;
        const bool segHasAudio = (srcIdx == 0) ? hasAudioStream : src.hasAudio;
        if (segHasAudio) {
            const int track = (srcIdx == 0) ? currentAudioTrack : 0;
            filter += QString("[%1:a:%2]atrim=start=%3:duration=%4,asetpts=PTS-STARTPTS,"
                              "aresample=async=1,aformat=sample_rates=48000:channel_layouts=stereo[a%5];")
                          .arg(srcIdx).arg(track).arg(sLocal).arg(d).arg(i);
        } else {
            filter += QString("aevalsrc=0:channel_layout=stereo:sample_rate=48000:d=%1[a%2];").arg(d).arg(i);
        }
    }

    for (int i = 0; i < segments.size(); ++i) filter += QString("[a%1]").arg(i);
    filter += QString("concat=n=%1:v=0:a=1[outa]").arg(segments.size());

    QStringList args;
    args << "-y";
    if (!multiSource && seekStart > 0.0) args << "-ss" << QString::number(seekStart);
    for (const auto &src : sources) args << "-i" << QDir::toNativeSeparators(src.path);
    args << "-filter_complex" << filter
         << "-map" << "[outa]"
         << "-c:a" << "libmp3lame" << "-b:a" << QString("%1k").arg(exportSettings.audioBitrateKbps) << "-threads" << "0"
         << "-progress" << "pipe:1"
         << QDir::toNativeSeparators(finalPath);

    auto *ffmpeg = new QProcess(this);
    showProgressNotification(ffmpeg, totalMs);

    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg](int exitCode) {
        isExporting = false;
        if (exitCode == 0) {
            auto *m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            emit exportFinished(true, "AUDIO EXPORTED · COPIED TO CLIPBOARD");
        } else {
            emit exportFinished(false, "AUDIO EXPORT FAILED");
        }
        ffmpeg->deleteLater();
    });
    ffmpeg->start(getFFmpegPath(), args);
}

QString TimelineWidget::generateClippedName(const QString &extension) const {
    if (!customExportName.isEmpty()) {
        return QString("%1.%2").arg(customExportName, extension);
    }

    const auto settings = exportSettings;
    const QFileInfo fileInfo(currentFileUrl.toLocalFile());
    QString origName = fileInfo.baseName();
    QString timeStr = QTime::currentTime().toString("hh-mm-ss-AP");
    QString prefix = settings.fileNamePrefix.trimmed().isEmpty() ? QString("clip") : settings.fileNamePrefix.trimmed();
    QStringList parts;
    parts << prefix;
    if (settings.includeSourceNameInExport && !origName.isEmpty()) parts << origName;
    parts << QString("clipped-%1").arg(timeStr);
    return QString("(%1).%2").arg(parts.join("-"), extension);
}

double TimelineWidget::getTotalSegmentsDuration() {
    qint64 totalMs = 0;
    for (const auto& seg : segments) {
        totalMs += (seg.endMs - seg.startMs);
    }
    return qMax(1.0, totalMs / 1000.0);
}

qint64 TimelineWidget::getStartLimit() const { return segments.isEmpty() ? 0 : segments.first().startMs; }
qint64 TimelineWidget::getEndLimit() const { return segments.isEmpty() ? durationMs : segments.last().endMs; }
