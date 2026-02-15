#include "../Includes/previewLabel.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTimer>
#include <QCoreApplication>

// Redefining the helper here so PreviewLabel knows where to find FFmpeg on Windows
static QString getFFmpegPath() {
#ifdef Q_OS_WIN
    // Looks for ffmpeg.exe in the same folder as your app's .exe
    return QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}

PreviewLabel::PreviewLabel(const QString &videoPath, QWidget *parent)
    : QLabel(parent), path(videoPath) {

    setStyleSheet("background-color: #000; border-radius: 4px; border: 1px solid #1a1a1c;");
    setMouseTracking(true);
    setAlignment(Qt::AlignCenter);

    generatePreview();
}

void PreviewLabel::generatePreview() {
    // Using QFileInfo to ensure we have a clean filename for the cache
    QString outPath = QDir::tempPath() + "/potato_cache_" + QFileInfo(path).baseName() + ".jpg";

    if (QFile::exists(outPath)) {
        if (filmstrip.load(outPath)) {
            updatePreview(0);
            return;
        }
    }

    QTimer::singleShot(250, this, [this, outPath]() {
        QStringList args;
        // Using the helper here instead of the raw "ffmpeg" string
        args << "-y" << "-ss" << "0" << "-t" << "10" << "-i" << path
             << "-vf" << "fps=1,scale=160:-1,tile=10x1"
             << "-frames:v" << "1" << "-preset" << "ultrafast" << outPath;

        QProcess *ffmpeg = new QProcess(this);
        connect(ffmpeg, &QProcess::finished, [this, outPath, ffmpeg]() {
            if (filmstrip.load(outPath)) {
                updatePreview(0);
            }
            ffmpeg->deleteLater();
        });

        // Start using the resolved path
        ffmpeg->start(getFFmpegPath(), args);
    });
}

void PreviewLabel::updatePreview(int index) {
    if (filmstrip.isNull()) return;

    int frameWidth = filmstrip.width() / frameCount;
    int xOffset = index * frameWidth;

    QPixmap frame = filmstrip.copy(xOffset, 0, frameWidth, filmstrip.height());

    setPixmap(frame.scaled(this->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
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