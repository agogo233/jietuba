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
    history.record(a, limits);
    history.record(b, limits);
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 2);
    // Newest first: the second recorded capture is first.
    QCOMPARE(history.load(entries[0]).pixelColor(0, 0), QColor(0, 255, 0));
    QCOMPARE(history.load(entries[1]).pixelColor(0, 0), QColor(255, 0, 0));
}

void CaptureHistoryTests::loads_entries() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(40, 40), qRgb(0, 0, 255)), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    const QImage image = history.load(entries[0]);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(40, 40));
    QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255));
}

void CaptureHistoryTests::enforces_count_limit() {
    get_tool_settings_manager()->set_capture_history_max_count(1);
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), CaptureHistory::limits());
    history.record(make_image(QSize(10, 10), qRgb(2, 0, 0)), CaptureHistory::limits());
    history.record(make_image(QSize(10, 10), qRgb(3, 0, 0)), CaptureHistory::limits());
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
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), CaptureHistory::limits());
    const QStringList entries = history.entries();
    QCOMPARE(entries.size(), 1);
    // Age the stored capture beyond the retention window, then record another: the old one goes.
    const QDateTime old = QDateTime::currentDateTime().addDays(-2);
    bool ok = false;
    QFile::setFileTime(QDir(CaptureHistory::directory()).filePath(entries[0]), old, QFileDevice::FileModificationTime, &ok);
    QVERIFY(ok);
    history.record(make_image(QSize(10, 10), qRgb(2, 0, 0)), CaptureHistory::limits());
    QCOMPARE(history.entries().size(), 1);
    QCOMPARE(history.load(history.entries()[0]).pixelColor(0, 0), QColor(2, 0, 0));
    get_tool_settings_manager()->set_capture_history_retention_days(7);
}

void CaptureHistoryTests::clears_storage() {
    CaptureHistory history;
    history.clear();
    history.record(make_image(QSize(10, 10), qRgb(1, 0, 0)), CaptureHistory::limits());
    QCOMPARE(history.entries().size(), 1);
    CaptureHistory::clear_storage();
    QVERIFY(history.entries().isEmpty());
}
}

QTEST_MAIN(jietuba::CaptureHistoryTests)
#include "test_capture_history.moc"
