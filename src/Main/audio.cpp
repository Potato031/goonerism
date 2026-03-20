#include <QAudioOutput>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>

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
        return;
    }

    QString tempAudioPath = QDir::tempPath() + "/waveform_data.raw";
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
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFToolPath("ffmpeg"), args);
}
void TimelineWidget::autoCutSilence() {
    if (durationMs <= 0 || isExporting || segments.empty() || !hasAudioStream) {
        showNotification("NO AUDIO TRACK TO ANALYZE");
        return;
    }
    saveState();
    showNotification("ANALYZING TRIMMED SECTIONS");

    // Capture the current segments we want to process
    // We copy them so that if the user changes the timeline while FFmpeg runs, we don't crash
    QList<Segment> originalWorkArea = segments;

    // We'll use a complex filter to only analyze the segments you've kept
    // This is more efficient than analyzing the whole file
    QStringList filterParts;
    for (const auto& seg : originalWorkArea) {
        double s = seg.startMs / 1000.0;
        double d = (seg.endMs - seg.startMs) / 1000.0;
        filterParts << QString("between(t,%1,%2)").arg(s).arg(s + d);
    }

    // This filter tells FFmpeg: "Only process audio if the time is within my segments"
    QString selectFilter = QString("aselect='%1',silencedetect=noise=-45dB:d=0.3").arg(filterParts.join("+"));

    QStringList args;
    args << "-i" << currentFileUrl.toLocalFile()
         << "-map" << QString("0:a:%1").arg(currentAudioTrack)
         << "-af" << selectFilter
         << "-f" << "null" << "-";

    QProcess* ffmpeg = new QProcess(this);
    connect(ffmpeg, &QProcess::finished, [this, ffmpeg, originalWorkArea]() {
        QString output = ffmpeg->readAllStandardError();

        QList<double> silenceStarts;
        QList<double> silenceEnds;

        const QRegularExpression startRegex("silence_start: (\\d+\\.?\\d*)");
        const QRegularExpression endRegex("silence_end: (\\d+\\.?\\d*)");

        auto startMatches = startRegex.globalMatch(output);
        while (startMatches.hasNext()) silenceStarts << startMatches.next().captured(1).toDouble();

        auto endMatches = endRegex.globalMatch(output);
        while (endMatches.hasNext()) silenceEnds << endMatches.next().captured(1).toDouble();

        if (silenceStarts.isEmpty()) {
            showNotification("NO SILENCE FOUND IN TRIMMED AREA");
        } else {
            QList<Segment> newSegments;
            constexpr double padding = 0.15; // Slightly tighter padding for auto-cut

            // Process each original segment and "sub-cut" it based on detected silence
            for (const auto& area : originalWorkArea) {
                double areaStart = area.startMs / 1000.0;
                double areaEnd = area.endMs / 1000.0;
                double lastProcessed = areaStart;

                for (int i = 0; i < silenceStarts.size(); ++i) {
                    double sStart = silenceStarts[i];
                    double sEnd = (i < silenceEnds.size()) ? silenceEnds[i] : areaEnd;

                    // If the silence is inside our current segment
                    if (sStart > areaStart && sStart < areaEnd) {
                        // Add the "loud" part before this silence
                        if (sStart - lastProcessed > 0.1) {
                            Segment s;
                            s.startMs = qMax(areaStart, lastProcessed - (lastProcessed == areaStart ? 0 : padding)) * 1000;
                            s.endMs = qMin(areaEnd, sStart + padding) * 1000;
                            newSegments.push_back(s);
                        }
                        lastProcessed = sEnd;
                    }
                }

                // Add the remaining "loud" part after the last silence in this area
                if (areaEnd - lastProcessed > 0.1) {
                    Segment s;
                    s.startMs = qMax(areaStart, lastProcessed - padding) * 1000;
                    s.endMs = areaEnd * 1000;
                    newSegments.push_back(s);
                }
            }

            if (!newSegments.isEmpty()) {
                segments = newSegments;
                showNotification(QString("CLEANED: %1 CLIPS").arg(segments.size()));
            }
            update();
        }
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFToolPath("ffmpeg"), args);
}
void TimelineWidget::detectAudioTracks(const QString &path) {
    auto runProbe = [&](const QStringList &args) {
        QProcess probe;
        probe.start(getFFToolPath("ffprobe"), args);
        probe.waitForFinished();
        return probe.readAllStandardOutput().trimmed();
    };

    const QString audioOutput = runProbe({
        "-v", "error",
        "-select_streams", "a",
        "-show_entries", "stream=index",
        "-of", "csv=p=0",
        path
    });

    const QString videoOutput = runProbe({
        "-v", "error",
        "-select_streams", "v",
        "-show_entries", "stream=index",
        "-of", "csv=p=0",
        path
    });

    totalAudioTracks = audioOutput.split('\n', Qt::SkipEmptyParts).count();
    hasAudioStream = totalAudioTracks > 0;
    hasVideoStream = !videoOutput.split('\n', Qt::SkipEmptyParts).isEmpty();

    if (!hasAudioStream) totalAudioTracks = 1;
    currentAudioTrack = 0;
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
