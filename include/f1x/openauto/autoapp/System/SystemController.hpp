/*
*  This file is part of openauto project.
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <functional>
#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QVariantMap>

namespace f1x::openauto::autoapp::system
{

// OpenHU OS: system update, ssh access, time zone and time (Settings -> System, system.* API) through the command
// line tool of OpenHU OS, answering in JSON: openhu update check/install/cancel (progress in <runDir>/update.json),
// openhu ssh status/on/off/password, openhu time status/zone/ntp/set. It runs these as root itself (pkexec, its
// polkit rule allows the openhu user exactly these commands).
// Elsewhere (Docker, .deb on another system) available is false and nothing runs.
class SystemController: public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)
    // boot-status.json of this boot: state normal/trial/updated/rollback, running, failed. "trial" turns into
    // "updated" once openhu-boot-check found OpenHU running, re-read until then.
    Q_PROPERTY(QVariantMap bootStatus READ bootStatus NOTIFY bootStatusChanged)
    // state (idle/checking/installing/done/uptodate/error/cancelled), phase, percent, from, version, error,
    // source (usb/online) of the running update; newest/available/source after a check, busy while the helper runs,
    // blocked while Android Auto is connected; usb: the newest version on a plugged in USB stick {version, newer, path};
    // offer: what installUpdate installs {version, source}, the newer of the checked one and the stick's
    Q_PROPERTY(QVariantMap update READ update NOTIFY updateChanged)
    // enabled, running, password, keys, busy, addresses (IPv4 of this device); error when the state is unknown
    Q_PROPERTY(QVariantMap ssh READ ssh NOTIFY sshChanged)
    // zone ("Europe/Berlin"), offset (seconds east of UTC, now), ntp (time from the internet), synchronized, busy;
    // error when the state is unknown
    Q_PROPERTY(QVariantMap time READ time NOTIFY timeChanged)

public:
    // osVersion: IMAGE_VERSION of OpenHU OS, empty elsewhere. The rest only changes for tests.
    explicit SystemController(const QString& osVersion, QObject* parent = nullptr,
                              const QString& cli = QStringLiteral("/usr/bin/openhu"),
                              const QString& runDir = QStringLiteral("/run/openhu-os"));

    bool available() const { return !osVersion_.isEmpty(); }
    QString osVersion() const { return osVersion_; }
    QVariantMap bootStatus() const { return bootStatus_; }
    QVariantMap update() const;
    QVariantMap ssh() const;
    QVariantMap time() const { return time_; }

    Q_INVOKABLE void checkForUpdate();
    // the newest of the checked version and the one on a USB stick;
    // empty when started, otherwise why not (Android Auto connected, nothing to install, ...)
    Q_INVOKABLE QString installUpdate();
    Q_INVOKABLE void cancelUpdate();

    Q_INVOKABLE void refreshSsh();
    Q_INVOKABLE void setSshEnabled(bool enabled);
    // checks the length here, the helper hashes it; empty when accepted
    Q_INVOKABLE QString setSshPassword(const QString& password);

    static constexpr int cMinPasswordLength = 8;

    Q_INVOKABLE void refreshTime();
    // applies at once, also to the clock of the shell
    Q_INVOKABLE void setTimeZone(const QString& zone);
    Q_INVOKABLE void setNtp(bool enabled);
    // seconds since 1970 (UTC), only while the time from the internet is off; empty when accepted
    Q_INVOKABLE QString setTime(qint64 seconds);
    // the zones to choose from: Region/City ("Europe/Berlin", "America/Argentina/Salta") and UTC
    Q_INVOKABLE static QStringList timeZones();

signals:
    void bootStatusChanged();
    void updateChanged();
    // a USB stick with a newer version was plugged in (shown as a toast)
    void usbUpdateFound(const QString& version);
    void sshChanged();
    // a switch or password change was refused (shown as a toast)
    void sshError(const QString& message);
    void timeChanged();
    // a time zone, time or switch was refused (shown as a toast)
    void timeError(const QString& message);

private:
    // openhu <command> <args> --json
    void run(const QString& command, const QStringList& args, const QByteArray& input,
             std::function<void(const QVariantMap&)> done);
    void readUpdateState();
    void readUsbUpdate();
    QVariantMap offer() const;
    void readBootStatus();
    void setUpdate(const QVariantMap& values);
    void setSsh(const QVariantMap& values);
    void setTimeState(const QVariantMap& values);
    bool updateRunning() const;

    QString osVersion_;
    QString cli_;
    QString runDir_;
    QVariantMap bootStatus_;
    QVariantMap update_;
    QVariantMap ssh_;
    QVariantMap time_;
    bool aaConnected_ = false;
    QTimer progressTimer_;
    QTimer bootTimer_;
    // <runDir>/usb-update.json, written by openhu-update scan when a stick comes or goes
    QFileSystemWatcher runWatcher_;
    bool usbRead_ = false;
    int bootReads_ = 0;
};

}
