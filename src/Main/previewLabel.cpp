#include "../Includes/previewLabel.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QCoreApplication>
#include <QMap>
#include <QApplication>
#include <QSharedPointer>
#include <functional>
#include "../Includes/mediautils.h"

// Static cache to store whether a preview is being generated or has failed
static QMap<QString, bool> g_generationCache;

static QString getFFmpegPath() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}

QQueue<std::function<void()>> g_previewJobs;
QQueue<std::function<void()>> g_previewStripJobs;
bool g_previewJobRunning = false;

void startNextPreviewJob() {
    if (g_previewJobRunning || (g_previewJobs.isEmpty() && g_previewStripJobs.isEmpty())) {
        return;
    }

    g_previewJobRunning = true;
    auto job = g_previewJobs.isEmpty() ? g_previewStripJobs.dequeue() : g_previewJobs.dequeue();
    job();
}

void enqueuePreviewJob(std::function<void()> job, bool animated) {
    if (animated) g_previewStripJobs.enqueue(std::move(job));
    else g_previewJobs.enqueue(std::move(job));
    startNextPreviewJob();
}

void finishPreviewJob() {
    g_previewJobRunning = false;
    QTimer::singleShot(10, []() {
        startNextPreviewJob();
    });
}

PreviewLabel::PreviewLabel(const QString &videoPath, QWidget *parent)
    : QLabel(parent), path(videoPath) {

    isAudioFile = MediaUtils::isKnownAudioFile(path);
    // Move styling to QSS or standardized here
    setMouseTracking(true);
    setAlignment(Qt::AlignCenter);

    sourceIdentity = MediaUtils::previewCachePath(path);
    generatePreview();
}

// Re-points a recycled media-bin row at a different file.
void PreviewLabel::setSource(const QString &videoPath) {
    const QString identity = MediaUtils::previewCachePath(videoPath);
    if (path == videoPath && sourceIdentity == identity) return;
    path = videoPath;
    setToolTip(QString());
    sourceIdentity = identity;
    isAudioFile = MediaUtils::isKnownAudioFile(path);
    filmstrip = QPixmap();
    frameCount = 1;
    generatePreview();
}

void PreviewLabel::generatePreview(bool animated) {
    // Container suffixes (notably Discord WebM recordings) do not tell us
    // whether there is a video stream. Remember successful audio-only probes.
    if (isAudioFile || QFile::exists(sourceIdentity + ".audio")) {
        isAudioFile = true;
        renderAudioPlaceholder();
        return;
    }
    if (animated && frameCount == 10) return;
    const QString sourcePath = path;
    const QString identity = sourceIdentity;
    // Separate caches avoid reusing the old keyframe-only strips with blank cells.
    const QString outPath = identity + (animated ? ".strip-v2.jpg" : ".thumb-v2.jpg");
    QPixmap cached;
    if (cached.load(outPath)) {
        filmstrip = cached;
        frameCount = animated ? 10 : 1;
        updatePreview(0);
        return;
    }
    if (filmstrip.isNull()) renderLoadingPlaceholder();
    if (g_generationCache.value(outPath, false)) {
        QTimer::singleShot(150, this, [this, identity, animated]() {
            if (sourceIdentity == identity && (!animated || isHovered)) generatePreview(animated);
        });
        return;
    }
    g_generationCache[outPath] = true;
    enqueuePreviewJob([self = QPointer<PreviewLabel>(this), sourcePath, identity, outPath, animated]() {
        if (!self || self->sourceIdentity != identity || (animated && !self->isHovered)) {
            g_generationCache[outPath] = false;
            finishPreviewJob();
            return;
        }
        auto *process = new QProcess(qApp);
        MediaUtils::prioritizeInteractivePlayback(process);
        auto completed = QSharedPointer<bool>::create(false);
        auto finish = [self, identity, outPath, animated, process, completed](bool success) {
            if (*completed) return;
            *completed = true;
            g_generationCache[outPath] = false;
            if (self && self->sourceIdentity == identity) {
                QPixmap result;
                if (self->isAudioFile) {
                    self->renderAudioPlaceholder();
                } else if (success && result.load(outPath)) {
                    // A late thumbnail must not overwrite a strip loaded on hover.
                    if (animated || self->frameCount != 10) {
                        self->filmstrip = result;
                        self->frameCount = animated ? 10 : 1;
                        self->updatePreview(0);
                    }
                } else if (self->filmstrip.isNull()) self->renderErrorPlaceholder();
            }
            if (!success) QFile::remove(outPath);
            process->deleteLater();
            finishPreviewJob();
        };
        auto probingAudio = QSharedPointer<bool>::create(false);
        QObject::connect(process, &QProcess::finished, qApp,
                         [self, identity, sourcePath, outPath, process, finish, probingAudio, completed](int code) {
            if (*completed) return;
            if (*probingAudio) {
                const auto streams = process->readAllStandardOutput().split('\n');
                const bool audioOnly = code == 0 && streams.contains("audio") && !streams.contains("video");
                if (audioOnly) {
                    QFile marker(identity + ".audio");
                    if (marker.open(QIODevice::WriteOnly)) marker.close();
                    if (self && self->sourceIdentity == identity) self->isAudioFile = true;
                }
                finish(audioOnly);
                return;
            }
            if (code == 0 && QFileInfo(outPath).size() > 0) {
                finish(true);
                return;
            }
            // ffmpeg cannot extract a frame from an audio-only container.
            // Probe asynchronously only after extraction fails, so normal
            // thumbnails still require just one decoder process.
            *probingAudio = true;
            process->readAllStandardOutput();
#ifdef Q_OS_WIN
            const QString ffprobe = QCoreApplication::applicationDirPath() + "/ffprobe.exe";
#else
            const QString ffprobe = "ffprobe";
#endif
            process->start(ffprobe, {"-v", "error", "-show_entries", "stream=codec_type",
                                     "-of", "csv=p=0", sourcePath});
        });
        QObject::connect(process, &QProcess::errorOccurred, qApp, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false);
        });
        QTimer::singleShot(animated ? 20000 : 5000, process, [process, finish]() { process->kill(); finish(false); });
        QStringList args{"-v", "error", "-y", "-threads", "1", "-i", sourcePath,
                         "-an", "-filter_threads", "1", "-vf"};
        // Decode one frame for the bin. Build the scrub strip only on hover;
        // repeat the final frame so sub-second clips fill every cell as well.
        args.append(animated
            ? "tpad=stop_mode=clone:stop_duration=10,fps=1:start_time=0,scale=160:-2,trim=duration=10,tile=10x1"
            : "scale=160:-2");
        args.append({"-frames:v", "1", outPath});
        process->start(getFFmpegPath(), args);
    }, animated);
}

void PreviewLabel::updatePreview(int index) {
    if (isAudioFile) {
        renderAudioPlaceholder();
        return;
    }

    if (filmstrip.isNull()) return;

    int frameWidth = filmstrip.width() / frameCount;
    int xOffset = index * frameWidth;

    QPixmap frame = filmstrip.copy(xOffset, 0, frameWidth, filmstrip.height());
    setPixmap(frame.scaled(this->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

void PreviewLabel::renderLoadingPlaceholder() {
    QPixmap pixmap(size().isValid() ? size() : QSize(160, 90));
    pixmap.fill(QColor("#0a0c10"));
    setPixmap(pixmap);
}

void PreviewLabel::renderErrorPlaceholder() {
    QPixmap pixmap(size().isValid() ? size() : QSize(160, 90));
    pixmap.fill(QColor("#1B1B22"));
    QPainter painter(&pixmap);
    painter.setPen(QColor("#A6A4B2"));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "No preview");
    setToolTip("Preview unavailable. Open the file to check whether it can be played.");
    setPixmap(pixmap);
}

void PreviewLabel::renderAudioPlaceholder() {
    QPixmap pixmap(size().isValid() ? size() : QSize(160, 90));
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(pixmap.rect(), QColor("#0f1b20"));

    painter.fillRect(pixmap.rect().adjusted(2, 2, -2, -2), QColor("#252A2D"));

    QFont iconFont = font();
    iconFont.setBold(true);
    iconFont.setPointSize(qMax(16, pixmap.height() / 3));
    painter.setFont(iconFont);
    painter.setPen(QColor("#b9fff0"));
    painter.drawText(pixmap.rect().adjusted(0, -10, 0, 0), Qt::AlignCenter, "A");

    QFont textFont = font();
    textFont.setPointSize(qMax(8, pixmap.height() / 10));
    textFont.setWeight(QFont::DemiBold);
    painter.setFont(textFont);
    painter.setPen(QColor("#d7f6ef"));
    painter.drawText(pixmap.rect().adjusted(6, pixmap.height() / 2, -6, -6), Qt::AlignHCenter | Qt::AlignTop,
                     QFileInfo(path).suffix().toUpper());

    setPixmap(pixmap);
}

void PreviewLabel::enterEvent(QEnterEvent *event) {
    isHovered = true;
    if (!isAudioFile) generatePreview(true);
    QLabel::enterEvent(event);
}

void PreviewLabel::leaveEvent(QEvent *event) {
    isHovered = false;
    updatePreview(0);
    QLabel::leaveEvent(event);
}

void PreviewLabel::mouseMoveEvent(QMouseEvent *event) {
    if (isHovered && !filmstrip.isNull()) {
        int index = (event->pos().x() * frameCount) / width();
        index = qBound(0, index, frameCount - 1);
        updatePreview(index);
    }
    QLabel::mouseMoveEvent(event);
}

void PreviewLabel::resizeEvent(QResizeEvent *event) {
    QLabel::resizeEvent(event);
    if (isAudioFile) {
        renderAudioPlaceholder();
    } else if (!filmstrip.isNull()) {
        updatePreview(0);
    }
}
