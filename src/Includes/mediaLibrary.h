#ifndef POTATO_EDITOR_MEDIA_LIBRARY_H
#define POTATO_EDITOR_MEDIA_LIBRARY_H

#include "mediautils.h"
#include <QDirIterator>
#include <QSet>
#include <algorithm>

namespace MediaLibrary {

inline QStringList mediaDirectories(const QStringList &configured) {
    QStringList roots;
    for (const auto &path : configured) {
        if (!path.trimmed().isEmpty()) roots.append(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    }
    // Some desktops map MoviesLocation to HOME. Never scan home by default.
    if (roots.isEmpty()) {
        QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (movies.isEmpty() || QDir::cleanPath(movies) == QDir::homePath())
            movies = QDir::homePath() + "/Videos";
        roots.append(movies);
    }
    roots.removeDuplicates();
    return roots;
}

struct MediaScanResult {
    QStringList files;
    QStringList identities;
    QString newestVideo;
};

inline MediaScanResult discoverMediaFiles(const QStringList &roots, const QStringList &imports, int limit) {
    QStringList filters;
    for (const auto &extension : MediaUtils::knownVideoExtensions() + MediaUtils::knownAudioExtensions())
        filters.append("*." + extension);
    QSet<QString> seen;
    QList<QFileInfo> files;
    QFileInfo newestVideo;
    const auto newer = [](const QFileInfo &a, const QFileInfo &b) {
        if (a.lastModified() == b.lastModified()) return a.absoluteFilePath() < b.absoluteFilePath();
        return a.lastModified() > b.lastModified();
    };
    auto addFile = [&](const QFileInfo &file) {
        const QString path = file.canonicalFilePath();
        if (path.isEmpty() || seen.contains(path)) return;
        seen.insert(path);
        if (MediaUtils::hasKnownExtension(path, MediaUtils::knownVideoExtensions()) &&
            (newestVideo.filePath().isEmpty() || newer(file, newestVideo))) newestVideo = file;
        // Keep only the requested number, rather than sorting an entire library.
        const auto position = std::lower_bound(files.begin(), files.end(), file, newer);
        files.insert(position, file);
        if (files.size() > limit) files.removeLast();
    };
    for (const auto &root : roots) {
        QDirIterator entries(root, filters, QDir::Files, QDirIterator::NoIteratorFlags);
        while (entries.hasNext()) {
            entries.next();
            addFile(entries.fileInfo());
        }
    }
    for (const auto &path : imports) {
        const QFileInfo file(path);
        if (file.exists() && file.isFile()) addFile(file);
    }
    MediaScanResult result;
    for (const auto &file : files) {
        result.files.append(file.absoluteFilePath());
        result.identities.append(file.absoluteFilePath() + ":" + QString::number(file.size()) + ":" +
                                 QString::number(file.lastModified().toMSecsSinceEpoch()));
    }
    if (!newestVideo.filePath().isEmpty()) result.newestVideo = newestVideo.absoluteFilePath();
    return result;
}

} // namespace MediaLibrary

#endif
