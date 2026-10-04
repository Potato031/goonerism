#include <QAudioOutput>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "../Includes/timelinewidget.h"
#include "../Includes/mediautils.h"

// Helper function to resolve the bundled binary path
static QString getFFToolPath(const QString &tool) {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/" + tool + ".exe";
#else
    return tool;
#endif
}

void TimelineWidget::loadAudioFast(const QString &inputPath) {
    const auto trackGeneration = ++waveformGeneration;
    if (!hasAudioStream) return;
    loadAudioWaveform(inputPath, 0, currentAudioTrack, trackGeneration);
}

void TimelineWidget::appendAudioWaveform(const QString &inputPath) {
    for (const auto &source : sources) {
        if (source.path == inputPath) {
            loadAudioWaveform(inputPath, source.offsetMs, 0, 0);
            return;
        }
    }
}

void TimelineWidget::loadAudioWaveform(const QString &path, qint64 offsetMs, int track, quint64 trackGeneration) {
    struct Waveform { QVector<float> samples; float peak = 0.01f; };
    auto temporary = QSharedPointer<QTemporaryFile>::create(QDir::tempPath() + "/potato-wave-XXXXXX.raw");
    if (!temporary->open()) return;
    const QString pcmPath = temporary->fileName();
    temporary->close();
    const auto generation = mediaGeneration;
    auto *process = new QProcess(this);
    MediaUtils::prioritizeInteractivePlayback(process);
    process->setProperty("mediaJob", true);
    connect(process, &QProcess::readyReadStandardError, this, [process]() { process->readAllStandardError(); });
    connect(process, &QProcess::finished, this,
            [this, process, temporary, pcmPath, generation, trackGeneration, offsetMs](int code) {
        process->deleteLater();
        if (code != 0 || generation != mediaGeneration ||
            (trackGeneration && trackGeneration != waveformGeneration)) return;
        auto *watcher = new QFutureWatcher<Waveform>(this);
        connect(watcher, &QFutureWatcher<Waveform>::finished, this,
                [this, watcher, temporary, generation, trackGeneration, offsetMs]() {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (generation != mediaGeneration ||
                (trackGeneration && trackGeneration != waveformGeneration)) return;
            const int start = offsetMs / 10;
            if (audioSamples.size() < start + result.samples.size()) audioSamples.resize(start + result.samples.size());
            std::copy(result.samples.cbegin(), result.samples.cend(), audioSamples.begin() + start);
            maxAmplitude = qMax(maxAmplitude, result.peak);
            update();
        });
        // Decode and reduce PCM on a worker. Only the compact 100 Hz envelope
        // crosses back to the UI; PCM reads and RMS calculations never stall input.
        watcher->setFuture(QtConcurrent::run([temporary, pcmPath]() {
            Waveform result;
            QFile file(pcmPath);
            if (!file.open(QIODevice::ReadOnly)) return result;
            QByteArray pending;
            while (!file.atEnd()) {
                pending.append(file.read(65536));
                const int windows = pending.size() / 160;
                for (int window = 0; window < windows; ++window) {
                    double sum = 0;
                    for (int j = 0; j < 80; ++j) {
                        const int index = window * 160 + j * 2;
                        const quint16 bits = quint8(pending[index]) | (quint16(quint8(pending[index + 1])) << 8);
                        const double value = static_cast<qint16>(bits) / 32768.0;
                        sum += value * value;
                    }
                    const float rms = std::pow(std::sqrt(sum / 80), 0.6);
                    result.samples.append(rms);
                    result.peak = qMax(result.peak, rms);
                }
                pending.remove(0, windows * 160);
            }
            return result;
        }));
    });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    process->start(getFFToolPath("ffmpeg"), {"-v", "error", "-y", "-threads", "1", "-i", path, "-vn",
                   "-map", QString("0:a:%1").arg(track), "-filter_threads", "1", "-f", "s16le",
                   "-ac", "1", "-ar", "8000", pcmPath});
}

void TimelineWidget::autoCutSilence() {
    if (durationMs <= 0 || isExporting || autoCutBusy || segments.empty() || !hasAudioStream) {
        showNotification("NO AUDIO TRACK TO ANALYZE");
        return;
    }
    const auto generation = mediaGeneration;
    const auto revision = editRevision;
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

    autoCutBusy = true;
    struct DetectState {
        QMap<int, QPair<QList<double>, QList<double>>> silence; // sourceIdx -> (starts, ends)
        int pending = 0;
    };
    auto state = QSharedPointer<DetectState>::create();
    state->pending = audioSources.size();

    auto finalize = [this, workArea, settings, state, generation, revision]() {
        if (generation != mediaGeneration) return;
        autoCutBusy = false;
        if (revision != editRevision) {
            showNotification("Timeline changed during analysis. Run auto-cut again.");
            return;
        }
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
            saveState("Auto-cut silence");
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
        ffmpeg->setProperty("mediaJob", true);
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

        connect(ffmpeg, &QProcess::errorOccurred, this, [this, ffmpeg, generation](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            ffmpeg->deleteLater();
            if (generation == mediaGeneration) {
                autoCutBusy = false;
                ++editRevision;
                showNotification("Cannot analyze audio. Check that ffmpeg is installed.");
            }
        });
        ffmpeg->start(getFFToolPath("ffmpeg"), args);
    }
}
void TimelineWidget::detectAudioTracks(const QString &path) {
    auto *probe = new QProcess(this);
    probe->setProperty("mediaJob", true);
    const auto generation = mediaGeneration;
    QStringList args;
    args << "-v" << "error" << "-show_entries" << "stream=codec_type,index" << "-of" << "csv=p=0" << path;

    connect(probe, &QProcess::finished, this, [this, probe, path, generation](int exitCode) {
        if (generation != mediaGeneration) { probe->deleteLater(); return; }
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

        if (!sources.isEmpty()) {
            sources[0].hasAudio = hasAudioStream;
            sources[0].hasVideo = hasVideoStream;
        }
        ensureSourceFilmstrip(0);
        emit mediaProbingFinished();
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

    connect(probe, &QProcess::errorOccurred, this, [this, probe, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        probe->deleteLater();
        if (generation == mediaGeneration) {
            showNotification("Cannot inspect media. Install ffprobe and try again.");
            emit mediaProbingFinished();
        }
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
