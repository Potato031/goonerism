#include "../Includes/titlebar.h"
#include "../Includes/icons.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QWindow>

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
    setObjectName("AppTitleBar");
    setFixedHeight(34);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 8, 0);
    layout->setSpacing(8);

    appMarkLabel = new QLabel();
    appMarkLabel->setObjectName("AppMark");
    appMarkLabel->setFixedSize(14, 14);
    appMarkLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    titleLabel = new QLabel("POTATO STUDIO");
    titleLabel->setObjectName("TitleBarLabel");
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(appMarkLabel);
    layout->addWidget(titleLabel);
    layout->addStretch();

    minBtn = new QPushButton();
    maxBtn = new QPushButton();
    closeBtn = new QPushButton();
    minBtn->setObjectName("WinBtn");
    maxBtn->setObjectName("WinBtn");
    closeBtn->setObjectName("CloseBtn");
    for (QPushButton *btn : {minBtn, maxBtn, closeBtn}) {
        btn->setFixedSize(30, 24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setIconSize(QSize(12, 12));
    }
    minBtn->setToolTip("Minimize");
    maxBtn->setToolTip("Maximize");
    closeBtn->setToolTip("Close");

    layout->addWidget(minBtn);
    layout->addWidget(maxBtn);
    layout->addWidget(closeBtn);

    connect(minBtn, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeRestoreRequested);
    connect(closeBtn, &QPushButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setTitleText(const QString &text) {
    titleLabel->setText(text);
}

void TitleBar::setAppMarkColor(const QColor &color) {
    appMarkLabel->setPixmap(Icons::appMark(color).pixmap(14, 14));
}

void TitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (auto *handle = window()->windowHandle()) {
            handle->startSystemMove();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit maximizeRestoreRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

ResizeGrip::ResizeGrip(Qt::Edges edges, QWidget *parent) : QWidget(parent), m_edges(edges) {
    setAttribute(Qt::WA_TranslucentBackground);
    const bool diag = (edges & (Qt::LeftEdge | Qt::RightEdge)) && (edges & (Qt::TopEdge | Qt::BottomEdge));
    if (diag) {
        const bool backslash = (edges == (Qt::LeftEdge | Qt::TopEdge)) || (edges == (Qt::RightEdge | Qt::BottomEdge));
        setCursor(backslash ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else {
        setCursor(Qt::SizeVerCursor);
    }
}

void ResizeGrip::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !window()->isMaximized()) {
        if (auto *handle = window()->windowHandle()) {
            handle->startSystemResize(m_edges);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ResizeGrip::mouseDoubleClickEvent(QMouseEvent *event) {
    // Only the plain top edge (not the corners) doubles as a maximize toggle,
    // matching the titlebar's own double-click behavior.
    if (m_edges == Qt::TopEdge) {
        if (auto *w = window()) {
            w->isMaximized() ? w->showNormal() : w->showMaximized();
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
