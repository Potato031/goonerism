#include "../Includes/previewLabel.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTimer>
#include <QCoreApplication>
#include "../Includes/mediautils.h"

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

    isAudioFile = MediaUtils::isKnownAudioFile(path);
    setStyleSheet("background-color: #091114; border-radius: 8px; border: 1px solid #1f3139;");
    setMouseTracking(true);
    setAlignment(Qt::AlignCenter);

    generatePreview();
}

void PreviewLabel::generatePreview() {
    if (isAudioFile) {
        renderAudioPlaceholder();
        return;
    }

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
