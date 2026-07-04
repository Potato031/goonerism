#include <QAudioOutput>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QSharedPointer>

#include "../Includes/timelinewidget.h"

// Helper function to resolve the bundled binary path
static QString getFFToolPath(const QString &tool) {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/" + tool + ".exe";
#else
    return tool;
#endif
}

void TimelineWidget::loadAudioFast(const QString &inputPath) {
    if (!hasAudioStream) {
        audioSamples.clear();
        maxAmplitude = 0.01f;
        update();
        emit mediaProbingFinished();
        return;
    }

    QString tempAudioPath = QDir::tempPath() + QString("/potato_wave_%1.raw").arg(qAbs(qHash(inputPath)));
    auto *ffmpeg = new QProcess(this);
    QStringList args;
    args << "-y" << "-i" << inputPath
         << "-map" << QString("0:a:%1").arg(currentAudioTrack)
         << "-f" << "s16le" << "-ac" << "1" << "-ar" << "8000" << tempAudioPath;

    connect(ffmpeg, &QProcess::finished, this, [this, tempAudioPath, ffmpeg]() {
        QFile file(tempAudioPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QFile::remove(tempAudioPath);
            const auto *samples = reinterpret_cast<const int16_t*>(data.constData());
            int count = data.size() / sizeof(int16_t);
            audioSamples.clear();
            float localMax = 0.01f;
            for (int i = 0; i < count; i += 80) {
                double sum = 0;
                int actualWindow = qMin(80, count - i);
                for (int j = 0; j < actualWindow; ++j) {
                    double val = samples[i + j] / 32768.0;
                    sum += val * val;
                }
                float rms = std::sqrt(sum / actualWindow);
                rms = std::pow(rms, 0.6f);
                audioSamples.push_back(rms);
                if (rms > localMax) localMax = rms;
            }
            maxAmplitude = localMax;
        }
        update();
        emit mediaProbingFinished();
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFToolPath("ffmpeg"), args);
}

// Waveform for a source appended to the end of the timeline. Samples are
// produced at the same fixed density as loadAudioFast (8000 Hz / 80-sample
// windows = 100 samples per second), so appending keeps the index→time
// mapping of the combined array valid.
void TimelineWidget::appendAudioWaveform(const QString &inputPath) {
    QString tempAudioPath = QDir::tempPath() + QString("/potato_wave_%1.raw").arg(qAbs(qHash(inputPath)));
    auto *ffmpeg = new QProcess(this);
    QStringList args;
    args << "-y" << "-i" << inputPath
         << "-map" << "0:a:0"
         << "-f" << "s16le" << "-ac" << "1" << "-ar" << "8000" << tempAudioPath;

    connect(ffmpeg, &QProcess::finished, this, [this, tempAudioPath, ffmpeg]() {
        QFile file(tempAudioPath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QFile::remove(tempAudioPath);
            const auto *samples = reinterpret_cast<const int16_t*>(data.constData());
            int count = data.size() / sizeof(int16_t);
            float localMax = maxAmplitude;
            for (int i = 0; i < count; i += 80) {
                double sum = 0;
                int actualWindow = qMin(80, count - i);
                for (int j = 0; j < actualWindow; ++j) {
                    double val = samples[i + j] / 32768.0;
                    sum += val * val;
                }
                float rms = std::sqrt(sum / actualWindow);
                rms = std::pow(rms, 0.6f);
                audioSamples.push_back(rms);
                if (rms > localMax) localMax = rms;
            }
            maxAmplitude = localMax;
        }
        update();
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFToolPath("ffmpeg"), args);
}

void TimelineWidget::autoCutSilence() {
    if (durationMs <= 0 || isExporting || segments.empty() || !hasAudioStream) {
        showNotification("NO AUDIO TRACK TO ANALYZE");
        return;
    }
    saveState("Auto-cut silence");
    showNotification("ANALYZING TRIMMED SECTIONS");

    // Copy the segments so timeline edits during analysis can't crash us.
    const QList<Segment> workArea = segments;
    const auto settings = autoCutSettings;

    // Every source that has audio gets its own silencedetect pass (silence
    // timestamps are source-local, so one pass per file). Segments from
    // sources without audio pass through untouched.
    QSet<int> audioSources;
    for (const auto &seg : workArea) {
        const int si = qBound(0, seg.sourceIdx, static_cast<int>(sources.size()) - 1);
        const bool srcHasAudio = (si == 0) ? hasAudioStream : sources[si].hasAudio;
        if (srcHasAudio) audioSources.insert(si);
    }
    if (audioSources.isEmpty()) {
        showNotification("NO AUDIO TO ANALYZE");
        return;
    }

    struct DetectState {
        QMap<int, QPair<QList<double>, QList<double>>> silence; // sourceIdx -> (starts, ends)
        int pending = 0;
    };
    auto state = QSharedPointer<DetectState>::create();
    state->pending = audioSources.size();

    auto finalize = [this, workArea, settings, state]() {
        QList<Segment> newSegments;
        const double padding = settings.paddingSec;
        const double minimumClipDuration = settings.minimumClipDurationSec;
        bool anySilence = false;

        for (const auto &area : workArea) {
            const int si = qBound(0, area.sourceIdx, static_cast<int>(sources.size()) - 1);
            if (!state->silence.contains(si)) {
                newSegments.push_back(area);
                continue;
            }
            const QList<double> &silenceStarts = state->silence[si].first;
            const QList<double> &silenceEnds = state->silence[si].second;
            const qint64 offset = sources[si].offsetMs;
            const double areaStart = (area.startMs - offset) / 1000.0; // source-local
            const double areaEnd = (area.endMs - offset) / 1000.0;
            double lastProcessed = areaStart;

            for (int i = 0; i < silenceStarts.size(); ++i) {
                const double sStart = silenceStarts[i];
                const double sEnd = (i < silenceEnds.size()) ? silenceEnds[i] : areaEnd;
                if (sStart > areaStart && sStart < areaEnd) {
                    if (sStart - lastProcessed > minimumClipDuration) {
                        Segment s = area; // keep crop / gain / sourceIdx
                        s.startMs = offset + static_cast<qint64>(qMax(areaStart, lastProcessed - (lastProcessed == areaStart ? 0 : padding)) * 1000);
                        s.endMs = offset + static_cast<qint64>(qMin(areaEnd, sStart + padding) * 1000);
                        newSegments.push_back(s);
                    }
                    lastProcessed = sEnd;
                    anySilence = true;
                }
            }

            if (areaEnd - lastProcessed > minimumClipDuration) {
                Segment s = area;
                s.startMs = offset + static_cast<qint64>(qMax(areaStart, lastProcessed - padding) * 1000);
                s.endMs = offset + static_cast<qint64>(areaEnd * 1000);
                newSegments.push_back(s);
            }
        }

        if (!anySilence) {
            showNotification("NO SILENCE FOUND IN TRIMMED AREA");
        } else if (!newSegments.isEmpty()) {
            segments = newSegments;
            showNotification(QString("CLEANED: %1 CLIPS").arg(segments.size()));
            emit clipTrimmed();
        }
        update();
    };

    for (int si : audioSources) {
        // Only analyze the parts of this source that are actually on the timeline.
        QStringList filterParts;
        for (const auto &seg : workArea) {
            if (qBound(0, seg.sourceIdx, static_cast<int>(sources.size()) - 1) != si) continue;
            const double s = (seg.startMs - sources[si].offsetMs) / 1000.0;
            const double e = (seg.endMs - sources[si].offsetMs) / 1000.0;
            filterParts << QString("between(t,%1,%2)").arg(s).arg(e);
        }

        const QString selectFilter = QString("aselect='%1',silencedetect=noise=%2dB:d=%3")
                                         .arg(filterParts.join("+"))
                                         .arg(settings.silenceThresholdDb, 0, 'f', 1)
                                         .arg(settings.minimumSilenceDurationSec, 0, 'f', 2);

        QStringList args;
        args << "-i" << sources[si].path
             << "-map" << QString("0:a:%1").arg(si == 0 ? currentAudioTrack : 0)
             << "-af" << selectFilter
             << "-f" << "null" << "-";

        QProcess* ffmpeg = new QProcess(this);
        connect(ffmpeg, &QProcess::finished, this, [this, ffmpeg, si, state, finalize]() {
            const QString output = ffmpeg->readAllStandardError();

            static const QRegularExpression startRegex("silence_start: (\\d+\\.?\\d*)");
            static const QRegularExpression endRegex("silence_end: (\\d+\\.?\\d*)");

            QList<double> starts, ends;
            auto startMatches = startRegex.globalMatch(output);
            while (startMatches.hasNext()) starts << startMatches.next().captured(1).toDouble();
            auto endMatches = endRegex.globalMatch(output);
            while (endMatches.hasNext()) ends << endMatches.next().captured(1).toDouble();

            if (!starts.isEmpty()) state->silence[si] = {starts, ends};
            ffmpeg->deleteLater();
            if (--state->pending == 0) finalize();
        });

        ffmpeg->start(getFFToolPath("ffmpeg"), args);
    }
}
void TimelineWidget::detectAudioTracks(const QString &path) {
    auto *probe = new QProcess(this);
    QStringList args;
    args << "-v" << "error" << "-show_entries" << "stream=codec_type,index" << "-of" << "csv=p=0" << path;

    connect(probe, &QProcess::finished, this, [this, probe, path](int exitCode) {
        if (exitCode != 0) {
            showNotification("TRACK DETECTION FAILED ❌");
            hasAudioStream = false;
            hasVideoStream = false;
            probe->deleteLater();
            emit mediaProbingFinished();
            return;
        }

        QString output = probe->readAllStandardOutput().trimmed();
        QStringList lines = output.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
        
        int audioCount = 0;
        bool hasVideo = false;
        for (const QString &line : lines) {
            const QStringList fields = line.toLower().split(',', Qt::SkipEmptyParts);
            for (QString field : fields) {
                field = field.trimmed();
                if (field == "audio") {
                    audioCount++;
                    break;
                }
                if (field == "video") {
                    hasVideo = true;
                    break;
                }
            }
        }

        totalAudioTracks = qMax(1, audioCount);
        hasAudioStream = audioCount > 0;
        hasVideoStream = hasVideo;
        
        // Safety: If currentAudioTrack is out of bounds for the new file, reset it
        if (currentAudioTrack >= totalAudioTracks) {
            currentAudioTrack = 0;
        }

        if (hasAudioStream) {
            loadAudioFast(path);
        } else {
            audioSamples.clear();
            update();
            emit mediaProbingFinished();
        }
        
        if (!hasAudioStream && !hasVideo) {
             showNotification("NO MEDIA STREAMS FOUND ⚠️");
        }

        probe->deleteLater();
    });

    probe->start(getFFToolPath("ffprobe"), args);
}

bool TimelineWidget::isAnySelectedMuted() {
    QSet<int> targets = selectedSegmentIndices;
    if (selectedSegmentIdx != -1) targets.insert(selectedSegmentIdx);
    for (int idx : targets) if (segments[idx].muted) return true;
    return false;
}

void TimelineWidget::updateEditorVolume() {
    if (!thumbPlayer || !thumbPlayer->audioOutput()) return; // Added safety check

    float currentClipGain = 1.0f;
    for (const auto& seg : segments) {
        if (currentPosMs >= seg.startMs && currentPosMs <= seg.endMs) {
            currentClipGain = seg.gain;
            if (seg.muted) currentClipGain = 0.0f;
            break;
        }
    }
    thumbPlayer->audioOutput()->setVolume(qBound(0.0f, audioGain * currentClipGain, 5.0f));
}
