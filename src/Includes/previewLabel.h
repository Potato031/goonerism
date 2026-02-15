#ifndef PREVIEWLABEL_H
#define PREVIEWLABEL_H

#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QFileInfo>
#include <QMouseEvent>

class PreviewLabel : public QLabel {
    Q_OBJECT
public:
    explicit PreviewLabel(const QString &videoPath, QWidget *parent = nullptr);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void generatePreview();
    void updatePreview(int index);

    QString path;
    QPixmap filmstrip;
    int frameCount = 10;
    bool isHovered = false;
};

#endif // PREVIEWLABEL_H