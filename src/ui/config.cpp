#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

// First existing candidate wins; when none exists the caller's fallback is
// kept so doctor/status report a concrete (MISSING) path instead of an
// empty one.
QString firstExisting (const QStringList& candidates, const QString& fallback) {
    for (const QString& candidate : candidates)
        if (!candidate.isEmpty () && QFile::exists (candidate))
            return candidate;
    return fallback;
}

// Steam install layouts, matching linux-wallpaperengine's own auto-detection
// list (native, ~/.steam symlink, flatpak, snap).
QStringList steamRoots () {
    const QString home = QDir::homePath ();
    return {
        home + "/.steam/steam",
        home + "/.local/share/Steam",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        home + "/snap/steam/common/.local/share/Steam",
    };
}

} // namespace

QString Config::configDir () {
    const QString base = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation);
    return base + "/lwe-dynamic-wallpaper";
}

QString Config::configPath () { return configDir () + "/config.json"; }

Config Config::load () {
    Config config;

    // Resolve defaults from standard install locations; the config file
    // (and a .deb install) overrides them.
    QStringList engineCandidates {
        "/opt/linux-wallpaperengine/linux-wallpaperengine", // deb payload layout
        "/usr/local/bin/linux-wallpaperengine",
        "/usr/bin/linux-wallpaperengine",
    };
    if (QCoreApplication::instance () != nullptr) {
        const QString appDir = QCoreApplication::applicationDirPath ();
        engineCandidates << appDir + "/../linux-wallpaperengine" // deb: bin/ sibling of the flat engine install
                         << appDir + "/linux-wallpaperengine";   // flat dev tree
    }
    config.enginePath = firstExisting (engineCandidates, "/opt/linux-wallpaperengine/linux-wallpaperengine");

    // an empty result is intentional: argvbuilder then omits --assets-dir
    // and the engine runs its own auto-detection
    QStringList assetsCandidates;
    for (const QString& root : steamRoots ())
        assetsCandidates << root + "/steamapps/common/wallpaper_engine/assets";
    config.assetsDir = firstExisting (assetsCandidates, QString ());

    QStringList workshopCandidates;
    for (const QString& root : steamRoots ())
        workshopCandidates << root + "/steamapps/workshop/content/431960";
    config.workshopDir = firstExisting (workshopCandidates, workshopCandidates.first ());

    QFile file (configPath ());
    if (!file.open (QIODevice::ReadOnly)) {
	return config;
    }

    const QJsonObject obj = QJsonDocument::fromJson (file.readAll ()).object ();
    return Config::fromJson (obj);
}

Config Config::fromJson (const QJsonObject& obj) {
    Config config;
    config.enginePath = obj.value ("enginePath").toString (config.enginePath);
    config.assetsDir = obj.value ("assetsDir").toString (config.assetsDir);
    config.workshopDir = obj.value ("workshopDir").toString (config.workshopDir);
    config.display = obj.value ("display").toString (config.display);
    config.scaling = obj.value ("scaling").toString (config.scaling);
    config.clamp = obj.value ("clamp").toString (config.clamp);
    config.fps = obj.value ("fps").toInt (config.fps);
    config.fullscreenPause = obj.value ("fullscreenPause").toBool (config.fullscreenPause);
    config.automute = obj.value ("automute").toBool (config.automute);
    config.audioProcessing = obj.value ("audioProcessing").toBool (config.audioProcessing);
    config.volume = obj.value ("volume").toInt (config.volume);
    config.silent = obj.value ("silent").toBool (config.silent);
    config.disableParticles = obj.value ("disableParticles").toBool (config.disableParticles);
    config.disableMouse = obj.value ("disableMouse").toBool (config.disableMouse);
    config.disableParallax = obj.value ("disableParallax").toBool (config.disableParallax);

    config.screens.clear ();
    const QJsonObject screens = obj.value ("screens").toObject ();
    for (auto it = screens.begin (); it != screens.end (); ++it) {
	config.screens.insert (it.key (), it.value ().toString ());
    }

    config.properties = obj.value ("properties").toObject ().toVariantMap ();
    return config;
}

QJsonObject Config::toJson () const {
    QJsonObject obj;
    obj.insert ("enginePath", enginePath);
    obj.insert ("assetsDir", assetsDir);
    obj.insert ("workshopDir", workshopDir);
    obj.insert ("scaling", scaling);
    obj.insert ("clamp", clamp);
    obj.insert ("fps", fps);
    obj.insert ("fullscreenPause", fullscreenPause);
    obj.insert ("automute", automute);
    obj.insert ("audioProcessing", audioProcessing);
    obj.insert ("volume", volume);
    obj.insert ("silent", silent);
    obj.insert ("disableParticles", disableParticles);
    obj.insert ("disableMouse", disableMouse);
    obj.insert ("disableParallax", disableParallax);

    QJsonObject screensJson;
    for (auto it = screens.begin (); it != screens.end (); ++it) {
	screensJson.insert (it.key (), it.value ());
    }
    obj.insert ("display", display);
    obj.insert ("screens", screensJson);
    obj.insert ("properties", QJsonObject::fromVariantMap (properties));
    return obj;
}

bool Config::save () const {
    QDir ().mkpath (configDir ());
    QFile file (configPath ());
    if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
	return false;
    }
    file.write (QJsonDocument (toJson ()).toJson (QJsonDocument::Indented));
    return true;
}

