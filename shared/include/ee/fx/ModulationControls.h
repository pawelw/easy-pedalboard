#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ee/dsp/AutoWahConfig.h"
#include "ee/dsp/RateMap.h"
#include "ee/dsp/Tremolo.h"
#include "ee/dsp/TremoloConfig.h"
#include "ee/fx/ModulationModule.h"

#include <cmath>
#include <iterator>

namespace ee::fx::modulation
{

/** What the Modulation module's knobs mean, for every plugin that carries one -
 * BitBit Alpine's Mod module and BitBit Modulation.
 *
 * ModulationModule takes real units: a period in seconds, a 0..1 wave morph, a
 * transport. How a knob position becomes one of those is the owner's business,
 * and there are two owners, so the maps live here rather than in either of them.
 * The same knob position has to mean the same rate in both pedals, and two
 * copies kept "in step by hand" are two copies that drift.
 */

// -------------------------------------------------------------------- tremolo

/** The Tremolo Rate knob's map, shared with BitBit Trem & Pan through the
    tremolo's own voicing header so the same knob position means the same rate
    on all three. */
inline constexpr ee::dsp::RateMap kTremRateMap { ee::dsp::tremolo::kRateMinPeriodMs,
                                                 ee::dsp::tremolo::kRateMaxPeriodMs,
                                                 ee::dsp::tremolo::kRateSkewCentreMs };

// --------------------------------------------------------------------- filter

/** The Filter Time knob: one LFO cycle from 30 ms (knob down) to 3 s (knob up)
    - a filter wobble, the same travel BitBit Wah's Time knob has, its
    {30, 3000, 450} kept in step by hand with BitBit Wah's own copy. Knob down is
    the shortest period, so every entry below flips the position before handing
    it to the shared map. */
inline constexpr ee::dsp::RateMap kFilterRateMap { 30.0f, 3000.0f, 450.0f };

inline float filterInvert (float rate01) noexcept
{
    return 1.0f - juce::jlimit (0.0f, 1.0f, rate01);
}

inline float filterRateToPeriodSeconds (float rate01, bool synced, double bpm) noexcept
{
    return kFilterRateMap.rateToPeriodSeconds (filterInvert (rate01), synced, bpm);
}

inline float filterSyncedDivisionBeats (float rate01) noexcept
{
    return ee::dsp::RateMap::syncedDivisionBeats (filterInvert (rate01));
}

inline juce::String filterRateToText (float rate01, bool synced)
{
    return kFilterRateMap.rateToText (filterInvert (rate01), synced);
}

/** The Filter's Freq knob (0..100) -> Hz, log spaced with the low end given more
    travel (kFreqKnobSkew < 1). */
inline float filterFreqHzFor (float pct) noexcept
{
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, pct * 0.01f), ee::dsp::autowah::kFreqKnobSkew);
    return ee::dsp::autowah::kFreqMinHz * std::pow (ee::dsp::autowah::kFreqMaxHz / ee::dsp::autowah::kFreqMinHz, t);
}

/** The <> wave picker's three positions as ee::dsp::lfoValue shape morphs -
    triangle mid-morph, ramp a quarter down, hard square at the top. In step
    with the face's FILTER_WAVES table (packages/module-face/src/engines.jsx),
    which draws its glyph from the same numbers. */
inline constexpr float kFilterWaveShape01[] = { 0.50f, 0.25f, 1.00f };

inline float filterWaveShape01 (int waveIndex) noexcept
{
    const int last = static_cast<int> (std::size (kFilterWaveShape01)) - 1;
    return kFilterWaveShape01[juce::jlimit (0, last, waveIndex)];
}

// ----------------------------------------------------------------- host sync

/** Keeps the module's two tempo-locked LFOs on the host grid.
 *
 * Both free-run. When a Sync switch is on and the transport is rolling, the
 * Filter LFO gets a hard snap onto the grid on the first playing block or after
 * a jump, and a gentle per-block pull otherwise - the same shape BitBit Wah uses.
 * The Tremolo is handed a Transport and does its own alignment, exactly as BitBit
 * Trem & Pan drives it.
 *
 * Call `process` once per block, before the module processes it, and only from
 * the audio callback: it reads the playhead, which JUCE documents as valid only
 * there (and Ableton's really is not, outside it).
 */
class HostSync
{
public:
    void reset() noexcept
    {
        filterHaveExpectedPpq = false;
        filterWasPlaying = false;
    }

    void process (ModulationModule& module, juce::AudioPlayHead* playHead,
                  float filterTime01, bool filterSynced,
                  float tremRate01, bool tremSynced,
                  double bpm, double sampleRate, int numSamples) noexcept
    {
        double ppqStart = 0.0;
        bool havePpq = false;
        bool isPlaying = false;

        if (playHead != nullptr)
            if (const auto position = playHead->getPosition())
            {
                if (const auto ppq = position->getPpqPosition())
                {
                    ppqStart = *ppq;
                    havePpq = std::isfinite (ppqStart);
                }
                isPlaying = position->getIsPlaying();
            }

        const double ppqPerSample = bpm / (60.0 * sampleRate);

        if (filterSynced && havePpq && isPlaying)
        {
            const double cyclesPerQuarter =
                1.0 / juce::jmax (1.0e-4, static_cast<double> (filterSyncedDivisionBeats (filterTime01)));

            const double target = ppqStart * cyclesPerQuarter;
            const bool jumped =
                ! filterWasPlaying || (filterHaveExpectedPpq && std::abs (ppqStart - filterExpectedPpq) > 0.25);

            if (jumped)
                module.snapFilterPhase (target);
            else
                module.nudgeFilterPhase (target);

            filterExpectedPpq = ppqStart + numSamples * ppqPerSample;
            filterHaveExpectedPpq = true;
        }
        else
        {
            filterHaveExpectedPpq = false;
        }
        filterWasPlaying = isPlaying;

        // `synced` for the engine is the switch AND a finite ppq AND playing.
        ee::dsp::Tremolo::Transport transport;
        transport.synced = tremSynced && havePpq && isPlaying;
        transport.playing = isPlaying;
        transport.ppqStart = ppqStart;
        transport.cyclesPerQuarter =
            1.0 / juce::jmax (1.0e-4, static_cast<double> (ee::dsp::RateMap::syncedDivisionBeats (tremRate01)));
        transport.ppqPerSample = ppqPerSample;
        module.setTremoloTransport (transport);
    }

private:
    double filterExpectedPpq = 0.0;
    bool filterHaveExpectedPpq = false;
    bool filterWasPlaying = false;
};

} // namespace ee::fx::modulation
