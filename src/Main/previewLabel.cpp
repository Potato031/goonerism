#include "../Includes/previewLabel.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTimer>

PreviewLabel::PreviewLabel(const QString &videoPath, QWidget *parent) 
    : QLabel(parent), path(videoPath) {
    
    // Aesthetic: Black background for the "monitor" look
    setStyleSheet("background-color: #000; border-radius: 4px; border: 1px solid #1a1a1c;");
    setMouseTracking(true); // Required for mouseMoveEvent to work without clicking
    setAlignment(Qt::AlignCenter);
    
    generatePreview();
}

void PreviewLabel::generatePreview() {
    QString outPath = QDir::tempPath() + "/potato_cache_" + QFileInfo(path).baseName() + ".jpg";

    if (QFile::exists(outPath)) {
        if (filmstrip.load(outPath)) {
            updatePreview(0);
            return;
        }
    }

    // Delay prevents launching 5 processes during the sidebar animation
    QTimer::singleShot(250, this, [this, outPath]() {
        QStringList args;
        // -t 10 limits the read to 10s.
        // -preset ultrafast skips heavy compression.
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
        ffmpeg->start("ffmpeg", args);
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
    updatePreview(0); // Reset to the first frame when mouse leaves
    QLabel::leaveEvent(event);
}

void PreviewLabel::mouseMoveEvent(QMouseEvent *event) {
    if (isHovered && !filmstrip.isNull()) {
        // Map the mouse X position (0 to 100) to a frame index (0 to 9)
        int index = (event->pos().x() * frameCount) / width();
        index = qBound(0, index, frameCount - 1);
        updatePreview(index);
    }
    QLabel::mouseMoveEvent(event);
}