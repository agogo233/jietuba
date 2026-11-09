#include "tools/capture_history.h"
#include "core/data_location.h"
#include "core/i18n.h"
#include "core/logger.h"
#include "settings/tool_settings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <algorithm>
#include <functional>

namespace jietuba {
namespace {
// The decoded entries kept around so a keypress does not re-decode the same PNG.
constexpr int MAX_CACHE = 3;
}

QMutex CaptureHistory::file_mutex_;

CaptureHistory::CaptureHistory() = default;

CaptureHistory::CaptureHistoryLimits CaptureHistory::limits() {
    const auto *config = get_tool_settings_manager();
    CaptureHistoryLimits limits;
    limits.retention_days = config->get_capture_history_retention_days();
    limits.max_count = config->get_capture_history_max_count();
    limits.max_total_mb = config->get_capture_history_max_total_mb();
    return limits;
}

QString CaptureHistory::directory() {
    return QDir(data_location().dir).filePath(QStringLiteral("capture_history"));
}

QStringList CaptureHistory::entries() const {
    QStringList names;
    {
        QMutexLocker lock(&file_mutex_);
        names = scan();
    }
    // Newest first, as HistoryNavigator expects (index 0 = newest).
    std::sort(names.begin(), names.end(), std::greater<QString>());
    return names;
}

QImage CaptureHistory::load(const QString &id) const {
    // The cache is only touched from the UI thread (load/clear), so no lock is needed.
    for (int i = 0; i < cache_ids_.size(); ++i) {
        if (cache_ids_[i] != id) continue;
        const QImage image = cache_images_[i];
        cache_ids_.move(i, 0);
        cache_images_.move(i, 0);
        return image;
    }
    QImage image;
    {
        QMutexLocker lock(&file_mutex_);
        image.load(QDir(directory()).filePath(id));
    }
    if (image.isNull()) return image;
    cache_ids_.prepend(id);
    cache_images_.prepend(image);
    while (cache_ids_.size() > MAX_CACHE) {
        cache_ids_.removeLast();
        cache_images_.removeLast();
    }
    return image;
}

void CaptureHistory::record(const QImage &image, const CaptureHistoryLimits &limits) {
    if (image.isNull()) return;
    QString base = QStringLiteral("capture_%1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmsszzz")));
    QString id = base + QStringLiteral(".png");
    const QString dir = directory();
    QMutexLocker lock(&file_mutex_);
    QDir().mkpath(dir);
    for (int suffix = 2; QFile::exists(QDir(dir).filePath(id)); ++suffix) id = base + QStringLiteral("_%1.png").arg(suffix);
    const QString path = QDir(dir).filePath(id);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        log_warning(T("无法写入截图历史: {path}", {{"path", QDir::toNativeSeparators(path)}}), "CaptureHistory");
        return;
    }
    if (!image.save(&file, "PNG")) {
        log_warning(T("写入截图历史失败: {path}", {{"path", QDir::toNativeSeparators(path)}}), "CaptureHistory");
        file.cancelWriting();
        return;
    }
    if (!file.commit()) log_warning(T("提交截图历史失败: {path}", {{"path", QDir::toNativeSeparators(path)}}), "CaptureHistory");
    enforce_limits(limits);
}

void CaptureHistory::clear() {
    {
        QMutexLocker lock(&file_mutex_);
        for (const auto &id : scan()) erase_file(id);
    }
    cache_ids_.clear();
    cache_images_.clear();
}

void CaptureHistory::clear_storage() {
    QMutexLocker lock(&file_mutex_);
    for (const auto &id : scan()) erase_file(id);
}

QStringList CaptureHistory::scan() {
    const QDir entry_dir(directory());
    QStringList names;
    const auto infos = entry_dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const auto &info : infos) {
        if (info.isFile()) names.append(info.fileName());
    }
    return names;
}

void CaptureHistory::erase_file(const QString &id) {
    const QString path = QDir(directory()).filePath(id);
    if (QFile::exists(path) && !QFile::remove(path))
        log_warning(T("删除截图历史失败: {path}", {{"path", QDir::toNativeSeparators(path)}}), "CaptureHistory");
}

void CaptureHistory::enforce_limits(const CaptureHistoryLimits &limits) {
    // Oldest first, so the newest capture always survives the limits.
    QStringList names = scan();
    std::sort(names.begin(), names.end());
    if (names.size() <= 1) return;
    const qint64 retention_ms = static_cast<qint64>(limits.retention_days) * 24 * 3600 * 1000;
    const qint64 max_count = limits.max_count;
    const qint64 max_total_bytes = static_cast<qint64>(limits.max_total_mb) * 1024 * 1024;
    const QDateTime now = QDateTime::currentDateTime();
    qint64 total_bytes = 0;
    for (const auto &id : names) total_bytes += QFileInfo(QDir(directory()).filePath(id)).size();
    // No ++it in the step: erase already advances it, so every removed entry is accounted
    // for exactly once and the newest entry is never walked past.
    for (auto it = names.begin(); it != names.end() && names.size() > 1;) {
        const QFileInfo info(QDir(directory()).filePath(*it));
        const bool too_old = retention_ms > 0 && info.lastModified().msecsTo(now) > retention_ms;
        const bool too_many = max_count > 0 && static_cast<qint64>(names.size()) > max_count;
        const bool too_big = max_total_bytes > 0 && total_bytes > max_total_bytes;
        if (!too_old && !too_many && !too_big) break;
        erase_file(*it);
        total_bytes -= info.size();
        it = names.erase(it);
    }
}
}
