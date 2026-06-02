#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace bridge::storage {

inline QString applicationBaseDir()
{
    return QCoreApplication::applicationDirPath();
}

inline QString absolutePathInAppDir(const QString& relativePath)
{
    return QDir(applicationBaseDir()).filePath(relativePath);
}

inline bool ensureDirectoryExists(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }

    QDir dir(path);
    if (dir.exists()) {
        return true;
    }

    return QDir().mkpath(path);
}

inline QString configDir()
{
    return absolutePathInAppDir(QStringLiteral("config"));
}

inline QString filesDir()
{
    return absolutePathInAppDir(QStringLiteral("files"));
}

inline QString cacheDir()
{
    return absolutePathInAppDir(QStringLiteral("cache"));
}

inline QString tempDir()
{
    return absolutePathInAppDir(QStringLiteral("temp"));
}

inline QStringList legacyStorageBaseDirs()
{
    QStringList candidates;

    const auto appendCandidate = [&candidates](const QString& candidate) {
        const QString normalized = QDir::cleanPath(candidate.trimmed());
        if (normalized.isEmpty()) {
            return;
        }
        if (!candidates.contains(normalized)) {
            candidates.append(normalized);
        }
    };

    appendCandidate(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    appendCandidate(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));

    const QString appName = QCoreApplication::applicationName().trimmed();
    if (!appName.isEmpty()) {
        const QStringList baseRoots = {
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        };

        for (const QString& root : baseRoots) {
            const QString normalizedRoot = QDir::cleanPath(root.trimmed());
            if (normalizedRoot.isEmpty()) {
                continue;
            }
            const QString nested = QDir(normalizedRoot).filePath(appName);
            appendCandidate(nested);
        }
    }

    return candidates;
}

inline QStringList legacyLocationsForRelativePath(const QString& relativePath)
{
    QStringList candidates;
    for (const QString& baseDir : legacyStorageBaseDirs()) {
        const QString candidate = QDir(baseDir).filePath(relativePath);
        if (!candidates.contains(candidate)) {
            candidates.append(candidate);
        }
    }
    return candidates;
}

inline bool moveFileAcrossVolumes(const QString& sourcePath, const QString& targetPath)
{
    if (QDir::cleanPath(sourcePath) == QDir::cleanPath(targetPath)) {
        return true;
    }

    QFileInfo targetInfo(targetPath);
    if (!ensureDirectoryExists(targetInfo.dir().absolutePath())) {
        return false;
    }

    if (QFileInfo::exists(targetPath)) {
        return true;
    }

    QFile sourceFile(sourcePath);
    if (!sourceFile.exists()) {
        return false;
    }

    if (sourceFile.rename(targetPath)) {
        return true;
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        return false;
    }

    return sourceFile.remove();
}

inline bool moveDirectoryAcrossVolumes(const QString& sourcePath, const QString& targetPath)
{
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return false;
    }

    if (sourceInfo.isFile()) {
        return moveFileAcrossVolumes(sourcePath, targetPath);
    }

    if (!ensureDirectoryExists(targetPath)) {
        return false;
    }

    QDir sourceDir(sourcePath);
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& entry : entries) {
        const QString targetEntryPath = QDir(targetPath).filePath(entry.fileName());
        if (!moveDirectoryAcrossVolumes(entry.filePath(), targetEntryPath)) {
            return false;
        }
    }

    return QDir().rmdir(sourcePath);
}

inline void migrateLegacyFileIfNeeded(const QString& targetPath, const QStringList& legacyPaths)
{
    if (QFileInfo::exists(targetPath)) {
        return;
    }

    for (const QString& legacyPath : legacyPaths) {
        if (!QFileInfo::exists(legacyPath)) {
            continue;
        }
        moveFileAcrossVolumes(legacyPath, targetPath);
        return;
    }
}

inline void migrateLegacyDirectoryIfNeeded(const QString& targetPath, const QStringList& legacyPaths)
{
    QDir targetDir(targetPath);
    if (targetDir.exists() && !targetDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        return;
    }

    for (const QString& legacyPath : legacyPaths) {
        QDir legacyDir(legacyPath);
        if (!legacyDir.exists()) {
            continue;
        }
        moveDirectoryAcrossVolumes(legacyPath, targetPath);
        return;
    }
}

inline QString persistentDatasetRootDir()
{
    const QString targetDir = QDir(cacheDir()).filePath(QStringLiteral("datasets"));
    migrateLegacyDirectoryIfNeeded(targetDir, legacyLocationsForRelativePath(QStringLiteral("datasets")));
    ensureDirectoryExists(targetDir);
    return targetDir;
}

inline QString persistentDatasetFactorSupportPassCacheFilePath(int datasetId)
{
    if (datasetId <= 0) {
        return {};
    }

    const QString targetPath = QDir(persistentDatasetRootDir()).filePath(
        QStringLiteral("dataset_%1_factor_support_pass_cache.json").arg(datasetId));
    ensureDirectoryExists(QFileInfo(targetPath).dir().absolutePath());
    return targetPath;
}

inline QString riskConfigurationFilePath()
{
    const QString targetPath = QDir(configDir()).filePath(QStringLiteral("risk/risk_configuration.json"));
    migrateLegacyFileIfNeeded(targetPath, legacyLocationsForRelativePath(QStringLiteral("risk/risk_configuration.json")));
    ensureDirectoryExists(QFileInfo(targetPath).dir().absolutePath());
    return targetPath;
}

} // namespace bridge::storage
