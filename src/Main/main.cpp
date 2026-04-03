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
#include <QSettings>
#include "../Includes/mainWindow.h"
#include "../Includes/appsettings.h"

int main(int argc, char *argv[]) {
    qputenv("QT_MULTIMEDIA_PREFERRED_PLUGINS", "ffmpeg");

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Potatoes");
    QCoreApplication::setApplicationName("PotatoEditor");
    app.setStyle("Fusion");

    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        const QString baseStyles = QString::fromUtf8(styleFile.readAll());
        app.setProperty("baseStyleSheet", baseStyles);
        app.setStyleSheet(baseStyles);
        styleFile.close();
    }

    MainWindow window;
    QSettings settings = makeAppSettings();
    if (!settings.contains("window/geometry")) {
        window.resize(1280, 850);
    }
    window.show();

    const bool shouldCheckUpdates = settings.contains("ui/checkForUpdatesOnStartup")
        ? settings.value("ui/checkForUpdatesOnStartup", true).toBool()
        : settings.value("general/checkForUpdatesOnStartup", true).toBool();
    if (shouldCheckUpdates) {
        const int delayMs = settings.contains("ui/updateCheckDelayMs")
            ? settings.value("ui/updateCheckDelayMs", 2000).toInt()
            : settings.value("general/updateCheckDelayMs", 2000).toInt();
        QTimer::singleShot(qMax(0, delayMs), &window, &MainWindow::checkForUpdates);
    }
    return app.exec();
}
