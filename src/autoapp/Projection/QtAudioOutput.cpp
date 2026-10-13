/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <QCoreApplication>
#include <QThread>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

namespace
{
// unplayed audio before a packet is written: normally what is left of the previous packet plus jitter
// (a packet is 42.7 ms for media; 40 ms dropped a packet in normal playback once)
constexpr qint64 cMaxQueuedUs = 60000;
}

QtAudioOutput::QtAudioOutput(uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate)
    : audioBuffer_(nullptr)
    , playbackStarted_(false)
{
    audioFormat_.setChannelCount(channelCount);
    audioFormat_.setSampleRate(sampleRate);
    // Android Auto sends and expects 16 bit PCM (sampleSize is always 16)
    audioFormat_.setSampleFormat(sampleSize == 16 ? QAudioFormat::Int16 : QAudioFormat::Int32);

    QThread * th = QCoreApplication::instance()->thread();
    this->moveToThread(th);
    th->setPriority(QThread::TimeCriticalPriority);

    connect(this, &QtAudioOutput::startPlayback, this, &QtAudioOutput::onStartPlayback);
    connect(this, &QtAudioOutput::suspendPlayback, this, &QtAudioOutput::onSuspendPlayback);
    connect(this, &QtAudioOutput::stopPlayback, this, &QtAudioOutput::onStopPlayback);

    QMetaObject::invokeMethod(this, "createAudioOutput", Qt::BlockingQueuedConnection);
}

void QtAudioOutput::createAudioOutput()
{
    OPENAUTO_LOG(info) << "[QtAudioOutput] createAudioOutput()";
    audioOutput_ = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), audioFormat_);
}

bool QtAudioOutput::open()
{
    return true;
}

void QtAudioOutput::write(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer)
{
    if (audioBuffer_ == nullptr)
    {
        return;
    }
    // After a transmission stall the phone sends the late packets at once; playing them all would keep that
    // delay for the rest of the stream (measured: 18 -> 210 ms after a 250 ms stall). With more unplayed audio
    // than the jitter of a normal stream the packet is dropped instead: one skip, then the normal latency again.
    const qint64 queued = audioOutput_->bufferSize() - audioOutput_->bytesFree();
    if (queued > audioFormat_.bytesForDuration(cMaxQueuedUs))
    {
        ++droppedPackets_;
        return;
    }
    audioBuffer_->write(reinterpret_cast<const char*>(buffer.cdata), buffer.size);
}

void QtAudioOutput::start()
{
    emit startPlayback();
}

void QtAudioOutput::stop()
{
    emit stopPlayback();
}

void QtAudioOutput::suspend()
{
    emit suspendPlayback();
}

uint32_t QtAudioOutput::getSampleSize() const
{
    return audioFormat_.bytesPerSample() * 8;
}

uint32_t QtAudioOutput::getChannelCount() const
{
    return audioFormat_.channelCount();
}

uint32_t QtAudioOutput::getSampleRate() const
{
    return audioFormat_.sampleRate();
}

void QtAudioOutput::onStartPlayback()
{
    if(!playbackStarted_)
    {
        audioBuffer_ = audioOutput_->start();
        playbackStarted_ = true;
    }
    else
    {
        audioOutput_->resume();
    }
}

void QtAudioOutput::onSuspendPlayback()
{
    audioOutput_->suspend();
}

void QtAudioOutput::onStopPlayback()
{
    if(droppedPackets_ > 0)
    {
        OPENAUTO_LOG(info) << "[QtAudioOutput] " << audioFormat_.sampleRate() << " Hz: " << droppedPackets_
                           << " late packets dropped";
        droppedPackets_ = 0;
    }
    if(playbackStarted_)
    {
        audioOutput_->stop();
        playbackStarted_ = false;
    }
}

}
}
}
}
