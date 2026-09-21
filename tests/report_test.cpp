// T0 pure-logic tests: the JSON wire shapes. These are the contract the CLI
// publishes with --json and that any other frontend reuses, so the
// assertions are on the exact structure consumers index into.
#include "../src/report.h"

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject parseObject(const nlohmann::json& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json.dump())).object();
}

QJsonArray parseArray(const nlohmann::json& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json.dump())).array();
}

} // namespace

class ReportTest : public QObject {
    Q_OBJECT

private slots:
    void statusCarriesUnitStateScreensAndEngine() {
        const std::map<std::string, std::string> screens { { "DP-0", "843532366" }, { "HDMI-1", "990011" } };
        const QJsonObject root =
            parseObject(report::Status("wallpaper-engine", "active", screens, "/opt/engine"));

        // the wrapper is what consumers reach through
        QCOMPARE(root.size(), 1);
        const QJsonObject status = root.value("status").toObject();
        QCOMPARE(status.value("unit").toString(), QString("wallpaper-engine"));
        QCOMPARE(status.value("state").toString(), QString("active"));
        QCOMPARE(status.value("enginePath").toString(), QString("/opt/engine"));
        QCOMPARE(status.value("screens").toObject().value("DP-0").toString(), QString("843532366"));
        QCOMPARE(status.value("screens").toObject().value("HDMI-1").toString(), QString("990011"));
    }

    void statusWithNoScreensHasAnEmptyScreensObject() {
        const QJsonObject status =
            parseObject(report::Status("unit", "inactive", {}, "/opt/engine")).value("status").toObject();
        QVERIFY(status.value("screens").isObject());
        QVERIFY(status.value("screens").toObject().isEmpty());
    }

    void libraryKeepsTheWrappedShape() {
        WallpaperEntry entry;
        entry.id = "843532366";
        entry.title = "星尘";
        entry.type = "scene";

        const QJsonArray root = parseArray(report::Library({ entry }));
        QCOMPARE(root.size(), 1); // the wrapper array
        const QJsonArray entries = root.at(0).toArray();
        QCOMPARE(entries.size(), 1);
        const QJsonObject first = entries.at(0).toObject();
        QCOMPARE(first.value("id").toString(), QString("843532366"));
        QCOMPARE(first.value("title").toString(), QString("星尘")); // UTF-8 titles survive
        QCOMPARE(first.value("type").toString(), QString("scene"));
    }

    void libraryOfNothingIsStillWrapped() {
        const QJsonArray root = parseArray(report::Library({}));
        QCOMPARE(root.size(), 1);
        QVERIFY(root.at(0).toArray().isEmpty());
    }

    void errorNamesTheKindAndKeepsTheDbusName() {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::NoSuchUnit;
        error.message = "Unit not loaded";
        error.dbusName = "org.freedesktop.systemd1.NoSuchUnit";

        const QJsonObject detail = parseObject(report::Error(error)).value("error").toObject();
        QCOMPARE(detail.value("kind").toString(), QString("no-such-unit"));
        QCOMPARE(detail.value("message").toString(), QString("Unit not loaded"));
        QCOMPARE(detail.value("dbusName").toString(), QString("org.freedesktop.systemd1.NoSuchUnit"));
    }

    void errorOmitsTheDbusNameWhenThereWasNone() {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::FileError;
        error.message = "cannot write /tmp/x";

        const QJsonObject detail = parseObject(report::Error(error)).value("error").toObject();
        QCOMPARE(detail.value("kind").toString(), QString("file-error"));
        QVERIFY(!detail.contains("dbusName"));
    }

    void errorWithNoMessageStillSaysSomething() {
        wallpaper_engine::Error error;
        error.kind = wallpaper_engine::Error::Unknown;

        const QJsonObject detail = parseObject(report::Error(error)).value("error").toObject();
        QCOMPARE(detail.value("kind").toString(), QString("unknown"));
        QVERIFY(!detail.value("message").toString().isEmpty());
    }
};

QTEST_MAIN(ReportTest)
#include "report_test.moc"
