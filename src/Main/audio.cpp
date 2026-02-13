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
    if (durationMs <= 0 || isExporting) return;
    saveState();
    showNotification("ANALYZING AUDIO");

    QStringList args;
    args << "-i" << currentFileUrl.toLocalFile()
         << "-af" << QString("silencedetect=noise=-45dB:d=0.3")
         << "-f" << "null" << "-";

    QProcess* ffmpeg = new QProcess(this);
    connect(ffmpeg, &QProcess::finished, [this, ffmpeg]() {
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
            showNotification("NO SILENCE FOUND");
        } else {
            segments.clear();
            double lastEnd = 0;
            constexpr double padding = 0.2;
            const double totalDur = durationMs / 1000.0;

            for (int i = 0; i < silenceStarts.size(); ++i) {
                const double clipStart = lastEnd;

                if (const double clipEnd = silenceStarts[i]; clipEnd - clipStart > 0.1) {
                    Segment s;
                    s.startMs = qMax(0.0, (clipStart - padding)) * 1000;
                    s.endMs = qMin(totalDur, (clipEnd + padding)) * 1000;

                    if (!segments.empty() && s.startMs < segments.back().endMs) {
                        segments.back().endMs = s.endMs;
                    } else {
                        segments.push_back(s);
                    }
                }
                if (i < silenceEnds.size()) lastEnd = silenceEnds[i];
            }

            if (totalDur - lastEnd > 0.1) {
                Segment s;
                s.startMs = qMax(0.0, (lastEnd - padding)) * 1000;
                s.endMs = totalDur * 1000;

                if (!segments.empty() && s.startMs < segments.back().endMs) {
                    segments.back().endMs = s.endMs;
                } else {
                    segments.push_back(s);
                }
            }

            showNotification(QString("AUTO-CUT: %1 CLIPS").arg(segments.size()));
            update();
        }
        ffmpeg->deleteLater();
    });

    ffmpeg->start(getFFToolPath("ffmpeg"), args);
}

void TimelineWidget::detectAudioTracks(const QString &path) {
    QProcess ffprobe;
    QStringList args;
    args << "-v" << "error" << "-select_streams" << "a"
         << "-show_entries" << "stream=index" << "-of" << "csv=p=0" << path;

    ffprobe.start(getFFToolPath("ffprobe"), args);
    ffprobe.waitForFinished();

    const QString output = ffprobe.readAllStandardOutput().trimmed();
    totalAudioTracks = output.split('\n', Qt::SkipEmptyParts).count();
    if (totalAudioTracks == 0) totalAudioTracks = 1;
    currentAudioTrack = 0;
}

bool TimelineWidget::isAnySelectedMuted() {
    QSet<int> targets = selectedSegmentIndices;
    if (selectedSegmentIdx != -1) targets.insert(selectedSegmentIdx);
    for (int idx : targets) if (segments[idx].muted) return true;
    return false;
}