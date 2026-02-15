#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTime>

#include "../Includes/timelinewidget.h"
#include "../Includes/mediaSource.h"

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
    QString path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Edited";
    QDir().mkpath(path);
    return path;
}

static QString buildFilterChain(int vidW, int vidH, const QList<VideoWithCropWidget::FilterObject>& filterList) {
    if (filterList.isEmpty()) return "[0:v]null[filtered];";

    // We start with the input stream
    QString chain = "[0:v]";

    for (int i = 0; i < filterList.size(); ++i) {
        const auto& obj = filterList[i];

        int absX = qRound(vidW * obj.l) & ~1;
        int absY = qRound(vidH * obj.t) & ~1;
        int absW = qRound(vidW * (obj.r - obj.l)) & ~1;
        int absH = qRound(vidH * (obj.b - obj.t)) & ~1;

        if (absW <= 0 || absH <= 0) continue;


        if (obj.mode == 0) { // Blur
            chain += QString("split[base][mask];[mask]crop=%1:%2:%3:%4,boxblur=20[blur];[base][blur]overlay=%3:%4")
                     .arg(absW).arg(absH).arg(absX).arg(absY);
        } else if (obj.mode == 1) { // Pixelate
            chain += QString("split[base][mask];[mask]crop=%1:%2:%3:%4,scale=iw/30:-1,scale=%1:%2:flags=neighbor[px];[base][px]overlay=%3:%4")
                     .arg(absW).arg(absH).arg(absX).arg(absY);
        } else { // Blackout
            chain += QString("drawbox=x=%1:y=%2:w=%3:h=%4:color=black:t=fill")
                     .arg(absX).arg(absY).arg(absW).arg(absH);
        }

        if (i < filterList.size() - 1) {
            QString nextLabel = QString("[v_tmp%1]").arg(i);
            chain += nextLabel + "; " + nextLabel;
        }
    }

    chain += "[filtered];";
    return chain;
}

void TimelineWidget::copyTrimmedVideo() {
    if (segments.empty() || isExporting) return;

    int vidW = 1920;
    int vidH = 1080;
    QList<VideoWithCropWidget::FilterObject> filters;
    const QWidget* topWindow = this->window();

    if (const QObject* videoContainer = topWindow->findChild<QObject*>("VideoContainer")) {
        auto* vwc = videoContainer->findChild<VideoWithCropWidget*>();
        if (vwc) {
            filters = vwc->filterObjects;
            const QVariant vW = vwc->property("actualWidth");
            const QVariant vH = vwc->property("actualHeight");
            if (vW.isValid() && vH.isValid()) {
                vidW = vW.toInt();
                vidH = vH.toInt();
            }
        }
    }

    const QString outputDir = getExportDir();
    QString finalPath = outputDir + "/" + generateClippedName("mp4");

    qint64 totalMs = 0;
    for (const auto& seg : segments) totalMs += (seg.endMs - seg.startMs);
    const double durationSec = qMax(0.1, totalMs / 1000.0);
    isExporting = true;
    const double seekStart = qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    QString filter = buildFilterChain(vidW, vidH, filters);

    for (int i = 0; i < segments.size(); ++i) {
        double s = (segments[i].startMs / 1000.0) - seekStart;
        double d = (segments[i].endMs - segments[i].startMs) / 1000.0;
        filter += QString("[filtered]trim=start=%1:duration=%2,setpts=PTS-STARTPTS[v%3];")
                  .arg(qMax(0.0, s)).arg(d).arg(i);
        filter += QString("[0:a:%1]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS[a%4];")
                  .arg(currentAudioTrack).arg(qMax(0.0, s)).arg(d).arg(i);
    }

    for (int i = 0; i < segments.size(); ++i) filter += QString("[v%1][a%1]").arg(i);

    QString cropStr = QString("crop=trunc(iw*(%1-%2)/2)*2:trunc(ih*(%3-%4)/2)*2:trunc(iw*%2/2)*2:trunc(ih*%4/2)*2,setsar=1")
                      .arg(cropRight).arg(cropLeft).arg(cropBottom).arg(cropTop);

    filter += QString("concat=n=%1:v=1:a=1[cv][outa];[cv]%2[outv]").arg(segments.size()).arg(cropStr);

    double targetSizeBytes = 6.7 * 1024 * 1024;
    double audioBitrateBps = 32000;
    double availableVideoBitsPerSec = (targetSizeBytes * 8 / durationSec) - audioBitrateBps;
    int videoBitrateKbps = qBound(200, static_cast<int>(availableVideoBitsPerSec / 1000), 12000);

    QStringList args;
    args << "-y" << "-ss" << QString::number(seekStart) << "-i" << QDir::toNativeSeparators(currentFileUrl.toLocalFile());
    args << "-filter_complex" << filter;
    args << "-map" << "[outv]" << "-map" << "[outa]";

    if (hasNvidiaEncoder()) {
        args << "-c:v" << "h264_nvenc" << "-preset" << "p4" << "-tune" << "hq" << "-rc" << "vbr";
    } else {
        args << "-c:v" << "libx264" << "-preset" << "faster";
    }

    args << "-pix_fmt" << "yuv420p"
         << "-b:v" << QString("%1k").arg(videoBitrateKbps)
         << "-maxrate" << QString("%1k").arg(static_cast<int>(videoBitrateKbps * 1.1))
         << "-bufsize" << QString("%1k").arg(videoBitrateKbps * 2);

    args << "-c:a" << "aac" << "-b:a" << "32k" << "-ac" << "1";
    args << "-progress" << "pipe:1" << QDir::toNativeSeparators(finalPath);

    auto *ffmpeg = new QProcess(this);
    showProgressNotification(ffmpeg, totalMs);

    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg](const int exitCode) {
        isExporting = false;
        if (exitCode == 0) {
            auto m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            showNotification("Copied trimmed video");
        } else {
            showNotification("FAILED");
        }
        update();
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFmpegPath(), args);
}

void TimelineWidget::copyTrimmedVideoMuted() {
    if (segments.empty() || isExporting) return;

    int vidW = 1920, vidH = 1080;
    QList<VideoWithCropWidget::FilterObject> filters;
    const QWidget* topWindow = this->window();
    if (const QObject* videoContainer = topWindow->findChild<QObject*>("VideoContainer")) {
        auto* vwc = videoContainer->findChild<VideoWithCropWidget*>();
        if (vwc) {
            filters = vwc->filterObjects;
            vidW = vwc->property("actualWidth").toInt();
            vidH = vwc->property("actualHeight").toInt();
        }
    }

    QString outputDir = getExportDir();
    QString finalPath = outputDir + "/MUTED_" + generateClippedName("mp4");

    qint64 totalMs = 0;
    for (const auto& seg : segments) totalMs += (seg.endMs - seg.startMs);
    const double durationSec = qMax(0.1, totalMs / 1000.0);

    const double timeRatio = static_cast<double>(totalMs) / static_cast<double>(durationMs);
    const double spatialRatio = (cropRight - cropLeft) * (cropBottom - cropTop);
    const double estMb = (originalFileSize * timeRatio * spatialRatio) / (1024.0 * 1024.0);

    isExporting = true;
    const double seekStart = qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    QString filter = buildFilterChain(vidW, vidH, filters);
    for (int i = 0; i < segments.size(); ++i) {
        double s = (segments[i].startMs / 1000.0) - seekStart;
        double d = (segments[i].endMs - segments[i].startMs) / 1000.0;
        filter += QString("[filtered]trim=start=%1:duration=%2,setpts=PTS-STARTPTS[v%3];")
                  .arg(qMax(0.0, s)).arg(d).arg(i);
    }
    for (int i = 0; i < segments.size(); ++i) filter += QString("[v%1]").arg(i);

    const QString cropStr = QString("crop=trunc(iw*(%1-%2)/2)*2:trunc(ih*(%3-%4)/2)*2:trunc(iw*%2/2)*2:trunc(ih*%4/2)*2,setsar=1")
                      .arg(cropRight).arg(cropLeft).arg(cropBottom).arg(cropTop);

    filter += QString("concat=n=%1:v=1:a=0[cv];[cv]%2[outv]").arg(segments.size()).arg(cropStr);

    QStringList args;
    args << "-y" << "-ss" << QString::number(seekStart) << "-i" << QDir::toNativeSeparators(currentFileUrl.toLocalFile())
         << "-filter_complex" << filter
         << "-map" << "[outv]"
         << "-an";

    bool nv = hasNvidiaEncoder();
    if (nv) {
        args << "-c:v" << "h264_nvenc" << "-preset" << "p1" << "-tune" << "ull" << "-zerolatency" << "1";
    } else {
        args << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-tune" << "zerolatency";
    }

    args << "-pix_fmt" << "yuv420p";

    if (estMb > 8.0) {
        int videoBitrateKbps = qBound(200, static_cast<int>((7.5 * 8192) / durationSec), 15000);
        args << "-r" << "25" << "-b:v" << QString("%1k").arg(videoBitrateKbps);
    } else {
        if (nv) args << "-rc" << "constqp" << "-qp" << "23";
        else args << "-crf" << "23";
    }

    args << "-progress" << "pipe:1" << QDir::toNativeSeparators(finalPath);

    auto ffmpeg = new QProcess(this);
    showProgressNotification(ffmpeg, totalMs);

    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg](int exitCode) {
        isExporting = false;
        if (exitCode == 0) {
            auto *m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            showNotification("MUTED CRUNCH COMPLETE ✅");
        } else {
            showNotification("MUTED EXPORT FAILED ❌");
        }
        update();
        ffmpeg->deleteLater();
    });
    ffmpeg->start(getFFmpegPath(), args);
}

void TimelineWidget::copyTrimmedGif() {
    if (segments.empty()) return;

    int vidW = 1920, vidH = 1080;
    QList<VideoWithCropWidget::FilterObject> filters;
    const QWidget* topWindow = this->window();
    if (const QObject* videoContainer = topWindow->findChild<QObject*>("VideoContainer")) {
        auto* vwc = videoContainer->findChild<VideoWithCropWidget*>();
        if (vwc) {
            filters = vwc->filterObjects;
            vidW = vwc->property("actualWidth").toInt();
            vidH = vwc->property("actualHeight").toInt();
        }
    }

    const QString outputDir = getExportDir();
    QString finalPath = outputDir + "/" + generateClippedName("gif");

    const double duration = (segments[0].endMs - segments[0].startMs) / 1000.0;
    const double seekStart = qMax(0.0, (segments[0].startMs / 1000.0));

    QString filter = buildFilterChain(vidW, vidH, filters);
    const QString cropFilter = QString("crop=trunc(iw*(%1-%2)/2)*2:trunc(ih*(%3-%4)/2)*2:trunc(iw*%2/2)*2:trunc(ih*%4/2)*2")
                         .arg(cropRight).arg(cropLeft).arg(cropBottom).arg(cropTop);

    filter += QString("[filtered]%1,fps=12,scale=480:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse")
                     .arg(cropFilter);

    QStringList args;
    args << "-y" << "-ss" << QString::number(seekStart) << "-t" << QString::number(duration)
         << "-i" << QDir::toNativeSeparators(currentFileUrl.toLocalFile()) << "-filter_complex" << filter << "-threads" << "0"
         << QDir::toNativeSeparators(finalPath);

    QProcess *ffmpeg = new QProcess(this);
    showProgressNotification(ffmpeg, (segments[0].endMs - segments[0].startMs));
    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg]() {
        QMimeData *m = new QMimeData();
        m->setUrls({QUrl::fromLocalFile(finalPath)});
        QApplication::clipboard()->setMimeData(m);
        showNotification("GIF COPIED ✅");
        ffmpeg->deleteLater();
    });
    ffmpeg->start(getFFmpegPath(), args);
}

void TimelineWidget::copyTrimmedAudio() {
    if (segments.empty() || isExporting) return;

    const QString outputDir = getExportDir();
    QString finalPath = outputDir + "/" + generateClippedName("mp3");

    isExporting = true;
    double seekStart = qMax(0.0, (segments[0].startMs / 1000.0) - 0.5);

    QString filter;
    for (int i = 0; i < segments.size(); ++i) {
        double s = (segments[i].startMs / 1000.0) - seekStart;
        double d = (segments[i].endMs - segments[i].startMs) / 1000.0;
        filter += QString("[0:a:%1]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS[a%4];")
                  .arg(currentAudioTrack).arg(qMax(0.0, s)).arg(d).arg(i);
    }

    for (int i = 0; i < segments.size(); ++i) filter += QString("[a%1]").arg(i);
    filter += QString("concat=n=%1:v=0:a=1[outa]").arg(segments.size());

    QStringList args;
    args << "-y" << "-ss" << QString::number(seekStart) << "-i" << QDir::toNativeSeparators(currentFileUrl.toLocalFile())
         << "-filter_complex" << filter
         << "-map" << "[outa]"
         << "-c:a" << "libmp3lame" << "-b:a" << "192k" << "-threads" << "0"
         << QDir::toNativeSeparators(finalPath);

    auto *ffmpeg = new QProcess(this);
    showNotification("EXPORTING AUDIO... 🎵");

    connect(ffmpeg, &QProcess::finished, this, [this, finalPath, ffmpeg](int exitCode) {
        isExporting = false;
        if (exitCode == 0) {
            auto *m = new QMimeData();
            m->setUrls({QUrl::fromLocalFile(finalPath)});
            QApplication::clipboard()->setMimeData(m);
            showNotification("AUDIO SAVED & COPIED ✅");
        } else {
            showNotification("AUDIO EXPORT FAILED ❌");
        }
        ffmpeg->deleteLater();
    });
    ffmpeg->start(getFFmpegPath(), args);
}

QString TimelineWidget::generateClippedName(const QString &extension) const {
    if (!customExportName.isEmpty()) {
        return QString("%1.%2").arg(customExportName, extension);
    }

    const QFileInfo fileInfo(currentFileUrl.toLocalFile());
    QString origName = fileInfo.baseName();
    QString timeStr = QTime::currentTime().toString("hh-mm-ss-AP");

    return QString("(clip-%1_clipped-%2).%3").arg(origName, timeStr, extension);
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