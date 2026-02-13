#ifndef SIMPLEVIDEOEDITOR_DROPFILTER_H
#define SIMPLEVIDEOEDITOR_DROPFILTER_H

#include <QFileInfo>
#include <QMimeData>
#include <QObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMediaPlayer>
#include <QLabel>
#include <QDir>
#include <QDebug>
#include <QApplication>
#include "timelinewidget.h"

class DropFilter : public QObject {
    Q_OBJECT
    QMediaPlayer* m_player;
    TimelineWidget* m_timeline;
    QLabel* m_status;

public:
    DropFilter(QMediaPlayer* p, TimelineWidget* t, QLabel* s)
        : m_player(p), m_timeline(t), m_status(s) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDropEvent*>(event);
            if (dragEvent->mimeData()->hasUrls()) {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QList<QUrl> urls = dropEvent->mimeData()->urls();

            if (!urls.isEmpty()) {
                QString filePath = urls.first().toLocalFile();

                qDebug() << "Detected Drop Path:" << filePath;

                if (!filePath.isEmpty()) {
                    static const QStringList formats = {"mp4", "mkv", "mov", "avi", "webm"};
                    QFileInfo info(filePath);

                    if (formats.contains(info.suffix().toLower())) {
                        m_player->stop();

                        QUrl localUrl = QUrl::fromLocalFile(filePath);
                        m_player->setSource(localUrl);
                        m_timeline->setMediaSource(localUrl);

                        m_player->play();

                        if (m_status) {
                            m_status->setText(QDir::toNativeSeparators(info.fileName()).toUpper());
                        }
                    }
                }
            }
            dropEvent->acceptProposedAction();
            return true;
        }
        return false;
    }
};

#endif //SIMPLEVIDEOEDITOR_DROPFILTER_H