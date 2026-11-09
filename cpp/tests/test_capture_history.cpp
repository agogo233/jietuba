#include "core/data_location.h"
#include "settings/tool_settings.h"
#include "tools/capture_history.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace jietuba {
namespace {
QImage make_image(const QSize &size, QRgb fill) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(fill);
    return image;
}
}

// Exercises CaptureHistory against a temporary sandbox, off the real data location.
class CaptureHistoryTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void records_newest_first();
    void loads_entries();
    void stores_selection_metadata();
    void selection_missing_without_metadata();
    void clear_removes_sidecars();
    void enforces_count_limit();
    void enforces_retention();
    void clears_storage();
private:
    QTemporaryDir sandbox_;
    DataLocation saved_location_;
    std::unique_ptr<QSettings> settings_;
};

void CaptureHistoryTests::initTestCase() {
    QVERIFY(sandbox_.isValid());
    saved_location_ = data_location();
    set_data_location({sandbox_.path(), false});
    settings_ = std::make_unique<QSettings>(QDir(sandbox_.path()).filePath("settings.ini"), QSettings::IniFormat);
    get_tool_settings_manager(settings_.get());
}

void CaptureHistoryTests::cleanupTestCase() {
    set_data_location(saved_location_);
}

void CaptureHistoryTests::records_newest_first() {
    CaptureHistory history;
    history.clear();
    const QImage a = make_image(QSize(100, 100), qRgb(255, 0, 0));
    const QImage b = make_image(QSize(120, 120), qRgb(0, 255, 0));
    const CaptureHistoryLimits limits = CaptureHistory::limits();
    history.record(a, QRectF(), limits);
    history.record(b, QRectF(), limits);
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 2);
    // Newest first: the second recorded capture is first.
    QCOMPARE(history.load(entries[0]).pixelColor(0, 0), QColor(0, 255, 0));
    QCOMPARE(history.load(entries[1]).pixelColor(0, 0), QColor(255, 0, 0));
}

void CaptureHistoryTests::loads_entries() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(40, 40), qRgb(0, 0, 255)), QRectF(), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    const QImage image = history.load(entries[0]);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(40, 40));
    QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255));
}

void CaptureHistoryTests::stores_selection_metadata() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(60, 60), qRgb(1, 2, 3)), QRectF(10, 20, 30, 40), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(history.selection_of(entries[0]), QRectF(10, 20, 30, 40));
    // The sidecar exists next to the PNG.
    const QString meta = entries[0].left(entries[0].size() - 4) + QStringLiteral(".txt");
    QVERIFY(QFile::exists(QDir(CaptureHistory::directory()).filePath(meta)));
}

void CaptureHistoryTests::selection_missing_without_metadata() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(60, 60), qRgb(1, 2, 3)), QRectF(), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    QVERIFY(!history.selection_of(entries[0]).isValid());
    const QString meta = entries[0].left(entries[0].size() - 4) + QStringLiteral(".txt");
    QVERIFY(!QFile::exists(QDir(CaptureHistory::directory()).filePath(meta)));
}

void CaptureHistoryTests::clear_removes_sidecars() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(60, 60), qRgb(1, 2, 3)), QRectF(10, 20, 30, 40), CaptureHistory::limits());
    QCOMPARE(history.entries().size(), 1);
    history.clear();
    QVERIFY(history.entries().isEmpty());
    // removeRecursively leaves nothing behind: no PNG, no orphaned sidecar.
    const QDir dir(CaptureHistory::directory());
    QVERIFY(dir.entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty());
    QVERIFY(dir.entryList({QStringLiteral("*.txt")}, QDir::Files).isEmpty());
}

void CaptureHistoryTests::enforces_count_limit() {
    get_tool_settings_manager()->set_capture_history_max_count(1);
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), QRectF(), CaptureHistory::limits());
    history.record(make_image(QSize(10, 10), qRgb(2, 0, 0)), QRectF(), CaptureHistory::limits());
    history.record(make_image(QSize(10, 10), qRgb(3, 0, 0)), QRectF(), CaptureHistory::limits());
    // Consecutive oldest entries are erased until only the newest survives.
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(history.load(entries[0]).pixelColor(0, 0), QColor(3, 0, 0));
    get_tool_settings_manager()->set_capture_history_max_count(20);
}

void CaptureHistoryTests::enforces_retention() {
    get_tool_settings_manager()->set_capture_history_retention_days(1);
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), QRectF(), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    // Age the stored capture beyond the retention window, then record another: the old one goes.
    const QDateTime old = QDateTime::currentDateTime().addDays(-2);
    bool ok = false;
    QFile::setFileTime(QDir(CaptureHistory::directory()).filePath(entries[0]), old, QFileDevice::FileModificationTime, &ok);
    QVERIFY(ok);
    history.record(make_image(QSize(10, 10), qRgb(2, 0, 0)), QRectF(), CaptureHistory::limits());
    QCOMPARE(history.entries().size(), 1);
    QCOMPARE(history.load(history.entries()[0]).pixelColor(0, 0), QColor(2, 0, 0));
    get_tool_settings_manager()->set_capture_history_retention_days(7);
}

void CaptureHistoryTests::clears_storage() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), QRectF(), CaptureHistory::limits());
    QCOMPARE(history.entries().size(), 1);
    CaptureHistory::clear_storage();
    QVERIFY(history.entries().isEmpty());
}
}

QTEST_MAIN(jietuba::CaptureHistoryTests)
#include "test_capture_history.moc"
