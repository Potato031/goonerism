#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#include "../Includes/timelinewidget.h"
#include "../Includes/appsettings.h"

// Streams ffmpeg's -progress output into the editor UI (progress bar in the
// timeline header) instead of a floating toast window.
void TimelineWidget::showProgressNotification(QProcess* process, qint64 totalMs, bool showCompletionToast) {
    Q_UNUSED(showCompletionToast);
    process->setProcessChannelMode(QProcess::MergedChannels);

    emit exportStarted("EXPORTING");

    connect(process, &QProcess::readyRead, this, [this, process, totalMs]() {
        QString data = process->readAll();
        static QRegularExpression re("out_time_us=(\\d+)");
        QRegularExpressionMatch match;
        // Take the LAST progress line in this chunk so the bar never jumps backwards.
        auto it = re.globalMatch(data);
        while (it.hasNext()) match = it.next();
        if (match.hasMatch()) {
            qint64 currentUs = match.captured(1).toLongLong();
            int progress = qBound(0, static_cast<int>((currentUs / 1000.0) / qMax<qint64>(1, totalMs) * 100), 100);
            emit exportProgress(progress);
        }
    });
}

void TimelineWidget::showNotification(const QString &message) {
    // nullptr parent is fine, but we add WindowDoesNotAcceptFocus for Windows stability
    QWidget* n = new QWidget(nullptr);
    n->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    n->setAttribute(Qt::WA_TranslucentBackground);
    n->setAttribute(Qt::WA_DeleteOnClose);
    n->setObjectName("ToastNotification");

    QLabel* l = new QLabel(message, n);
    l->setObjectName("ToastLabel");

    QVBoxLayout* lay = new QVBoxLayout(n);
    lay->addWidget(l);

    n->adjustSize();

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();

    QRect screenGeom = screen->availableGeometry();
    QSettings settings = makeAppSettings();
    const QString position = settings.value("general/notificationPosition", "top-right").toString();
    int x = screenGeom.right() - n->width() - 20;
    int y = screenGeom.top() + 20;
    if (position == "top-left") {
        x = screenGeom.left() + 20;
    } else if (position == "bottom-right") {
        y = screenGeom.bottom() - n->height() - 20;
    } else if (position == "bottom-left") {
        x = screenGeom.left() + 20;
        y = screenGeom.bottom() - n->height() - 20;
    }

    n->move(x, y);
    n->show();

    const int durationMs = settings.value("general/notificationDurationMs", 2000).toInt();
    QTimer::singleShot(qMax(250, durationMs), n, &QWidget::close);
}
