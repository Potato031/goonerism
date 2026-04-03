#ifndef SIMPLEVIDEOEDITOR_APPSETTINGS_H
#define SIMPLEVIDEOEDITOR_APPSETTINGS_H

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

inline QString appSettingsFilePath() {
    QString baseDir = QDir::homePath() + "/.config/PotatoEditor";
    QDir().mkpath(baseDir);
    return baseDir + "/settings.ini";
}

inline QSettings makeAppSettings() {
    return QSettings(appSettingsFilePath(), QSettings::IniFormat);
}

#endif // SIMPLEVIDEOEDITOR_APPSETTINGS_H
