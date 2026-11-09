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

// The sidecar holding an entry's selection, in image-local coordinates. The stem is the
// PNG's own (ids always end in ".png"), so an entry is a self-contained file pair.
QString sidecar_path(const QString &id) {
    return QDir(CaptureHistory::directory()).filePath(id.left(id.size() - 4) + QStringLiteral(".txt"));
}

QString selection_text(const QRect &rect) {
    return QStringLiteral("%1 %2 %3 %4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

QRectF parse_selection(const QString &text) {
    const QStringList parts = text.split(' ', Qt::SkipEmptyParts);
    if (parts.size() != 4) return QRectF();
    bool ok[4] = {false, false, false, false};
    const int x = parts[0].toInt(&ok[0]);
    const int y = parts[1].toInt(&ok[1]);
    const int w = parts[2].toInt(&ok[2]);
    const int h = parts[3].toInt(&ok[3]);
    if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || w <= 0 || h <= 0) return QRectF();
    return QRectF(x, y, w, h);
}
}

QMutex CaptureHistory::file_mutex_;

CaptureHistory::CaptureHistory() = default;

CaptureHistoryLimits CaptureHistory::limits() {
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

void CaptureHistory::record(const QImage &image, const QRectF &selection_in_image, const CaptureHistoryLimits &limits) {
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
    if (!file.commit()) {
        log_warning(T("提交截图历史失败: {path}", {{"path", QDir::toNativeSeparators(path)}}), "CaptureHistory");
        return;  // No PNG on disk, so no sidecar may be left for it.
    }
    // A capture without a selection (should not happen) keeps no sidecar; switching then
    // selects the whole image, so a missing or unreadable sidecar only degrades that entry.
    if (selection_in_image.isValid() && !selection_in_image.isEmpty()) {
        const QString meta_path = sidecar_path(id);
        QSaveFile meta(meta_path);
        if (meta.open(QIODevice::WriteOnly)) {
            meta.write(selection_text(selection_in_image.toAlignedRect()).toUtf8());
            if (!meta.commit())
                log_warning(T("写入截图历史选区失败: {path}", {{"path", QDir::toNativeSeparators(meta_path)}}), "CaptureHistory");
        } else {
            log_warning(T("无法写入截图历史选区: {path}", {{"path", QDir::toNativeSeparators(meta_path)}}), "CaptureHistory");
        }
    }
    enforce_limits(limits);
}

QRectF CaptureHistory::selection_of(const QString &id) const {
    QMutexLocker lock(&file_mutex_);
    QFile meta(sidecar_path(id));
    if (!meta.open(QIODevice::ReadOnly)) return QRectF();
    return parse_selection(QString::fromUtf8(meta.readAll()));
}

void CaptureHistory::clear() {
    {
        // removeRecursively also clears orphaned sidecars left by a manual PNG delete.
        QMutexLocker lock(&file_mutex_);
        QDir(directory()).removeRecursively();
    }
    cache_ids_.clear();
    cache_images_.clear();
}

void CaptureHistory::clear_storage() {
    QMutexLocker lock(&file_mutex_);
    QDir(directory()).removeRecursively();
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
    const QString meta = sidecar_path(id);
    if (QFile::exists(meta) && !QFile::remove(meta))
        log_warning(T("删除截图历史选区失败: {path}", {{"path", QDir::toNativeSeparators(meta)}}), "CaptureHistory");
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
