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
bool g_previewJobRunning = false;

void startNextPreviewJob() {
    if (g_previewJobRunning || g_previewJobs.isEmpty()) {
        return;
    }

    g_previewJobRunning = true;
    auto job = g_previewJobs.dequeue();
    job();
}

void enqueuePreviewJob(std::function<void()> job) {
    g_previewJobs.enqueue(std::move(job));
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

    generatePreview();
}

// Re-points a recycled media-bin row at a different file.
void PreviewLabel::setSource(const QString &videoPath) {
    if (path == videoPath) return;
    path = videoPath;
    isAudioFile = MediaUtils::isKnownAudioFile(path);
    filmstrip = QPixmap();
    generatePreview();
}

void PreviewLabel::generatePreview() {
    if (isAudioFile) { renderAudioPlaceholder(); return; }
    const QString sourcePath = path;
    const QString outPath = MediaUtils::previewCachePath(path);
    if (filmstrip.load(outPath)) { updatePreview(0); return; }
    renderLoadingPlaceholder();
    // Other rows requesting the same media share the on-disk result.
    if (g_generationCache.value(sourcePath, false)) {
        QTimer::singleShot(150, this, [this, sourcePath]() {
            if (path == sourcePath) generatePreview();
        });
        return;
    }
    g_generationCache[sourcePath] = true;
    enqueuePreviewJob([self = QPointer<PreviewLabel>(this), sourcePath, outPath]() {
        if (!self || self->path != sourcePath) {
            g_generationCache[sourcePath] = false;
            finishPreviewJob();
            return;
        }
        // Jobs belong to the application, not a recycled row. Every exit path
        // releases the queue even if the row disappears while ffmpeg is running.
        auto *process = new QProcess(qApp);
        MediaUtils::prioritizeInteractivePlayback(process);
        auto completed = QSharedPointer<bool>::create(false);
        auto finish = [self, sourcePath, outPath, process, completed](bool success) {
            if (*completed) return;
            *completed = true;
            g_generationCache[sourcePath] = false;
            if (self && self->path == sourcePath) {
                if (success && self->filmstrip.load(outPath)) self->updatePreview(0);
                else self->renderErrorPlaceholder();
            }
            process->deleteLater();
            finishPreviewJob();
        };
        QObject::connect(process, &QProcess::finished, qApp, [finish](int code) { finish(code == 0); });
        QObject::connect(process, &QProcess::errorOccurred, qApp, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish(false);
        });
        QTimer::singleShot(20000, process, [process, finish]() { process->kill(); finish(false); });
        process->start(getFFmpegPath(), {"-v", "error", "-y", "-threads", "1", "-skip_frame", "nokey",
                       "-ss", "0", "-t", "10", "-i", sourcePath, "-an", "-filter_threads", "1",
                       "-vf", "fps=1,scale=160:-1,tile=10x1", "-frames:v", "1", outPath});
    });
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
    pixmap.fill(QColor("#1a0c0c"));
    QPainter painter(&pixmap);
    painter.setPen(Qt::red);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "ERROR");
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
