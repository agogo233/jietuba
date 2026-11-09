#pragma once
#include "canvas/items/item_reference.h"
#include "canvas/items/spotlight_item.h"
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <functional>

class QWidget;

namespace jietuba {
class CanvasScene;
class CommandUndoStack;

// Browses previous captures inside an open capture window ("history switching").
// The capture being edited is the "live endpoint": the first step away snapshots
// its image, selection and annotations, and returning restores them losslessly,
// so an in-progress capture never loses its edits. Annotations drawn while
// browsing go to a separate undo stack and are discarded when moving on.
class HistoryNavigator {
public:
    // History capture image ids, newest first; empty when the history is
    // unavailable or holds none of this application's captures.
    using ListLoader = std::function<QList<QString>()>;
    // Loads one capture's composite image; a null image means it is gone.
    using ImageLoader = std::function<QImage(const QString &image_id)>;
    // The selection that produced a capture, in image-local coordinates; an invalid
    // rect means the entry carries none, so the whole image is selected instead.
    using SelectionLoader = std::function<QRectF(const QString &image_id)>;
    // Whether the capture session is still open and accepting input.
    using ActiveCheck = std::function<bool()>;
    // Reports a one-off message (toast).
    using Notifier = std::function<void(const QString &message)>;

    HistoryNavigator(CanvasScene *scene, QWidget *parent_window, ListLoader list_loader, ImageLoader image_loader,
                     ActiveCheck is_active = {}, Notifier notifier = {}, SelectionLoader selection_loader = {});
    ~HistoryNavigator() = default;
    // Steps to the next older capture; false when there is none or the state
    // cannot switch. Steps to the next newer capture, returning to the live
    // endpoint at the newest end; false when already there.
    bool step_older();
    bool step_newer();
    // Restores the live endpoint (the capture being edited).
    bool return_to_current();
    // True while a history capture is shown instead of the live endpoint.
    bool browsing() const { return index_ >= 0; }

private:
    // Loads and caches the history list once; false when it is empty.
    bool ensure_history();
    // Snapshot the live endpoint and switch to a fresh undo stack.
    void begin_browsing();
    // Removes and deletes annotations drawn while browsing; clears the temp stack.
    void discard_history_edits();
    // Shows the capture at `index` (history_ has already been loaded).
    void apply_entry(int index, const QImage &image);
    void toast_boundary();
    // Whether a switch may start now: not during a real selection drag. The smart
    // selection tracks the cursor as a drag, so a held button tells them apart; a
    // bare hover ends the tracking before the switch.
    bool switch_allowed();

    CanvasScene *scene_ = nullptr;
    QWidget *parent_window_ = nullptr;
    ListLoader list_loader_;
    ImageLoader image_loader_;
    ActiveCheck is_active_;
    Notifier notifier_;
    SelectionLoader selection_loader_;
    // Live endpoint snapshot.
    QImage live_image_;
    QRectF live_selection_;
    QList<ItemReference> live_items_;
    CommandUndoStack *live_stack_ = nullptr;
    double live_curtain_opacity_ = SpotlightCurtain::DEFAULT_DARKNESS;
    // Owned by the scene; deleted explicitly on return, else by the scene.
    CommandUndoStack *temp_stack_ = nullptr;
    // The browsed history, newest first.
    QList<QString> history_;
    bool history_loaded_ = false;
    // -1 = live endpoint; 0..n-1 = history entry.
    int index_ = -1;
};
}
