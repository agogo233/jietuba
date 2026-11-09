#include "tools/history_navigator.h"
#include "canvas/items/background_item.h"
#include "canvas/items/spotlight_item.h"
#include "canvas/items/watermark_item.h"
#include "canvas/scene.h"
#include "canvas/selection_model.h"
#include "canvas/smart_erase_wait.h"
#include "core/i18n.h"
#include "core/logger.h"
#include "ui/selection_info/controller.h"
#include <QGraphicsItem>
#include <QGuiApplication>

namespace jietuba {
namespace {
QString history_tr(const char *text) { return i18n_tr(text, "HistoryNavigator"); }
}

HistoryNavigator::HistoryNavigator(CanvasScene *scene, QWidget *parent_window, ListLoader list_loader, ImageLoader image_loader,
                                   ActiveCheck is_active, Notifier notifier)
    : scene_(scene), parent_window_(parent_window), list_loader_(std::move(list_loader)), image_loader_(std::move(image_loader)),
      is_active_(std::move(is_active)), notifier_(std::move(notifier)) {}

bool HistoryNavigator::ensure_history() {
    if (history_loaded_) return !history_.isEmpty();
    history_loaded_ = true;
    history_ = list_loader_ ? list_loader_() : QList<QString>();
    return !history_.isEmpty();
}

void HistoryNavigator::begin_browsing() {
    // Snapshot the live endpoint: image, selection, annotations and undo stack.
    live_image_ = scene_->background ? scene_->background->image() : QImage();
    live_selection_ = scene_->selection_model->rect();
    live_stack_ = scene_->undo_stack;
    live_items_.clear();
    WatermarkItem *watermark = WatermarkItem::find(scene_);
    for (auto *item : scene_->get_drawing_items_in_rect(scene_->scene_rect)) {
        if (item == watermark) continue;  // The watermark follows the new selection.
        auto *tracked = dynamic_cast<TrackedItem *>(item);
        if (!tracked) continue;  // Untracked parts stay attached (not expected).
        scene_->removeItem(item);
        // The lease keeps the item alive and deletes it if never re-attached.
        live_items_.append(ItemReference(tracked));
    }
    // The shared spotlight curtain is not an annotation, but browsing spotlights
    // change its darkness; remember the live value to restore on return.
    if (auto *curtain = SpotlightCurtain::find(scene_)) live_curtain_opacity_ = curtain->opacity();
    // A fresh stack for annotations drawn while browsing; the live stack is
    // restored untouched on return.
    temp_stack_ = new CommandUndoStack(scene_);
    scene_->set_active_undo_stack(temp_stack_);
}

void HistoryNavigator::discard_history_edits() {
    if (!temp_stack_) return;
    WatermarkItem *watermark = WatermarkItem::find(scene_);
    for (auto *item : scene_->get_drawing_items_in_rect(scene_->scene_rect)) {
        if (item == watermark) continue;
        scene_->removeItem(item);
        // Deleting a TrackedItem nulls its lifetime, so the temp stack's
        // commands for it become safe no-ops; clear() then releases them.
        delete item;
    }
    temp_stack_->clear();
}

void HistoryNavigator::apply_entry(int index, const QImage &image) {
    auto *host = dynamic_cast<SelectionInfoHost *>(parent_window_);
    if (host) host->replace_background(image);
    else if (scene_->background) scene_->background->update_image(image);
    // History images are the exported selections, drawn from the desktop origin;
    // selecting the whole image lets the user confirm, crop or copy it at once.
    const QSizeF size(image.size());
    scene_->preset_selection(QRectF(scene_->scene_rect.topLeft(), size));
    index_ = index;
    log_debug(T("切换历史截图: index={index}", {{"index", index}}), "HistoryNavigator");
}

bool HistoryNavigator::switch_allowed() {
    auto *model = scene_->selection_model;
    // A real selection drag holds a button; the smart selection's cursor tracking
    // only sets the dragging flag, and switching ends that tracking cleanly.
    if (model->is_dragging() && QGuiApplication::mouseButtons() != Qt::NoButton) return false;
    if (model->is_dragging()) model->stop_dragging();
    return true;
}

bool HistoryNavigator::step_older() {
    if (!is_active_ || !is_active_()) return false;
    if (!switch_allowed()) return false;
    if (smart_erase_pending(scene_, scene_->selection_model->rect())) return false;
    if (!ensure_history()) {
        toast_boundary();
        return false;
    }
    // Skip captures that vanished (deleted while browsing).
    for (int target = index_ + 1; target < history_.size(); ++target) {
        const QImage image = image_loader_ ? image_loader_(history_[target]) : QImage();
        if (image.isNull()) continue;
        if (!browsing()) begin_browsing();
        else discard_history_edits();
        apply_entry(target, image);
        return true;
    }
    toast_boundary();
    return false;
}

bool HistoryNavigator::step_newer() {
    if (!browsing()) return false;
    if (!is_active_ || !is_active_()) return false;
    if (!switch_allowed()) return false;
    if (smart_erase_pending(scene_, scene_->selection_model->rect())) return false;
    // Newest end of the history is the live endpoint itself.
    for (int target = index_ - 1; target >= 0; --target) {
        const QImage image = image_loader_ ? image_loader_(history_[target]) : QImage();
        if (image.isNull()) continue;
        discard_history_edits();
        apply_entry(target, image);
        return true;
    }
    return return_to_current();
}

bool HistoryNavigator::return_to_current() {
    if (!browsing()) return false;
    discard_history_edits();
    // Restore the live stack before re-adding the items its commands expect.
    scene_->set_active_undo_stack(live_stack_);
    auto *host = dynamic_cast<SelectionInfoHost *>(parent_window_);
    if (host) host->replace_background(live_image_);
    else if (scene_->background) scene_->background->update_image(live_image_);
    for (const auto &ref : live_items_) {
        if (auto *item = ref.get()) scene_->addItem(item);
    }
    live_items_.clear();
    // Restore the live curtain darkness, overwritten by any browsing spotlight.
    if (auto *curtain = SpotlightCurtain::find(scene_)) curtain->setOpacity(live_curtain_opacity_);
    if (!live_selection_.isNull()) scene_->preset_selection(live_selection_);
    else scene_->selection_model->deactivate();
    delete temp_stack_;
    temp_stack_ = nullptr;
    index_ = -1;
    log_debug(T("返回当前截图"), "HistoryNavigator");
    return true;
}

void HistoryNavigator::toast_boundary() {
    if (notifier_) notifier_(history_tr("No earlier screenshot"));
}
}
