#pragma once
#include "core/export.h"
#include <QPointer>
#include <QRectF>
#include <QWidget>
#include <functional>
#include <memory>

class QGraphicsItem;

namespace jietuba {
class CanvasScene;
class CaptureHistory;
class HistoryNavigator;
class SaveService;
class ToolSettingsManager;

// The window ActionTools works for; cleanup_and_close when it has one, else close().
class ActionHost {
public:
    virtual ~ActionHost() = default;
    virtual void cleanup_and_close() = 0;
    // Gone for the user (hidden, a new capture may start) while an action still finishes on the scene.
    // The object returned lives until cleanup_and_close; a wait hung on it ends there too.
    virtual QObject *hand_over() = 0;
};

// The toolbar's action buttons: confirm, copy, save, pin, translate, recognize, scan.
class ActionTools {
public:
    ActionTools(CanvasScene *scene, ToolSettingsManager *config_manager = nullptr, QWidget *parent_window = nullptr);
    // Copies to the clipboard, auto-saves per settings and closes.
    void handle_confirm();
    // Same as confirm.
    void handle_copy();
    // Asks where to save.
    void handle_save(bool close_after = true);
    // Straight into the save folder without asking, whether or not auto-save is on.
    void handle_quick_save();
    // A pin window gets the clean base image plus the annotations as vector items,
    // so they stay editable; the composite still goes to the clipboard.
    void handle_pin(bool close_after = true);
    // Steps to the previous or next capture kept in this application's own history,
    // restoring the live endpoint at the newest end and on Esc/close.
    void handle_history_prev();
    void handle_history_next();
    // Returns to the capture being edited without stepping through newer history.
    void handle_history_return();
    // One capture action chosen by a mouse binding; false when it does not apply.
    bool handle_capture_action(const QString &action);
    // Recognizes the selection, closes the capture, then opens the translation window.
    void handle_screenshot_translate();
    // Recognition is slow (hundreds of ms locally), so the result window opens first and waits.
    void handle_text_recognize();
    // Lists QR codes and barcodes in the selection after closing the capture.
    void handle_scan_code();
    // The save window, or the system Save As dialog when the setting asks for it.
    static void save_image_interactively(QWidget *parent, const QImage &image, const QString &base_name, ToolSettingsManager *config,
                                         const std::shared_ptr<SaveService> &service);
    CanvasScene *scene;
    ToolSettingsManager *config_manager;
    QPointer<QWidget> parent_window;
    ExportService export_service;
    std::shared_ptr<SaveService> save_service;
    std::unique_ptr<HistoryNavigator> history_navigator;
    // This application's own capture history, independent of the clipboard history. Shared with
    // the recording task, which may outlive this object (the window closes first).
    std::shared_ptr<CaptureHistory> capture_history_;
private:
    // Export -> clipboard -> auto save -> close.
    void copy_save_and_close(bool close_after = true);
    // Runs finish now, or, while a smart erase in the selection still rebuilds its pixels, hands the
    // window over and runs it once they are in.
    void finish_when_ready(const QRectF &selection_rect, std::function<void()> finish);
    static bool has_annotations(const QList<QGraphicsItem *> &drawing_items);
    // Quick save skips deliver_screenshot, so "also save the original" is handled here.
    void save_original_beside(const QRectF &selection_rect, const QString &saved_path);
    // The selection without annotations, which would cover text and codes; warns and
    // returns a null image without a confirmed selection or on export failure.
    QImage selection_base_image();
    // Scene coordinates are absolute virtual desktop coordinates, stored as is.
    void remember_last_region(const QRectF &rect);
    // Leaves editing so selection frames and handles are not exported.
    void temporarily_exit_editing();
    // Stores a capture's base image in the history, asynchronously, unless disabled or the
    // capture being confirmed is already a history entry (browsing).
    void record_capture_history(const QImage &base_image);
    void cleanup_and_close();
    static QString format_from_save_path(const QString &file_path, const QString &selected_filter);
};
}
