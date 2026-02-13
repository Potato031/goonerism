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
#include "timelinewidget.h"

class DropFilter : public QObject {
    Q_OBJECT // Added macro for consistency
    QMediaPlayer* m_player;
    TimelineWidget* m_timeline;
    QLabel* m_status;

public:
    DropFilter(QMediaPlayer* p, TimelineWidget* t, QLabel* s)
        : m_player(p), m_timeline(t), m_status(s) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            // On Windows, verify it's actually a file and not just a web URL
            if (de->mimeData()->hasUrls()) {
                de->setDropAction(Qt::CopyAction);
                de->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            const QList<QUrl> urls = de->mimeData()->urls();

            if (!urls.isEmpty()) {
                QString filePath = urls.first().toLocalFile();

                // Ensure the path isn't empty (possible with some non-file URLs)
                if (filePath.isEmpty()) return false;

                static const QStringList formats = {"mp4", "mkv", "mov", "avi", "webm"};
                QFileInfo info(filePath);

                if (formats.contains(info.suffix().toLower())) {
                    m_player->stop();

                    QUrl localUrl = QUrl::fromLocalFile(filePath);
                    m_player->setSource(localUrl);
                    m_timeline->setMediaSource(localUrl);

                    m_player->play();

                    // Windows Polish: Use native separators for the status display
                    m_status->setText(QDir::toNativeSeparators(info.fileName()).toUpper());
                }
            }
            de->acceptProposedAction();
            return true;
        }
        return false;
    }
};

#endif //SIMPLEVIDEOEDITOR_DROPFILTER_H