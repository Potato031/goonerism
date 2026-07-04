#ifndef SIMPLEVIDEOEDITOR_TITLEBAR_H
#define SIMPLEVIDEOEDITOR_TITLEBAR_H

#include <QWidget>
#include <QColor>
#include <Qt>

class QLabel;
class QPushButton;

// Slim custom titlebar replacing the OS window decoration: app mark + title
// on the left, an empty drag region, and min/max/close on the right. Dragging
// and double-click-to-maximize use QWindow::startSystemMove(), which is the
// only approach that works across X11, Wayland compositors (e.g. KWin), and
// Windows without platform-specific code — manually repositioning the window
// via move() does not work under Wayland.
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);
    void setTitleText(const QString &text);
    void setAppMarkColor(const QColor &color);

    QPushButton *minBtn;
    QPushButton *maxBtn;
    QPushButton *closeBtn;

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QLabel *titleLabel;
    QLabel *appMarkLabel;
};

// Thin, invisible strip along one edge/corner of a frameless MainWindow that
// hands off to the compositor's interactive resize on press — frameless
// windows lose the OS resize border, so this stands in for it.
class ResizeGrip : public QWidget {
    Q_OBJECT
public:
    ResizeGrip(Qt::Edges edges, QWidget *parent);
    Qt::Edges edges() const { return m_edges; }

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    Qt::Edges m_edges;
};

#endif // SIMPLEVIDEOEDITOR_TITLEBAR_H
