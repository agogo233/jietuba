#include "canvas/items/drawing_items.h"
#include "canvas/scene.h"
#include "canvas/selection_model.h"
#include "tools/history_navigator.h"
#include <QImage>
#include <QList>
#include <QPen>
#include <QString>
#include <QWidget>
#include <QtTest>

namespace jietuba {
namespace {
QImage make_image(const QSize &size, QRgb fill) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(fill);
    return image;
}
}

// Exercises HistoryNavigator with a plain widget (so replace_background falls
// back to the scene background) and injected loaders, off the clipboard backend.
class HistoryNavigatorTests : public QObject {
    Q_OBJECT
private slots:
    void empty_history();
    void navigates_and_returns();
    void skips_missing();
    void undo_stack_isolated();
    void annotation_stashed_and_restored();
    void live_selection_null_restore();
    void smart_selection_tracking_allows_switch();
};

// Builds a navigator over a fresh scene whose live background is `live`.
void HistoryNavigatorTests::empty_history() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    QWidget parent;
    QList<QString> toasts;
    HistoryNavigator navigator(&scene, &parent, [] { return QList<QString>{}; }, [](const QString &) { return QImage(); },
                               [] { return true; }, [&toasts](const QString &m) { toasts.append(m); });
    QVERIFY(!navigator.step_older());
    QCOMPARE(toasts.size(), 1);
    QVERIFY(navigator.step_newer());
    QVERIFY(!navigator.return_to_current());
}

void HistoryNavigatorTests::navigates_and_returns() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    const QImage older = make_image(QSize(100, 60), qRgb(255, 0, 0));
    const QImage older2 = make_image(QSize(120, 80), qRgb(0, 0, 255));
    QWidget parent;
    const QList<QString> ids{QStringLiteral("a"), QStringLiteral("b")};
    const QList<QImage> images{older, older2};
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&images](const QString &id) { return id == QStringLiteral("a") ? images[0] : images[1]; },
                               [] { return true; }, [](const QString &) {});
    // A live selection to restore.
    scene.preset_selection(QRectF(10, 10, 50, 30));

    QVERIFY(navigator.step_older());
    QCOMPARE(scene.background->image().size(), older.size());
    QCOMPARE(scene.background->image().pixelColor(0, 0), QColor(255, 0, 0));
    QVERIFY(scene.selection_model->is_confirmed());
    QCOMPARE(scene.selection_model->rect(), QRectF(0, 0, 100, 60));

    QVERIFY(navigator.step_older());
    QCOMPARE(scene.background->image().size(), older2.size());
    QCOMPARE(scene.selection_model->rect(), QRectF(0, 0, 120, 80));

    QVERIFY(navigator.step_newer());
    QCOMPARE(scene.selection_model->rect(), QRectF(0, 0, 100, 60));

    QVERIFY(navigator.step_newer());
    QVERIFY(!navigator.browsing());
    QCOMPARE(scene.background->image().size(), live.size());
    QCOMPARE(scene.background->image().pixelColor(0, 0), QColor(0, 255, 0));
    QCOMPARE(scene.selection_model->rect(), QRectF(10, 10, 50, 30));
}

void HistoryNavigatorTests::skips_missing() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    const QImage present = make_image(QSize(40, 40), qRgb(0, 0, 0));
    QWidget parent;
    const QList<QString> ids{QStringLiteral("gone"), QStringLiteral("present")};
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&present](const QString &id) { return id == QStringLiteral("present") ? present : QImage(); },
                               [] { return true; }, [](const QString &) {});
    QVERIFY(navigator.step_older());
    QCOMPARE(scene.background->image().size(), present.size());
    QCOMPARE(scene.selection_model->rect(), QRectF(0, 0, 40, 40));
}

void HistoryNavigatorTests::undo_stack_isolated() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    const QList<QString> ids{QStringLiteral("a")};
    const QList<QImage> images{make_image(QSize(100, 60), qRgb(255, 0, 0))};
    QWidget parent;
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&images](const QString &) { return images[0]; }, [] { return true; }, [](const QString &) {});
    auto *live_stack = scene.undo_stack;
    QVERIFY(navigator.step_older());
    QVERIFY(scene.undo_stack != live_stack);
    QVERIFY(navigator.return_to_current());
    QCOMPARE(scene.undo_stack, live_stack);
}

void HistoryNavigatorTests::annotation_stashed_and_restored() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    auto *item = new RectItem(QRectF(10, 10, 20, 20), QPen(QColor(255, 0, 0), 3));
    scene.addItem(item);
    const QList<QString> ids{QStringLiteral("a")};
    const QList<QImage> images{make_image(QSize(100, 60), qRgb(255, 0, 0))};
    QWidget parent;
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&images](const QString &) { return images[0]; }, [] { return true; }, [](const QString &) {});
    QVERIFY(scene.items().contains(item));
    QVERIFY(navigator.step_older());
    QVERIFY(!scene.items().contains(item));  // Stashed away, kept alive by the lease.
    QVERIFY(navigator.return_to_current());
    QVERIFY(scene.items().contains(item));  // Re-attached on return.
}

void HistoryNavigatorTests::smart_selection_tracking_allows_switch() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    const QList<QString> ids{QStringLiteral("a")};
    const QList<QImage> images{make_image(QSize(100, 60), qRgb(255, 0, 0))};
    QWidget parent;
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&images](const QString &) { return images[0]; }, [] { return true; }, [](const QString &) {});
    // The smart selection tracks the cursor as a drag (no button held); a real drag holds
    // one. The switch must be allowed for cursor tracking, so merely moving the mouse no
    // longer blocks history browsing.
    scene.selection_model->start_dragging();
    QVERIFY(navigator.step_older());
    QCOMPARE(scene.background->image().pixelColor(0, 0), QColor(255, 0, 0));
    QVERIFY(!scene.selection_model->is_dragging());  // The tracking ended with the switch.
}

void HistoryNavigatorTests::live_selection_null_restore() {
    const QImage live = make_image(QSize(200, 100), qRgb(0, 255, 0));
    CanvasScene scene(live, QRectF(0, 0, 200, 100));
    const QList<QString> ids{QStringLiteral("a")};
    const QList<QImage> images{make_image(QSize(100, 60), qRgb(255, 0, 0))};
    QWidget parent;
    HistoryNavigator navigator(&scene, &parent, [&ids] { return ids; },
                               [&images](const QString &) { return images[0]; }, [] { return true; }, [](const QString &) {});
    QVERIFY(navigator.step_older());
    QVERIFY(navigator.return_to_current());
    QVERIFY(scene.selection_model->rect().isNull());
}
}

QTEST_MAIN(jietuba::HistoryNavigatorTests)
#include "test_history_navigator.moc"
