#pragma once
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace jietuba {
// The application's own screenshot history, kept independently of the clipboard history:
// captures are stored as their base (annotation-free) images on disk, so history switching
// works with the clipboard feature off and survives restarts. Entries are PNG files in
// <data_location>/capture_history, named by capture timestamp (newest sorts last); cleanup
// enforces a retention period, an entry count and a total size, keeping the newest entry.
// The limits a recording is cleaned against, snapshotted from the live config so the
// recording task never reads QSettings off its thread (QSettings is not thread-safe).
struct CaptureHistoryLimits {
    int retention_days = 7;
    int max_count = 20;
    int max_total_mb = 256;
};

class CaptureHistory {
public:
    // Loads and caches the directory location.
    CaptureHistory();
    // Snapshots the current limits from the live config; call on the UI thread.
    static CaptureHistoryLimits limits();
    // The stored capture ids (file names), newest first.
    QStringList entries() const;
    // Loads one capture's image; a null image means it is missing or unreadable.
    QImage load(const QString &id) const;
    // Stores `image` as the newest capture, then enforces `limits`. Never throws; a
    // storage failure is logged and ignored so captures are never blocked by the history.
    void record(const QImage &image, const CaptureHistoryLimits &limits);
    // Deletes every stored capture and clears the image cache.
    void clear();
    // Deletes every stored capture on disk (the settings page's clear button); the live
    // capture window is closed while the settings are open, so no cache is stale.
    static void clear_storage();
    // The directory holding the captures (created lazily on first use).
    static QString directory();

private:
    static QStringList scan();
    void enforce_limits(const CaptureHistoryLimits &limits);
    static void erase_file(const QString &id);
    // Guards the directory scan/write/delete against the recording task and the UI.
    static QMutex file_mutex_;
    // A few decoded entries, so a keypress does not re-decode the same PNG.
    mutable QStringList cache_ids_;
    mutable QList<QImage> cache_images_;
};
}
