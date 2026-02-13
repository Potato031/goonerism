//
// Created by potato on 2/12/26.
//

#ifndef SIMPLEVIDEOEDITOR_RESIZEFILTER_H
#define SIMPLEVIDEOEDITOR_RESIZEFILTER_H
#include <qcoreevent.h>
#include <qevent.h>
#include <QWidget>

class ResizeFilter : public QObject {
    QWidget* target;
public:
    ResizeFilter(QWidget* t) : target(t) {}
protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::Resize) {
            target->resize(static_cast<QResizeEvent*>(e)->size());
        }
        return false;
    }
};

#endif //SIMPLEVIDEOEDITOR_RESIZEFILTER_H