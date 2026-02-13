#include "../Includes/borderlesswindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QApplication>
#include <QGraphicsDropShadowEffect>

BorderlessWindow::BorderlessWindow(QWidget *parent) : QWidget(parent) {
    // 1. Setup Frameless & Translucent surface
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowSystemMenuHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10); // Shadow breathing room

    // 2. The Main UI Container
    auto* container = new QWidget();
    container->setObjectName("MainCanvas");
    rootLayout->addWidget(container);

    // 3. Shadow Effect
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(25);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 0);
    container->setGraphicsEffect(shadow);

    mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- CUSTOM TITLE BAR ---
    titleBar = new QWidget();
    titleBar->setObjectName("TitleBar");
    titleBar->setFixedHeight(40);

    auto* tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(15, 0, 0, 0);
    tbLayout->setSpacing(8); // Space between icon and text
    tbLayout->setAlignment(Qt::AlignVCenter);

    // Potato Icon
    auto* iconLabel = new QLabel("🥔");
    iconLabel->setStyleSheet("background: transparent; font-size: 16px;");

    // Title Branding (Fixed "Dark Box" issue)
    titleLabel = new QLabel();
    titleLabel->setStyleSheet("background-color: transparent; border: none; padding: 0px;");
    titleLabel->setText("<span style='color:#FFFFFF; font-weight:900; font-size:11px; letter-spacing:1.5px;'>POTATOES</span> "
                        "<span style='color:#3D5AFE; font-weight:400; font-size:11px; letter-spacing:1.5px;'>QUICK ONE</span>");

    // Ensure clicks pass through the text to the title bar for dragging
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* minBtn = new QPushButton("–");
    auto* maxBtn = new QPushButton("▢");
    auto* closeBtn = new QPushButton("✕");

    minBtn->setObjectName("WinBtn");
    maxBtn->setObjectName("WinBtn");
    closeBtn->setObjectName("CloseBtn");

    // Assemble Title Bar
    tbLayout->addWidget(iconLabel);
    tbLayout->addWidget(titleLabel);
    tbLayout->addStretch();
    tbLayout->addWidget(minBtn);
    tbLayout->addWidget(maxBtn);
    tbLayout->addWidget(closeBtn);

    mainLayout->addWidget(titleBar);

    // Window Control Logic
    connect(closeBtn, &QPushButton::clicked, qApp, &QApplication::quit);
    connect(minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maxBtn, &QPushButton::clicked, [this, rootLayout]() {
        if (isMaximized()) {
            showNormal();
            rootLayout->setContentsMargins(10, 10, 10, 10);
        } else {
            showMaximized();
            rootLayout->setContentsMargins(0, 0, 0, 0);
        }
    });
}

void BorderlessWindow::setContent(QWidget *content) {
    mainLayout->addWidget(content);
}

void BorderlessWindow::setTitle(const QString &title) {
    // This allows manual overrides if needed
    titleLabel->setText(title);
}

// Window Dragging & Double-Click Logic
void BorderlessWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint localPos = titleBar->mapFromGlobal(event->globalPosition().toPoint());
        if (titleBar->rect().contains(localPos)) {
            m_dragging = true;
            m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void BorderlessWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        // If maximized, restore before dragging
        if (isMaximized()) {
            // Optional: Calculate restoration ratio so window doesn't jump
            layout()->setContentsMargins(10, 10, 10, 10);
            showNormal();
        }
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}

void BorderlessWindow::mouseReleaseEvent(QMouseEvent *) {
    m_dragging = false;
}

void BorderlessWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    QPoint localPos = titleBar->mapFromGlobal(event->globalPosition().toPoint());
    if (titleBar->rect().contains(localPos)) {
        if (isMaximized()) {
            showNormal();
            layout()->setContentsMargins(10, 10, 10, 10);
        } else {
            showMaximized();
            layout()->setContentsMargins(0, 0, 0, 0);
        }
    }
}