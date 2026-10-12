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
#include <QTimer>
#include <QVariantMap>

namespace f1x::openauto::autoapp::system
{

// OpenHU OS: system update and ssh access (Settings -> System, system.* API). The helpers of OpenHU OS run as root
// through pkexec (its polkit rule allows the openhu user exactly these programs) and answer in JSON:
// openhu-update (status/check/start/cancel, progress in <runDir>/update.json) and openhu-ssh.
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
    // newest/available after a check, busy while the helper runs, blocked while Android Auto is connected
    Q_PROPERTY(QVariantMap update READ update NOTIFY updateChanged)
    // enabled, running, password, keys, busy, addresses (IPv4 of this device); error when the state is unknown
    Q_PROPERTY(QVariantMap ssh READ ssh NOTIFY sshChanged)

public:
    // osVersion: IMAGE_VERSION of OpenHU OS, empty elsewhere. The rest only changes for tests.
    explicit SystemController(const QString& osVersion, QObject* parent = nullptr,
                              const QString& pkexec = QStringLiteral("pkexec"),
                              const QString& helperDir = QStringLiteral("/usr/libexec/openhu-os"),
                              const QString& runDir = QStringLiteral("/run/openhu-os"));

    bool available() const { return !osVersion_.isEmpty(); }
    QString osVersion() const { return osVersion_; }
    QVariantMap bootStatus() const { return bootStatus_; }
    QVariantMap update() const;
    QVariantMap ssh() const;

    Q_INVOKABLE void checkForUpdate();
    // empty when started, otherwise why not (Android Auto connected, nothing to install, ...)
    Q_INVOKABLE QString installUpdate();
    Q_INVOKABLE void cancelUpdate();

    Q_INVOKABLE void refreshSsh();
    Q_INVOKABLE void setSshEnabled(bool enabled);
    // checks the length here, the helper hashes it; empty when accepted
    Q_INVOKABLE QString setSshPassword(const QString& password);

    static constexpr int cMinPasswordLength = 8;

signals:
    void bootStatusChanged();
    void updateChanged();
    void sshChanged();
    // a switch or password change was refused (shown as a toast)
    void sshError(const QString& message);

private:
    void run(const QString& helper, const QStringList& args, const QByteArray& input,
             std::function<void(const QVariantMap&)> done);
    void readUpdateState();
    void readBootStatus();
    void setUpdate(const QVariantMap& values);
    void setSsh(const QVariantMap& values);
    bool updateRunning() const;

    QString osVersion_;
    QString pkexec_;
    QString helperDir_;
    QString runDir_;
    QVariantMap bootStatus_;
    QVariantMap update_;
    QVariantMap ssh_;
    bool aaConnected_ = false;
    QTimer progressTimer_;
    QTimer bootTimer_;
    int bootReads_ = 0;
};

}
