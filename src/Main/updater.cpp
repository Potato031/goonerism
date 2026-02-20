//
// Created by potato on 2/13/26.
//
#include <QFileInfo>
#include <QMessageBox>
#include <QJsonArray>
#include <QProcess>
#include <QProgressDialog>
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

            if (!latestTag.isEmpty() && latestTag != CURRENT_VERSION && !isUpdating) {
                QJsonArray assets = obj.value("assets").toArray();
                QString downloadUrl;

                for (const QJsonValue& asset : assets) {
                    QString name = asset.toObject().value("name").toString();
#ifdef Q_OS_WIN
                    if (name.endsWith(".zip")) downloadUrl = asset.toObject().value("browser_download_url").toString();
#else
                    if (name.endsWith(".AppImage")) downloadUrl = asset.toObject().value("browser_download_url").toString();
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
            // --- RESOLVE WRITABLE PATH ---
            QString appPath;
#ifdef Q_OS_LINUX
            char* envApp = getenv("APPIMAGE");
            appPath = envApp ? QString::fromLocal8Bit(envApp) : QCoreApplication::applicationFilePath();
#else
            appPath = QCoreApplication::applicationFilePath();
#endif
            QString folderDir = QFileInfo(appPath).absolutePath();

#ifdef Q_OS_WIN
            QString fileName = folderDir + "/update.zip";
#else
            QString fileName = folderDir + "/update.AppImage";
#endif

            QFile file(fileName);
            if (file.open(QFile::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                finalizeUpdate();
            } else {
                QMessageBox::critical(this, "Update Error", "Could not save to writable directory: " + fileName);
                isUpdating = false;
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

void MainWindow::finalizeUpdate() {
    // --- RESOLVE WRITABLE PATH ---
    QString appPath;
#ifdef Q_OS_LINUX
    char* envApp = getenv("APPIMAGE");
    appPath = envApp ? QString::fromLocal8Bit(envApp) : QCoreApplication::applicationFilePath();
#else
    appPath = QCoreApplication::applicationFilePath();
#endif
    QString folderDir = QFileInfo(appPath).absolutePath();

#ifdef Q_OS_WIN
    QFile batchFile(folderDir + "/update.bat");
    if (batchFile.open(QFile::WriteOnly)) {
        QTextStream out(&batchFile);
        out << "@echo off\n"
            << "title POTATO UPDATE ENGINE\n"
            << "echo Waiting for PotatoEditor to exit...\n"
            << ":loop\n"
            << "taskkill /f /im PotatoEditor.exe >nul 2>&1\n"
            << "timeout /t 1 /nobreak >nul\n"
            // Check if the process is STILL running
            << "tasklist /fi \"imagename eq PotatoEditor.exe\" | find /i \"PotatoEditor.exe\" >nul\n"
            // If FIND found the name (Error Level 0), the app is still there, so loop.
            << "if %errorlevel% equ 0 goto loop\n"
            << "echo App closed. Extracting update...\n"
            << "powershell -windowstyle hidden -command \"Expand-Archive -Path '" << folderDir << "/update.zip' -DestinationPath '" << folderDir << "/temp_update' -Force\"\n"
            << "echo Installing...\n"
            << "robocopy \"" << folderDir << "/temp_update\" \"" << folderDir << "\" /S /E /MOVE >nul\n"
            << "rd /s /q \"" << folderDir << "/temp_update\"\n"
            << "del /f /q \"" << folderDir << "/update.zip\"\n"
            << "start \"\" \"" << folderDir << "/PotatoEditor.exe\"\n"
            << "del \"%~f0\"\n";
        batchFile.close();

        // Use /C to ensure the window stays open if there is an error, or /Q for quiet
        QProcess::startDetached("cmd.exe", {"/c", folderDir + "/update.bat"});
        qApp->quit();
    }
#else
    QString shPath = folderDir + "/update.sh";
    QFile shFile(shPath);
    if (shFile.open(QFile::WriteOnly)) {
        QTextStream out(&shFile);
        out << "#!/bin/bash\n"
            << "sleep 2\n"
            << "cd \"" << folderDir << "\"\n"
            << "chmod +x update.AppImage\n"
            // Use quotes in case there are spaces in the user's directory name
            << "mv \"update.AppImage\" \"PotatoEditor_linux.AppImage\"\n"
            << "chmod +x \"PotatoEditor_linux.AppImage\"\n"
            << "./\"PotatoEditor_linux.AppImage\" &\n"
            << "rm -- \"$0\"\n";
        shFile.close();

        QProcess::execute("chmod", {"+x", shPath});
        QProcess::startDetached("/bin/bash", {shPath});
        qApp->quit();
    }
#endif
}