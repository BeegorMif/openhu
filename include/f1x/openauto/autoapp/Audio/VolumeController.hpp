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

#include <QObject>
#include <QProcess>
#include <QTimer>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

// Master volume of the default output via pactl (PulseAudio and PipeWire-pulse).
// Changes made elsewhere (other apps, pavucontrol) are picked up through "pactl subscribe".
// Publishes "audio.volume" {volume, muted} on the EventBus.
class VolumeController: public QObject
{
    Q_OBJECT
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)

public:
    explicit VolumeController(QObject* parent = nullptr);
    ~VolumeController() override;

    int volume() const;
    bool muted() const;

    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void changeVolume(int delta);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void runPactl(const QStringList& arguments);
    void onSubscribeOutput();
    void publish();

    int volume_ = 0;
    bool muted_ = false;
    QProcess subscribe_;
    QTimer refreshTimer_;
};

}
}
}
}
