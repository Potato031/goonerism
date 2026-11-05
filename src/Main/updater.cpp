//
// Created by potato on 2/13/26.
//
#include <QFileInfo>
#include <QMessageBox>
#include <QJsonArray>
#include <QProcess>
#include <QProgressDialog>
#include <QTemporaryFile>
#include "../Includes/updateutils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../Includes/mainWindow.h"


void MainWindow::checkForUpdates() {
    if (isUpdating) return;

    auto* manager = new QNetworkAccessManager(this);
    QUrl url("https://api.github.com/repos/Potato031/goonerism/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PotatoEditor-Updater");
    request.setRawHeader("Accept", "application/vnd.github.v3+json");

    connect(manager, &QNetworkAccessManager::finished, [this, manager](QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QString latestTag = obj.value("tag_name").toString();

            if (latestTag.startsWith('v')) latestTag = latestTag.mid(1);
            if (!latestTag.isEmpty() && latestTag != CURRENT_VERSION && !isUpdating) {
                QJsonArray assets = obj.value("assets").toArray();
                QString downloadUrl;

                for (const QJsonValue& asset : assets) {
                    QString name = asset.toObject().value("name").toString();
#ifdef Q_OS_WIN
                    if (name.endsWith(".zip")) downloadUrl = asset.toObject().value("browser_download_url").toString();
#else
                    if (name.endsWith(".appimage", Qt::CaseInsensitive)) downloadUrl = asset.toObject().value("browser_download_url").toString();
#endif
                }

                if (!downloadUrl.isEmpty()) {
                    isUpdating = true;
                    auto res = QMessageBox::question(this, "Update Available",
                        "A new version (" + latestTag + ") is available. Update now?",
                        QMessageBox::Yes | QMessageBox::No);

                    if (res == QMessageBox::Yes) {
                        if (player) player->pause(); // Pause video
                        downloadUpdate(downloadUrl);
                    } else {
                        isUpdating = false;
                    }
                }
            }
        }
        reply->deleteLater();
        manager->deleteLater();
    });
    manager->get(request);
}

void MainWindow::downloadUpdate(const QString &url) {
    auto* manager = new QNetworkAccessManager(this);
    QNetworkReply* reply = manager->get(QNetworkRequest(QUrl(url)));
    auto* progress = new QProgressDialog("Downloading update...", "Cancel", 0, 100, this);

    connect(reply, &QNetworkReply::finished, [this, reply, progress, manager]() {
        progress->close();
        if (reply->error() == QNetworkReply::NoError) {
            QString appPath = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
            const QString appImage = qEnvironmentVariable("APPIMAGE");
            if (!appImage.isEmpty()) appPath = appImage;
#endif
            // Keep the exact basename, case and directory of the launched app.
            appPath = QFileInfo(appPath).absoluteFilePath();
            const QString folderDir = QFileInfo(appPath).absolutePath();
#ifdef Q_OS_WIN
            const QString suffix = ".zip";
#else
            const QString suffix = ".appimage";
#endif
            QTemporaryFile file(folderDir + "/.potato-update-XXXXXX" + suffix);
            const QByteArray payload = reply->readAll();
            if (payload.isEmpty() || !file.open() || file.write(payload) != payload.size() || !file.flush()) {
                QMessageBox::critical(this, "Update Error", "Could not save the complete update in: " + folderDir);
                isUpdating = false;
            } else {
                file.close();
                // If launching the installer fails, the temporary download is
                // removed automatically and the existing app remains untouched.
                if (finalizeUpdate(file.fileName(), appPath)) file.setAutoRemove(false);
            }
        } else {
            isUpdating = false;
        }
        reply->deleteLater();
        manager->deleteLater();
    });

    connect(reply, &QNetworkReply::downloadProgress, [progress](qint64 received, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>((received * 100) / total));
    });
}

bool MainWindow::finalizeUpdate(const QString &stagedPath, const QString &appPath) {
    const QString folderDir = QFileInfo(appPath).absolutePath();
#ifdef Q_OS_WIN
    QTemporaryFile script(folderDir + "/.potato-updater-XXXXXX.ps1");
    const QByteArray contents = UpdateUtils::windowsInstallerScript().toUtf8();
#else
    QTemporaryFile script(folderDir + "/.potato-updater-XXXXXX.sh");
    const QByteArray contents = UpdateUtils::linuxInstallerScript().toUtf8();
#endif
    if (!script.open() || script.write(contents) != contents.size() || !script.flush()) {
        QMessageBox::critical(this, "Update Error", "Could not prepare the update installer.");
        isUpdating = false;
        return false;
    }
    const QString scriptPath = script.fileName();
    script.close();
    const QString pid = QString::number(QCoreApplication::applicationPid());
#ifdef Q_OS_WIN
    const bool started = QProcess::startDetached("powershell.exe",
        {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", scriptPath,
         stagedPath, appPath, pid}, folderDir);
#else
    const bool started = QProcess::startDetached("/bin/bash", {scriptPath, stagedPath, appPath, pid}, folderDir);
#endif
    if (!started) {
        QMessageBox::critical(this, "Update Error", "Could not start the update installer.");
        isUpdating = false;
        return false;
    }
    script.setAutoRemove(false);
    qApp->quit();
    return true;
}
