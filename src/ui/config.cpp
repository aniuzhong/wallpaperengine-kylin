#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

QString Config::configDir () {
    const QString base = QStandardPaths::writableLocation (QStandardPaths::GenericConfigLocation);
    return base + "/lwe-dynamic-wallpaper";
}

QString Config::configPath () { return configDir () + "/config.json"; }

Config Config::load () {
    Config config;

    // known-good defaults for this machine's layout; a .deb install
    // overrides them
    config.enginePath = "/home/hido/仓库/linux-wallpaperengine/build/output/linux-wallpaperengine";
    config.assetsDir = "/home/hido/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/wallpaper_engine/assets";
    config.workshopDir = "/home/hido/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960";

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
    config.scaling = obj.value ("scaling").toString (config.scaling);
    config.clamp = obj.value ("clamp").toString (config.clamp);
    config.fps = obj.value ("fps").toInt (config.fps);
    config.fullscreenPause = obj.value ("fullscreenPause").toBool (config.fullscreenPause);
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
    obj.insert ("volume", volume);
    obj.insert ("silent", silent);
    obj.insert ("disableParticles", disableParticles);
    obj.insert ("disableMouse", disableMouse);
    obj.insert ("disableParallax", disableParallax);

    QJsonObject screensJson;
    for (auto it = screens.begin (); it != screens.end (); ++it) {
	screensJson.insert (it.key (), it.value ());
    }
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

