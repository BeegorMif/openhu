#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Audio/VolumeController.hpp>

using f1x::openauto::autoapp::audio::VolumeController;

namespace
{

// pactl replacement in front of PATH: the volume is a file, missing file = no audio server (yet)
const char* cFakePactl = R"(#!/bin/sh
dir=$(dirname "$0")
case "$1" in
    subscribe) exec sleep 60 ;;
    get-sink-volume) [ -f "$dir/volume" ] && echo "Volume: front-left: 26214 /  $(cat "$dir/volume")% / -23.88 dB" ;;
    get-sink-mute) [ -f "$dir/volume" ] && echo "Mute: no" ;;
    set-sink-volume) v="$3"; echo "${v%\%}" > "$dir/volume" ;;
esac
exit 0
)";

}

class TestVolume: public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(dir_.isValid());
        QFile script(dir_.filePath("pactl"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(cFakePactl);
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QFile::remove(dir_.filePath("volume"));
        oldPath_ = qgetenv("PATH");
        qputenv("PATH", dir_.path().toLocal8Bit() + ":" + oldPath_);
    }

    void cleanup() { qputenv("PATH", oldPath_); }

    // The audio server starts after OpenHU: learning the volume is no adjustment (no volume overlay at startup)
    void firstReadIsNoAdjustment()
    {
        VolumeController volume;
        QSignalSpy changed(&volume, &VolumeController::changed);
        QSignalSpy adjusted(&volume, &VolumeController::adjusted);
        QCOMPARE(volume.volume(), 0);

        setServerVolume(40);
        volume.refresh();
        QCOMPARE(volume.volume(), 40);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(adjusted.count(), 0);

        volume.setVolume(55);
        QCOMPARE(volume.volume(), 55);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(adjusted.count(), 1);
    }

    void unchangedVolumeEmitsNothing()
    {
        setServerVolume(40);
        VolumeController volume;
        QCOMPARE(volume.volume(), 40);
        QSignalSpy changed(&volume, &VolumeController::changed);
        QSignalSpy adjusted(&volume, &VolumeController::adjusted);

        volume.refresh();
        QCOMPARE(changed.count(), 0);
        QCOMPARE(adjusted.count(), 0);

        setServerVolume(70);  // changed by another app
        volume.refresh();
        QCOMPARE(volume.volume(), 70);
        QCOMPARE(adjusted.count(), 1);
    }

private:
    void setServerVolume(int value)
    {
        QFile file(dir_.filePath("volume"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::number(value));
    }

    QTemporaryDir dir_;
    QByteArray oldPath_;
};

QTEST_GUILESS_MAIN(TestVolume)
#include "test_volume.moc"
