#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ee::dsp
{

/** Pluck / string-attack detector, ported from Cycfi Q's onset_gate.

    Q (https://github.com/cycfi/q, Boost Software License 1.0) builds onset
    detection from three pieces: a peak-envelope follower, a slope taken over a
    fixed short window (its `differentiator.hpp` `slope`), and a two-threshold
    gate (`noise_gate` / `onset_gate`). Rather than pull in the whole library -
    Boost, an `infra` submodule and its unit-type machinery - the ~40 lines that
    matter are reproduced here in the house style, the same way chowdsp_wdf is
    vendored as headers.

    Differences from Q's `onset_gate`:

      * that class is built to gate a signal path, so it opens on level-or-slope
        and only closes once the level falls back to near silence. Here the gate
        is an edge trigger - it fires the wah's retrigger once per attack - so it
        re-arms on the rise falling back, not the level. Otherwise a still-ringing
        note would never re-arm and the next pluck would be missed, which is the
        exact case this detector exists to fix;

      * the rise is measured as a *fraction of the pre-attack level*, not an
        absolute delta. An absolute threshold is deaf to quiet notes - their
        envelope simply never climbs that far - and twitchy on loud ones. A pluck
        roughly doubles the envelope across its attack whatever the level, so
        "grew by riseRatioOn of where it started" tracks soft and hard playing
        alike. A small absolute floor (minRise) still has to be cleared so plain
        noise near silence can't ratio its way to a trigger;

      * a refractory time (lockoutMs). One pluck's attack is not a single clean
        ramp - a hard hit on a low string blooms and beats for tens of ms, and
        every up-swing that clears the threshold would fire again. After a trigger
        the gate cannot fire again until lockoutMs has passed, so a single note
        kicks the sweep exactly once. Set it under the fastest run you want to
        track (16ths at 120 BPM are 125 ms apart).

    Feed it a smoothed envelope - the gate follower, not the raw rectified signal,
    or a low note's own waveform ripples through the window. operator() returns
    true only on the sample where a new attack is detected.
*/
class OnsetGate
{
public:
    /** @param sps            sample rate
        @param envDecayMs      release of the internal peak follower
        @param attackWidthMs   window the envelope rise is measured over
        @param riseRatioOn     rise / pre-attack level that fires a trigger
        @param riseRatioOff    ratio falls back below this to re-arm (< riseRatioOn)
        @param minRise         absolute rise the window must also clear (noise guard)
        @param lockoutMs       minimum time between triggers */
    void prepare (float sps, float envDecayMs, float attackWidthMs,
                  float riseRatioOn, float riseRatioOff, float minRise,
                  float lockoutMs) noexcept
    {
        const float fs = sps > 0.0f ? sps : 44100.0f;

        envDecay = std::exp (-2.0f / (fs * std::max (1.0e-4f, envDecayMs * 0.001f)));

        int win = static_cast<int> (std::lround (fs * attackWidthMs * 0.001f));
        win = std::max (1, win);
        history.assign (static_cast<size_t> (win), 0.0f);

        onRatio  = riseRatioOn;
        offRatio = riseRatioOff;
        floorRise = minRise;
        lockoutSamples = std::max (0, static_cast<int> (std::lround (fs * lockoutMs * 0.001f)));

        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        std::fill (history.begin(), history.end(), 0.0f);
        writeIndex = 0;
        armed = true;
        sinceFire = lockoutSamples;
    }

    /** @param x  a smoothed envelope sample (e.g. the gate follower)
        @return   true on the sample a new attack is detected */
    bool operator() (float x) noexcept
    {
        // Peak follower: instant attack, exponential release (Q's formula).
        env = x > env ? x : x + envDecay * (env - x);

        // Rise across the attack window, and that rise relative to where the
        // envelope started - the ratio is what makes soft and loud notes read
        // the same.
        const float past = history[static_cast<size_t> (writeIndex)];
        history[static_cast<size_t> (writeIndex)] = env;
        if (++writeIndex >= static_cast<int> (history.size()))
            writeIndex = 0;
        const float rise  = env - past;
        const float ratio = rise / std::max (past, 1.0e-4f);

        if (sinceFire < lockoutSamples)
            ++sinceFire;

        bool fired = false;
        if (armed && rise > floorRise && ratio > onRatio)
        {
            armed = false;
            if (sinceFire >= lockoutSamples)
            {
                fired = true;
                sinceFire = 0;
            }
        }
        else if (! armed && ratio < offRatio)
        {
            armed = true;
        }
        return fired;
    }

    /** The internal peak-envelope value, for a scope or a test. */
    float envelope() const noexcept { return env; }

private:
    std::vector<float> history;
    int   writeIndex = 0;
    float env = 0.0f;
    float envDecay = 0.0f;
    float onRatio = 0.0f;
    float offRatio = 0.0f;
    float floorRise = 0.0f;
    int   lockoutSamples = 0;
    int   sinceFire = 0;
    bool  armed = true;
};

} // namespace ee::dsp
