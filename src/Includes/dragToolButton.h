//
// A tool button that can either be clicked (adds an overlay at the playhead)
// or dragged onto the video preview / timeline to place an overlay there.
//

#ifndef SIMPLEVIDEOEDITOR_DRAGTOOLBUTTON_H
#define SIMPLEVIDEOEDITOR_DRAGTOOLBUTTON_H

#include <QPushButton>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>

class DragToolButton : public QPushButton {
public:
    explicit DragToolButton(int overlayType, const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent), m_overlayType(overlayType) {}

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) m_pressPos = e->pos();
        QPushButton::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if ((e->buttons() & Qt::LeftButton) &&
            (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            auto *mime = new QMimeData();
            mime->setData("application/x-potato-overlay", QByteArray::number(m_overlayType));
            auto *drag = new QDrag(this);
            drag->setMimeData(mime);
            setDown(false);
            drag->exec(Qt::CopyAction);
            return;
        }
        QPushButton::mouseMoveEvent(e);
    }

private:
    int m_overlayType;
    QPoint m_pressPos;
};

#endif // SIMPLEVIDEOEDITOR_DRAGTOOLBUTTON_H
