#ifndef BORDERLESSWINDOW_H
#define BORDERLESSWINDOW_H

#include <QWidget>
#include <QPoint>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;

class BorderlessWindow : public QWidget {
    Q_OBJECT
public:
    explicit BorderlessWindow(QWidget *parent = nullptr);
    void setContent(QWidget *content);
    void setTitle(const QString &title);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    QVBoxLayout *mainLayout;
    QWidget *titleBar;
    QLabel *titleLabel;
    bool m_dragging = false;
    QPoint m_dragPos;
};

#endif // BORDERLESSWINDOW_H