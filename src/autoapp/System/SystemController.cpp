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

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QTimeZone>
#include <QVersionNumber>
#include <f1x/openauto/autoapp/System/SystemController.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::system
{

namespace
{

QVariantMap readJson(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

// The helpers print one JSON object (the last line counts)
QVariantMap parseOutput(const QByteArray& out)
{
    const QList<QByteArray> lines = out.trimmed().split('\n');
    return lines.isEmpty() ? QVariantMap() : QJsonDocument::fromJson(lines.last()).object().toVariantMap();
}

}

SystemController::SystemController(const QString& osVersion, QObject* parent, const QString& pkexec,
                                   const QString& helperDir, const QString& runDir)
    : QObject(parent)
    , osVersion_(osVersion)
    , pkexec_(pkexec)
    , helperDir_(helperDir)
    , runDir_(runDir)
{
    if(!this->available())
    {
        return;
    }

    bootTimer_.setInterval(2000);
    connect(&bootTimer_, &QTimer::timeout, this, &SystemController::readBootStatus);
    this->readBootStatus();

    auto& bus = core::EventBus::instance();
    aaConnected_ = bus.last("projection.state").value("connected").toBool();
    connect(&bus, &core::EventBus::event, this, [this](const QString& topic, const QVariantMap& data) {
        if(topic == "projection.state" && data.value("connected").toBool() != aaConnected_)
        {
            aaConnected_ = data.value("connected").toBool();
            emit updateChanged();
        }
    });

    // an update keeps running when OpenHU restarts: show it
    progressTimer_.setInterval(500);
    connect(&progressTimer_, &QTimer::timeout, this, &SystemController::readUpdateState);
    this->readUpdateState();

    // a stick plugged in before OpenHU started counts too
    runWatcher_.addPath(runDir_);
    connect(&runWatcher_, &QFileSystemWatcher::directoryChanged, this, &SystemController::readUsbUpdate);
    this->readUsbUpdate();
    this->refreshSsh();
    this->refreshTime();
}

void SystemController::readBootStatus()
{
    const QVariantMap status = readJson(runDir_ + "/boot-status.json");
    if(status != bootStatus_)
    {
        bootStatus_ = status;
        emit bootStatusChanged();
    }
    // the boot check needs OpenHU's API, so it answers after OpenHU started (90 s at most)
    const bool trial = status.value("state").toString() == "trial";
    if(trial && ++bootReads_ < 60)
    {
        bootTimer_.start();
    }
    else
    {
        bootTimer_.stop();
    }
}

QVariantMap SystemController::offer() const
{
    QVariantMap offer;
    if(update_.value("checked").toBool() && update_.value("available").toBool())
    {
        offer = {{"version", update_.value("newest")}, {"source", update_.value("source")}};
    }
    const QVariantMap usb = update_.value("usb").toMap();
    if(usb.value("newer").toBool() && QVersionNumber::fromString(usb.value("version").toString())
                                          > QVersionNumber::fromString(offer.value("version").toString()))
    {
        offer = {{"version", usb.value("version")}, {"source", "usb"}};
    }
    return offer;
}

QVariantMap SystemController::update() const
{
    QVariantMap update = update_;
    update.insert("blocked", aaConnected_);
    const QVariantMap offer = this->offer();
    if(!offer.isEmpty())
    {
        update.insert("offer", offer);
    }
    if(!update.contains("state"))
    {
        update.insert("state", "idle");
    }
    return update;
}

QVariantMap SystemController::ssh() const
{
    QVariantMap ssh = ssh_;
    QStringList addresses;
    for(const QHostAddress& address: QNetworkInterface::allAddresses())
    {
        if(address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
        {
            addresses << address.toString();
        }
    }
    ssh.insert("addresses", addresses);
    return ssh;
}

bool SystemController::updateRunning() const
{
    const QString state = update_.value("state").toString();
    return state == "checking" || state == "installing";
}

void SystemController::checkForUpdate()
{
    if(!this->available() || update_.value("busy").toBool() || this->updateRunning())
    {
        return;
    }
    this->setUpdate({{"busy", true}, {"checkError", QVariant()}});
    this->run("openhu-update", {"check"}, {}, [this](const QVariantMap& result) {
        if(result.contains("error"))
        {
            this->setUpdate({{"busy", false}, {"checked", true}, {"checkError", result.value("error")}});
            return;
        }
        this->setUpdate({{"busy", false}, {"checked", true}, {"current", result.value("current")},
                         {"newest", result.value("newest")}, {"available", result.value("available")},
                         {"source", result.value("source")}});
    });
}

QString SystemController::installUpdate()
{
    if(!this->available())
    {
        return qtTrId("system.only_openhu_os");
    }
    if(aaConnected_)
    {
        return qtTrId("system.not_android_auto_connected");
    }
    if(this->updateRunning() || update_.value("busy").toBool())
    {
        return qtTrId("system.update_already_running");
    }

    QStringList args{"start"};
    const QString newest = this->offer().value("version").toString();
    if(!newest.isEmpty())
    {
        args << "--version" << newest;
    }
    OPENAUTO_LOG(info) << "[SystemController] starting the system update " << newest.toStdString();
    this->setUpdate({{"busy", true}, {"state", "checking"}, {"error", QVariant()}, {"percent", 0}});
    this->run("openhu-update", args, {}, [this](const QVariantMap& result) {
        this->setUpdate({{"busy", false}});
        if(result.contains("error"))
        {
            this->setUpdate({{"state", "error"}, {"error", result.value("error")}});
            return;
        }
        progressTimer_.start();
    });
    return {};
}

void SystemController::cancelUpdate()
{
    if(!this->available())
    {
        return;
    }
    this->run("openhu-update", {"cancel"}, {}, [this](const QVariantMap&) { this->readUpdateState(); });
}

void SystemController::readUpdateState()
{
    QVariantMap state = readJson(runDir_ + "/update.json");
    if(state.isEmpty())
    {
        progressTimer_.stop();
        return;
    }
    // from the previous update: only "phase", "percent" and "error" of the running one count
    QVariantMap values{{"phase", QVariant()}, {"percent", QVariant()}, {"error", QVariant()}};
    for(auto it = state.cbegin(); it != state.cend(); ++it)
    {
        values.insert(it.key(), it.value());
    }
    this->setUpdate(values);

    const QString current = state.value("state").toString();
    if(current == "checking" || current == "installing")
    {
        if(!progressTimer_.isActive())
        {
            progressTimer_.start();
        }
    }
    else
    {
        progressTimer_.stop();
    }
}

void SystemController::readUsbUpdate()
{
    const QVariantMap state = readJson(runDir_ + "/usb-update.json");
    const QVariantMap usb = state.value("version").toString().isEmpty() ? QVariantMap() : QVariantMap{
        {"version", state.value("version")}, {"newer", state.value("newer")}, {"path", state.value("path")}};
    const QVariantMap before = update_.value("usb").toMap();
    // the first read (stick plugged in before OpenHU started) without a toast
    const bool first = !usbRead_;
    usbRead_ = true;
    if(usb == before)
    {
        return;
    }
    this->setUpdate({{"usb", usb.isEmpty() ? QVariant() : QVariant(usb)}});
    // the checked version was on the stick that is gone: search again
    if(usb.isEmpty() && update_.value("source").toString() == "usb" && !this->updateRunning())
    {
        this->setUpdate({{"checked", QVariant()}, {"available", QVariant()}, {"newest", QVariant()}, {"source", QVariant()}});
    }
    if(!first && usb.value("newer").toBool() && usb.value("version") != before.value("version"))
    {
        // a new stick starts over: the failed or cancelled update before is no longer shown
        const QString state = update_.value("state").toString();
        if(state == "error" || state == "cancelled" || state == "uptodate")
        {
            this->setUpdate({{"state", "idle"}, {"error", QVariant()}});
        }
        OPENAUTO_LOG(info) << "[SystemController] update on a USB stick: " << usb.value("version").toString().toStdString();
        emit usbUpdateFound(usb.value("version").toString());
    }
}

void SystemController::setUpdate(const QVariantMap& values)
{
    bool changed = false;
    for(auto it = values.cbegin(); it != values.cend(); ++it)
    {
        if(update_.value(it.key()) != it.value())
        {
            if(it.value().isNull())
            {
                update_.remove(it.key());
            }
            else
            {
                update_.insert(it.key(), it.value());
            }
            changed = true;
        }
    }
    if(changed)
    {
        emit updateChanged();
        core::EventBus::instance().publish("system.update", this->update());
    }
}

void SystemController::refreshSsh()
{
    if(!this->available())
    {
        return;
    }
    this->run("openhu-ssh", {"status"}, {}, [this](const QVariantMap& result) {
        if(result.contains("error"))
        {
            ssh_ = {{"busy", false}, {"error", result.value("error")}};
            emit sshChanged();
            return;
        }
        this->setSsh(result);
    });
}

void SystemController::setSshEnabled(bool enabled)
{
    if(!this->available())
    {
        return;
    }
    OPENAUTO_LOG(info) << "[SystemController] ssh " << (enabled ? "on" : "off");
    ssh_.insert("busy", true);
    emit sshChanged();
    this->run("openhu-ssh", {enabled ? "enable" : "disable"}, {}, [this](const QVariantMap& result) {
        this->setSsh(result);
    });
}

QString SystemController::setSshPassword(const QString& password)
{
    if(!this->available())
    {
        return qtTrId("system.only_openhu_os");
    }
    if(password.size() < cMinPasswordLength)
    {
        return qtTrId("system.least_characters").arg(cMinPasswordLength);
    }
    if(password.contains('\n'))
    {
        return qtTrId("system.no_line_break_password");
    }
    ssh_.insert("busy", true);
    emit sshChanged();
    this->run("openhu-ssh", {"password"}, password.toUtf8() + '\n', [this](const QVariantMap& result) {
        this->setSsh(result);
    });
    return {};
}

void SystemController::setSsh(const QVariantMap& values)
{
    if(values.contains("error"))
    {
        emit sshError(values.value("error").toString());
        this->refreshSsh();  // the switch shows what really is
        return;
    }
    ssh_ = values;
    ssh_.insert("busy", false);
    emit sshChanged();
    core::EventBus::instance().publish("system.ssh", this->ssh());
}

void SystemController::refreshTime()
{
    if(!this->available())
    {
        return;
    }
    this->run("openhu-time", {"status"}, {}, [this](const QVariantMap& result) {
        if(result.contains("error"))
        {
            time_ = {{"busy", false}, {"error", result.value("error")}};
            emit timeChanged();
            return;
        }
        this->setTimeState(result);
    });
}

void SystemController::setTimeZone(const QString& zone)
{
    if(!this->available() || zone == time_.value("zone").toString())
    {
        return;
    }
    OPENAUTO_LOG(info) << "[SystemController] time zone " << zone.toStdString();
    time_.insert("busy", true);
    emit timeChanged();
    this->run("openhu-time", {"zone", zone}, {}, [this](const QVariantMap& result) { this->setTimeState(result); });
}

void SystemController::setNtp(bool enabled)
{
    if(!this->available())
    {
        return;
    }
    OPENAUTO_LOG(info) << "[SystemController] time from the internet " << (enabled ? "on" : "off");
    time_.insert("busy", true);
    emit timeChanged();
    this->run("openhu-time", {"ntp", enabled ? "on" : "off"}, {}, [this](const QVariantMap& result) {
        this->setTimeState(result);
    });
}

QString SystemController::setTime(qint64 seconds)
{
    if(!this->available())
    {
        return qtTrId("system.only_openhu_os");
    }
    if(time_.value("ntp").toBool())
    {
        return qtTrId("system.switch_time_internet_off");
    }
    OPENAUTO_LOG(info) << "[SystemController] setting the time to "
                       << QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC).toString(Qt::ISODate).toStdString();
    time_.insert("busy", true);
    emit timeChanged();
    this->run("openhu-time", {"set", QString::number(seconds)}, {}, [this](const QVariantMap& result) {
        this->setTimeState(result);
    });
    return {};
}

QStringList SystemController::timeZones()
{
    // Region/City; not Etc/GMT+1 & co. (inverted signs) and the old names without a region
    QStringList zones;
    for(const QByteArray& id: QTimeZone::availableTimeZoneIds())
    {
        if(id.contains('/') && !id.startsWith("Etc/"))
        {
            zones << QString::fromLatin1(id);
        }
    }
    zones.sort();
    zones << QStringLiteral("UTC");
    return zones;
}

void SystemController::setTimeState(const QVariantMap& values)
{
    if(values.contains("error"))
    {
        emit timeError(values.value("error").toString());
        this->refreshTime();  // the controls show what really is
        return;
    }
    time_ = values;
    time_.insert("busy", false);
    const QTimeZone zone(values.value("zone").toByteArray());
    time_.insert("offset", zone.isValid() ? zone.offsetFromUtc(QDateTime::currentDateTimeUtc()) : 0);
    emit timeChanged();
    core::EventBus::instance().publish("system.time", time_);
}

void SystemController::run(const QString& helper, const QStringList& args, const QByteArray& input,
                           std::function<void(const QVariantMap&)> done)
{
    auto* process = new QProcess(this);
    QStringList arguments{helperDir_ + "/" + helper};
    arguments << args;
    if(helper == "openhu-update")
    {
        arguments << "--json";  // openhu-ssh always answers in JSON
    }
    connect(process, &QProcess::finished, this, [process, done, helper](int exitCode, QProcess::ExitStatus) {
        QVariantMap result = parseOutput(process->readAllStandardOutput());
        if(result.isEmpty() && exitCode != 0)
        {
            const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
            result.insert("error", error.isEmpty() ? qtTrId("system.error").arg(helper).arg(exitCode) : error);
        }
        if(result.contains("error"))
        {
            OPENAUTO_LOG(warning) << "[SystemController] " << helper.toStdString() << ": "
                                  << result.value("error").toString().toStdString();
        }
        process->deleteLater();
        done(result);
    });
    connect(process, &QProcess::errorOccurred, this, [process, done, helper](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            process->deleteLater();
            done({{"error", qtTrId("system.could_not_started").arg(helper)}});
        }
    });
    process->start(pkexec_, arguments);
    if(!input.isEmpty())
    {
        process->write(input);
    }
    process->closeWriteChannel();
}

}
