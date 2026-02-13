#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#include <QApplication>
#include <QFile>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QProcess>
#include "../Includes/mainWindow.h"

int main(int argc, char *argv[]) {
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "ffmpeg");

    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        app.setStyleSheet(styleFile.readAll());
        styleFile.close();
    }

    MainWindow window;
    window.resize(1280, 850);
    window.show();

    QTimer::singleShot(2000, &window, &MainWindow::checkForUpdates);
    return app.exec();
}