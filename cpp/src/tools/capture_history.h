#pragma once
#include <QImage>
#include <QList>
#include <QMutex>
#include <QRectF>
#include <QString>
#include <QStringList>

namespace jietuba {
// The application's own screenshot history, kept independently of the clipboard history:
// each capture is stored as the whole frozen desktop's annotation-free image, so history
// switching goes back to the full screen of that moment. The selection that produced the
// capture travels with the entry, in image-local coordinates, as a sidecar text file
// (<entry>.txt next to the PNG), so switching can re-select it. Entries are PNG files in
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
    // The selection recorded with `id`, in image-local coordinates; an invalid rect means
    // the entry carries none (older entries, or the sidecar is missing or unreadable).
    QRectF selection_of(const QString &id) const;
    // Stores `image` (the whole frozen desktop) as the newest capture, with the selection
    // that produced it, then enforces `limits`. Never throws; a storage failure is logged
    // and ignored so captures are never blocked by the history.
    void record(const QImage &image, const QRectF &selection_in_image, const CaptureHistoryLimits &limits);
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
    // Deletes a capture's PNG and its selection sidecar; the caller holds file_mutex_.
    static void erase_file(const QString &id);
    // Guards the directory scan/write/delete against the recording task and the UI.
    static QMutex file_mutex_;
    // A few decoded entries, so a keypress does not re-decode the same PNG.
    mutable QStringList cache_ids_;
    mutable QList<QImage> cache_images_;
};
}
