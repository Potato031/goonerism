#include "../Includes/previewLabel.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QCoreApplication>
#include <QMap>
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
    if (isAudioFile) {
        renderAudioPlaceholder();
        return;
    }

    QString outPath = QDir::tempPath() + "/potato_cache_" + QFileInfo(path).baseName() + ".jpg";

    if (QFile::exists(outPath)) {
        if (filmstrip.load(outPath)) {
            updatePreview(0);
            return;
        }
    }

    // If already generating, don't enqueue again
    if (g_generationCache.value(path, false)) {
        renderLoadingPlaceholder();
        return;
    }

    renderLoadingPlaceholder();
    g_generationCache[path] = true;

    QTimer::singleShot(100, this, [self = QPointer<PreviewLabel>(this), outPath]() {
        enqueuePreviewJob([self, outPath]() {
            if (!self) {
                finishPreviewJob();
                return;
            }

            QStringList args;
            args << "-y" << "-ss" << "0" << "-t" << "10" << "-i" << self->path
                 << "-vf" << "fps=1,scale=160:-1,tile=10x1"
                 << "-frames:v" << "1" << "-preset" << "ultrafast" << outPath;

            QProcess *ffmpeg = new QProcess(self);
            QObject::connect(ffmpeg, &QProcess::finished, self, [self, outPath, ffmpeg]() {
                if (self) {
                    if (self->filmstrip.load(outPath)) {
                        self->updatePreview(0);
                    } else {
                        self->renderErrorPlaceholder();
                    }
                    g_generationCache[self->path] = false;
                }
                ffmpeg->deleteLater();
                finishPreviewJob();
            });

            ffmpeg->start(getFFmpegPath(), args);
        });
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

    QLinearGradient gradient(0, 0, pixmap.width(), pixmap.height());
    gradient.setColorAt(0.0, QColor("#173038"));
    gradient.setColorAt(1.0, QColor("#081115"));
    painter.fillRect(pixmap.rect().adjusted(2, 2, -2, -2), gradient);

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
