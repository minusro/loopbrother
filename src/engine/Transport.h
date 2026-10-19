#pragma once
// Where the loops' timeline comes from, block by block. Plain C++17, no JUCE.
//
// - The host transport while it plays.
// - The pad or a MIDI note while held with the transport stopped: an
//   audition that starts from 1.1.1 when the hold begins and stops when it
//   ends. Holding through a transport stop carries on seamlessly instead.
// - A free run when there is no transport at all (the standalone).

namespace jm {

struct TimelineBlock {
    bool   playing;  // anything to play this block
    double beat;     // timeline beat at its first sample
    bool   resync;   // the host transport just took over: start on the song position
};

class TransportFollower {
public:
    TimelineBlock next(bool hasTransport, bool hostPlaying, double hostBeat, double bpm,
                       bool held, int samples, double sampleRate)
    {
        const double blockBeats = samples * bpm / 60.0 / sampleRate;
        TimelineBlock b { false, 0.0, false };
        if (! hasTransport)
        {
            b = { true, free_, false };
            free_ += blockBeats;
            mode_ = Free;
        }
        else if (hostPlaying)
        {
            b = { true, hostBeat, mode_ != Host };
            hostEnd_ = hostBeat + blockBeats;
            mode_ = Host;
        }
        else if (held)
        {
            if (mode_ != Audition) audition_ = mode_ == Host ? hostEnd_ : 0.0; // new hold: 1.1.1
            b = { true, audition_, false };
            audition_ += blockBeats;
            mode_ = Audition;
        }
        else mode_ = Stopped;
        return b;
    }

private:
    enum Mode { Stopped, Host, Audition, Free } mode_ = Stopped;
    double free_ = 0.0, audition_ = 0.0, hostEnd_ = 0.0;
};

} // namespace jm
