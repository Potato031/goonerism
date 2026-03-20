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
#include <functional>
#include "timelinewidget.h"
#include "mediautils.h"

class DropFilter : public QObject {
    Q_OBJECT
    std::function<void(const QString&)> m_openMedia;

public:
    explicit DropFilter(std::function<void(const QString&)> openMedia, QObject *parent = nullptr)
        : QObject(parent), m_openMedia(std::move(openMedia)) {}

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

                if (!filePath.isEmpty() && MediaUtils::isSupportedMediaFile(filePath) && m_openMedia) {
                    m_openMedia(filePath);
                }
            }
            dropEvent->acceptProposedAction();
            return true;
        }
        return false;
    }
};

#endif //SIMPLEVIDEOEDITOR_DROPFILTER_H
