#ifndef SIMPLEVIDEOEDITOR_MEDIAUTILS_H
#define SIMPLEVIDEOEDITOR_MEDIAUTILS_H

#include <QFileInfo>
#include <QMimeDatabase>
#include <QString>
#include <QStringList>

namespace MediaUtils {

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

inline QString importDialogFilter() {
    return "Media files (*.mp4 *.m4v *.mov *.mkv *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.ts *.m2ts *.mts *.3gp *.ogv "
           "*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.alac *.mka *.ac3 *.amr *.ape *.caf *.mid *.midi *.mp2);;"
           "Video files (*.mp4 *.m4v *.mov *.mkv *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.ts *.m2ts *.mts *.3gp *.ogv);;"
           "Audio files (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.alac *.mka *.ac3 *.amr *.ape *.caf *.mid *.midi *.mp2);;"
           "All files (*.*)";
}

}

#endif
