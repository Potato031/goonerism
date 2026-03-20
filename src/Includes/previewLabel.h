#ifndef PREVIEWLABEL_H
#define PREVIEWLABEL_H

#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QFileInfo>
#include <QMouseEvent>
#include <QResizeEvent>

class PreviewLabel : public QLabel {
    Q_OBJECT
public:
    explicit PreviewLabel(const QString &videoPath, QWidget *parent = nullptr);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void generatePreview();
    void updatePreview(int index);
    void renderAudioPlaceholder();

    QString path;
    QPixmap filmstrip;
    int frameCount = 10;
    bool isHovered = false;
    bool isAudioFile = false;
};

#endif // PREVIEWLABEL_H
