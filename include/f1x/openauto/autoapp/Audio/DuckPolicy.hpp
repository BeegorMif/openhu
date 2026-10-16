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

#include <algorithm>
#include <QString>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

// Audio focus request types of the Android Auto protocol (AudioFocusRequestType, same numbers)
enum class FocusRequest
{
    None = 0,
    Gain = 1,
    GainTransient = 2,
    GainTransientMayDuck = 3,
    Release = 4
};

inline FocusRequest focusRequestFromProtocol(int type)
{
    return type >= 1 && type <= 4 ? FocusRequest(type) : FocusRequest::None;
}

inline const char* focusRequestName(FocusRequest request)
{
    switch(request)
    {
        case FocusRequest::Gain: return "AUDIO_FOCUS_GAIN";
        case FocusRequest::GainTransient: return "AUDIO_FOCUS_GAIN_TRANSIENT";
        case FocusRequest::GainTransientMayDuck: return "AUDIO_FOCUS_GAIN_TRANSIENT_MAY_DUCK";
        case FocusRequest::Release: return "AUDIO_FOCUS_RELEASE";
        case FocusRequest::None: break;
    }
    return "none";
}

// What local sources (radio, other processes' streams) should sound like
struct DuckTarget
{
    bool active = false;
    // loudness of the local sources in % (linear amplitude like Android's ducking, 100 = unchanged, 0 = silent)
    int level = 100;
    // "transient" (muted: Assistant, voice), "may_duck" (navigation prompts), "guidance" (no focus request that says
    // which), empty while inactive
    QString reason;

    bool operator==(const DuckTarget& other) const
    {
        return active == other.active && level == other.level && reason == other.reason;
    }
    bool operator!=(const DuckTarget& other) const { return !(*this == other); }
};

// Ducking of local sources while Android Auto plays on its guidance channel (#93), like Android does it for other
// apps: transient focus (the Assistant) silences them, "may duck" (navigation prompts) lowers them to the configured
// level (Android: 20 %).
// The guidance channel decides when (MediaChannelStartIndication .. StopIndication), the last audio focus request of
// the phone decides how much. Which request the Assistant and navigation send is not verified yet (expected:
// Assistant GAIN_TRANSIENT, navigation GAIN_TRANSIENT_MAY_DUCK); everything except GAIN_TRANSIENT lowers to the
// configured level, so an unexpected type never silences a prompt's background completely.
class DuckPolicy
{
public:
    explicit DuckPolicy(int duckLevel = 20)
    {
        this->setDuckLevel(duckLevel);
    }

    // % for "may duck" (Settings -> Audio); takes effect at once while ducked
    void setDuckLevel(int level)
    {
        duckLevel_ = std::clamp(level, 0, 100);
    }

    int duckLevel() const
    {
        return duckLevel_;
    }

    void focusRequested(FocusRequest request)
    {
        if(request == FocusRequest::Release)
        {
            // The release can arrive before the stop indication: keep the level until the prompt ended, the
            // background must not come up to the duck level for the last moment of the Assistant's answer.
            if(guidance_)
            {
                releasePending_ = true;
            }
            else
            {
                focus_ = FocusRequest::None;
            }
            return;
        }
        focus_ = request;
        releasePending_ = false;
    }

    void guidanceStarted()
    {
        guidance_ = true;
    }

    void guidanceStopped()
    {
        guidance_ = false;
        if(releasePending_)
        {
            focus_ = FocusRequest::None;
            releasePending_ = false;
        }
    }

    // Disconnect: nothing of the session counts any more
    void sessionEnded()
    {
        guidance_ = false;
        focus_ = FocusRequest::None;
        releasePending_ = false;
    }

    bool guidance() const
    {
        return guidance_;
    }

    FocusRequest focus() const
    {
        return focus_;
    }

    DuckTarget target() const
    {
        if(!guidance_)
        {
            return DuckTarget();
        }
        if(focus_ == FocusRequest::GainTransient)
        {
            return DuckTarget{true, 0, QStringLiteral("transient")};
        }
        return DuckTarget{true, duckLevel_,
                          focus_ == FocusRequest::GainTransientMayDuck ? QStringLiteral("may_duck")
                                                                       : QStringLiteral("guidance")};
    }

private:
    int duckLevel_ = 20;
    bool guidance_ = false;
    FocusRequest focus_ = FocusRequest::None;
    bool releasePending_ = false;
};

}
}
}
}
