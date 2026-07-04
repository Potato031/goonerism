#ifndef SIMPLEVIDEOEDITOR_DROPFILTER_H
#define SIMPLEVIDEOEDITOR_DROPFILTER_H

#include <QFileInfo>
#include <QMimeData>
#include <QObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWindow>
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
    // The timeline handles its own drops (files dropped there are APPENDED as
    // extra clips, and effect buttons can be dragged onto it), so anything
    // landing on it must be left alone here. Drag events arrive on the
    // top-level QWidgetWindow — not the widget under the cursor — so the real
    // target has to be resolved from the drop position.
    static bool dropTargetsTimeline(QObject* obj, QDropEvent* dropEvent) {
        QWidget* receiver = qobject_cast<QWidget*>(obj);
        if (!receiver) {
            if (auto* win = qobject_cast<QWindow*>(obj)) {
                for (QWidget* tlw : QApplication::topLevelWidgets()) {
                    if (tlw->windowHandle() == win) { receiver = tlw; break; }
                }
            }
        }
        if (!receiver) return false;
        QWidget* under = receiver->childAt(dropEvent->position().toPoint());
        for (QWidget* w = under; w; w = w->parentWidget()) {
            if (qobject_cast<TimelineWidget*>(w)) return true;
        }
        return false;
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDropEvent*>(event);
            if (dropTargetsTimeline(obj, dragEvent)) return false;
            if (dragEvent->mimeData()->hasUrls()) {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (dropTargetsTimeline(obj, dropEvent)) return false;
            if (!dropEvent->mimeData()->hasUrls()) return false;

            const QList<QUrl> urls = dropEvent->mimeData()->urls();
            if (!urls.isEmpty()) {
                QString filePath = urls.first().toLocalFile();
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
