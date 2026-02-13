#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>

#include "../Includes/timelinewidget.h"

void TimelineWidget::showProgressNotification(QProcess* process, qint64 totalMs) {
    process->setProcessChannelMode(QProcess::MergedChannels);

    ProgressBarNotification *notif = new ProgressBarNotification("COPYING");
    notif->show();

    connect(process, &QProcess::readyRead, this, [=]() {
        QString data = process->readAll();
        static QRegularExpression re("out_time_us=(\\d+)");
        auto match = re.match(data);
        if (match.hasMatch()) {
            qint64 currentUs = match.captured(1).toLongLong();
            int progress = qBound(0, static_cast<int>((currentUs / 1000.0) / totalMs * 100), 100);
            notif->setProgress(progress);
        }
    });

    connect(process, &QProcess::finished, this, [=](int exitCode) {
        notif->close();
        notif->deleteLater();
        if(exitCode == 0) showNotification("COPIED");
        else showNotification("EXPORT FAILED ❌");
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
    int x = screenGeom.right() - n->width() - 20;
    int y = screenGeom.top() + 20;

    n->move(x, y);
    n->show();

    QTimer::singleShot(2000, n, &QWidget::close);
}