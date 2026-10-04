#ifndef SIMPLEVIDEOEDITOR_MEDIAUTILS_H
#define SIMPLEVIDEOEDITOR_MEDIAUTILS_H

#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QMimeDatabase>
#include <QString>
#include <QStringList>
#include <QProcess>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace MediaUtils {

inline void prioritizeInteractivePlayback(QProcess *backgroundProcess) {
#ifdef Q_OS_UNIX
    // Adjust only the decoder child; playback and the UI retain normal priority.
    backgroundProcess->setChildProcessModifier([] { (void)::nice(10); });
#else
    Q_UNUSED(backgroundProcess);
#endif
}

inline QString previewCachePath(const QString &path) {
    const QFileInfo info(path);
    const QByteArray identity = (info.absoluteFilePath() + ":" + QString::number(info.size()) +
                                ":" + QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/previews";
    QDir().mkpath(directory);
    return directory + "/" + QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex() + ".jpg";
}


inline QStringList knownVideoExtensions() {
    return {
        "mp4", "m4v", "mov", "mkv", "avi", "webm", "wmv", "flv",
        "mpeg", "mpg", "ts", "m2ts", "mts", "3gp", "ogv"
    };
}

inline QStringList knownAudioExtensions() {
    return {
        "mp3", "wav", "flac", "aac", "m4a", "ogg", "opus", "wma",
        "aiff", "aif", "alac", "mka", "ac3", "amr", "ape", "caf",
        "mid", "midi", "mp2"
    };
}

inline bool hasKnownExtension(const QString &filePath, const QStringList &extensions) {
    return extensions.contains(QFileInfo(filePath).suffix().toLower());
}

inline bool isSupportedMediaFile(const QString &filePath) {
    if (filePath.isEmpty()) return false;

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) return false;

    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(info);
    const QString mimeName = mime.name();
    if (mimeName.startsWith("audio/") || mimeName.startsWith("video/")) return true;

    return hasKnownExtension(filePath, knownVideoExtensions()) ||
           hasKnownExtension(filePath, knownAudioExtensions());
}

inline bool isKnownAudioFile(const QString &filePath) {
    if (filePath.isEmpty()) return false;

    QMimeDatabase db;
    const QString mimeName = db.mimeTypeForFile(filePath).name();
    if (mimeName.startsWith("audio/")) return true;

    return hasKnownExtension(filePath, knownAudioExtensions());
}

inline bool isKnownVideoFile(const QString &filePath) {
    if (filePath.isEmpty()) return false;

    QMimeDatabase db;
    const QString mimeName = db.mimeTypeForFile(filePath).name();
    if (mimeName.startsWith("video/")) return true;

    return hasKnownExtension(filePath, knownVideoExtensions());
}

inline QString importDialogFilter() {
    return "Media files (*.mp4 *.m4v *.mov *.mkv *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.ts *.m2ts *.mts *.3gp *.ogv "
           "*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.alac *.mka *.ac3 *.amr *.ape *.caf *.mid *.midi *.mp2);;"
           "Video files (*.mp4 *.m4v *.mov *.mkv *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.ts *.m2ts *.mts *.3gp *.ogv);;"
           "Audio files (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.alac *.mka *.ac3 *.amr *.ape *.caf *.mid *.midi *.mp2);;"
           "All files (*.*)";
}

}

#endif
