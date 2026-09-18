#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariantMap>

// The config file is a JSON projection of linux-wallpaperengine's CLI
// arguments. Every field maps to an engine flag.
struct Config {
    QString enginePath;                      // engine binary
    QString assetsDir;                       // --assets-dir
    QString workshopDir;                     // wallpaper library scan path (not an engine flag)
    QMap<QString, QString> screens;          // screen -> wallpaper ID (--screen-root/--bg)
    QString display = ":0";                  // DISPLAY for the engine systemd unit
    QString scaling = "fill";                // --scaling
    QString clamp = "border";                // --clamp
    int fps = 30;                            // --fps
    bool fullscreenPause = true;             // false emits --no-fullscreen-pause
    int volume = 15;                         // --volume
    bool silent = true;                      // --silent
    bool disableParticles = false;           // --disable-particles
    bool disableMouse = false;               // --disable-mouse
    bool disableParallax = false;            // --disable-parallax
    QVariantMap properties;                  // wallpaper ID -> {property: value} (--set-property)

    static QString configDir ();             // ~/.config/lwe-dynamic-wallpaper
    static QString configPath ();
    static Config load ();
    bool save () const;

    QJsonObject toJson () const;
    static Config fromJson (const QJsonObject& obj);
};

