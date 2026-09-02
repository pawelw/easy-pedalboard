#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "GrainerConfig.h"
#include "GrainerTuning.h"
#include "OnsetGate.h"

namespace ee::dsp
{

/** Granular scatterer: stereo in, stereo out, wet only.

    Input is summed to mono and recorded into a circular buffer. On a jittered
    timer a grain is spawned - a Hann-windowed voice that reads that buffer from
    a random point in the past, at a random rate (which is its pitch), in a
    random direction, placed at a random pan position. Many overlap, and the sum
    is a cloud of fragments of what was just played.

    The signal path is strictly feed-forward, so unlike a reverb it cannot latch
    a NaN: a non-finite input is zeroed on the way into the buffer and nothing
    downstream of that can produce one.

    Everything about the character that is not on a knob - the interval tables,
    the spawn jitter, the output trim - is in GrainerTuning.h, and can be driven
    live by the development panel.
*/
class Grainer
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        size = static_cast<int> (sampleRate * config::kGrainBufferSeconds) + 4;
        buffer.assign (static_cast<size_t> (size), 0.0f);

        minGrainSamples = static_cast<int> (sampleRate * config::kMinGrainSeconds);
        maxGrainSamples = static_cast<int> (sampleRate * config::kMaxGrainSeconds);

        // The attack detector runs off a smoothed envelope of the input, so it
        // needs a follower of its own - the raw signal's own ripple would read
        // as an attack on every cycle of a low note.
        const float fs = static_cast<float> (sampleRate);
        followerCoeff = std::exp (-1.0f / (fs * kFollowerSeconds));
        onsetGate.prepare (fs, kOnsetEnvDecayMs, kOnsetAttackWidthMs, kOnsetRiseRatioOn, kOnsetRiseRatioOff,
                           kOnsetMinRise, kOnsetLockoutMs);

        // Touch the window table here rather than letting the first grain pay
        // for building it on the audio thread.
        windowTable();

        updateDerived();
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;

        for (auto& g : grains)
            g.active = false;

        spawnCountdown = 1;
        rngState = kRngSeed;
        smoothedNorm = normTarget;

        onsetGate.reset();
        follower = 0.0f;
        attackIndex = -1;
        sinceAttack = 0;
        panLeft = false;
    }

    //==========================================================================
    // The four knobs. All are latched by the next grain to spawn; none of them
    // disturbs a grain already in flight, so none of them can click.

    /** Grain length in milliseconds. */
    void setSizeMs (float ms) noexcept
    {
        sizeMs = std::clamp (ms, config::kMinGrainMs, config::kMaxGrainMs);
        updateDerived();
    }

    /** Grains spawned per second. */
    void setDensityHz (float hz) noexcept
    {
        densityHz = std::clamp (hz, config::kMinDensityHz, config::kMaxDensityHz);
        updateDerived();
    }

    /** How long the cloud lasts, in milliseconds. Sets how far back a grain may
        be drawn from and how far its level falls off with how old its source
        is, which are the same thing heard from either end. */
    void setDecayMs (float ms) noexcept
    {
        decayMs = std::clamp (ms, config::kMinDecayMs, config::kMaxDecayMs);
    }

    /** Share of grains that play backwards, 0 to 1. */
    void setReverse (float amount01) noexcept
    {
        reverse = std::clamp (amount01, 0.0f, 1.0f);
    }

    /** Width of the random pan placement, 0 (centred) to 1 (hard left/right). */
    void setStereo (float amount01) noexcept
    {
        stereo = std::clamp (amount01, 0.0f, 1.0f);
    }

    /** Random detune on every grain, in cents either way. */
    void setDetuneCents (float cents) noexcept
    {
        detuneCents = std::clamp (cents, config::kMinDetuneCents, config::kMaxDetuneCents);
    }

    /** Relative weight of the three pitch groups. Each grain picks one of them
        in proportion to these, then an interval from that group's table; the
        unison group is always exactly 0 semitones. Need not sum to anything -
        only the ratio matters - and all three at zero is taken as unison, so a
        face with no pitch dialled in still makes a sound. */
    void setPitchMix (float low, float unison, float high) noexcept
    {
        pitchLow = std::max (0.0f, low);
        pitchUnison = std::max (0.0f, unison);
        pitchHigh = std::max (0.0f, high);
    }

    /** The rest of the voicing - everything the face does not carry. Safe to
        call while playing: each field is only read when a grain spawns, so the
        worst a change mid-block can do is land on the next grain instead of
        this one. The development tuning panel drives it live. */
    const GrainerTuning& getTuning() const noexcept { return tuning; }

    void setTuning (const GrainerTuning& newTuning) noexcept
    {
        tuning = newTuning;
        updateDerived();
    }

    //==========================================================================

    /** Writes the wet grain cloud to outL/outR. The caller keeps its own dry.
        In and out may alias: every input sample is read before its output slot
        is written. `inR` may be null for a mono source. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
    {
        if (size <= 0 || outL == nullptr || outR == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = inL != nullptr ? inL[i] : 0.0f;
            const float r = inR != nullptr ? inR[i] : l;
            const float mono = 0.5f * (l + r);

            const float sample = std::isfinite (mono) ? mono : 0.0f;
            buffer[static_cast<size_t> (writeIndex)] = sample;

            // Mark where the attack landed, so grains can be drawn from it
            // rather than from wherever the window happens to reach.
            const float rectified = std::abs (sample);
            follower = rectified > follower ? rectified : rectified + followerCoeff * (follower - rectified);

            if (onsetGate (follower))
            {
                attackIndex = writeIndex;
                sinceAttack = 0;
            }
            else if (attackIndex >= 0 && sinceAttack < size)
            {
                ++sinceAttack;
            }

            if (++writeIndex >= size)
                writeIndex = 0;

            if (--spawnCountdown <= 0)
            {
                spawnGrain();
                spawnCountdown = nextInterval();
            }

            float sumL = 0.0f;
            float sumR = 0.0f;

            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                const float windowed = read (g.position) * windowAt (g.windowPhase);

                sumL += windowed * g.gainL;
                sumR += windowed * g.gainR;

                g.position += g.rate;
                if (g.position >= static_cast<double> (size))
                    g.position -= static_cast<double> (size);
                else if (g.position < 0.0)
                    g.position += static_cast<double> (size);

                g.windowPhase += g.windowInc;

                if (++g.age >= g.length)
                    g.active = false;
            }

            // The normaliser tracks the overlap, which moves whenever Size or
            // Density does. Smoothed so those knobs do not step the level.
            smoothedNorm += (normTarget - smoothedNorm) * kNormSmoothing;

            outL[i] = sumL * smoothedNorm;
            outR[i] = sumR * smoothedNorm;
        }
    }

    /** How long the cloud keeps going after the input stops.

        Spray alone undercounts it. A backwards grain starts one Spray back and
        then walks further back still, by as much source as it spans - which at
        the top of the interval table is several times its own length - and only
        then does it have to play out. */
    float getTailSeconds() const noexcept
    {
        return (decayMs + sizeMs * static_cast<float> (kMaxRate + 1.0)) * 0.001f;
    }

    /** Grains currently sounding. For the tests - the pool must never overflow
        and the engine must fall silent when it is left alone. */
    int getActiveGrains() const noexcept
    {
        int n = 0;
        for (const auto& g : grains)
            if (g.active)
                ++n;
        return n;
    }

private:
    struct Grain
    {
        double position = 0.0;   // fractional index into buffer
        double rate = 1.0;       // samples of source per sample of output; negative plays backwards
        float windowPhase = 0.0f;
        float windowInc = 0.0f;
        float gainL = 0.0f;
        float gainR = 0.0f;
        int age = 0;
        int length = 0;
        bool active = false;
    };

    // A grain never exceeds this rate, which bounds how much source one spans
    // and therefore how far behind the write head it has to start. The interval
    // table tops out at +19 semitones (2.997x) and the detune adds a hair.
    static constexpr double kMaxRate = 3.2;

    static constexpr float kNormSmoothing = 0.0005f;

    // Envelope follower feeding the attack detector, and the detector's own
    // voicing. The same numbers Peak Wah's retrigger uses - a pluck is a pluck.
    static constexpr float kFollowerSeconds = 0.010f;
    static constexpr float kOnsetEnvDecayMs = 120.0f;
    static constexpr float kOnsetAttackWidthMs = 15.0f;
    static constexpr float kOnsetRiseRatioOn = 0.55f;
    static constexpr float kOnsetRiseRatioOff = 0.15f;
    static constexpr float kOnsetMinRise = 0.005f;
    static constexpr float kOnsetLockoutMs = 90.0f;
    static constexpr std::uint32_t kRngSeed = 0x9E3779B9u;

    //==========================================================================

    static const std::array<float, config::kWindowPoints>& windowTable()
    {
        static const std::array<float, config::kWindowPoints> table = []
        {
            std::array<float, config::kWindowPoints> t {};
            const double denom = static_cast<double> (config::kWindowPoints - 1);
            for (int i = 0; i < config::kWindowPoints; ++i)
                t[static_cast<size_t> (i)] =
                    static_cast<float> (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / denom));
            return t;
        }();
        return table;
    }

    /** Hann amplitude at a fractional table position. Both ends of the table are
        exactly zero, which is what makes a grain unable to click whatever its
        length or its content. */
    static float windowAt (float phase) noexcept
    {
        const auto& table = windowTable();

        if (phase <= 0.0f)
            return 0.0f;
        if (phase >= static_cast<float> (config::kWindowPoints - 1))
            return 0.0f;

        const int i = static_cast<int> (phase);
        const float frac = phase - static_cast<float> (i);

        return table[static_cast<size_t> (i)] * (1.0f - frac) + table[static_cast<size_t> (i + 1)] * frac;
    }

    /** Four-point Hermite read, the same interpolator ModDelayLine uses. Linear
        would lose the top octave off every backwards grain. */
    float read (double position) const noexcept
    {
        const int i1 = static_cast<int> (position);
        const float frac = static_cast<float> (position - static_cast<double> (i1));

        const int i0 = i1 > 0 ? i1 - 1 : size - 1;
        int i2 = i1 + 1; if (i2 >= size) i2 -= size;
        int i3 = i2 + 1; if (i3 >= size) i3 -= size;

        const float y0 = buffer[static_cast<size_t> (i0)];
        const float y1 = buffer[static_cast<size_t> (i1)];
        const float y2 = buffer[static_cast<size_t> (i2)];
        const float y3 = buffer[static_cast<size_t> (i3)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    //==========================================================================

    /** 0..1. Xorshift rather than juce::Random so the engine stays a plain
        header and a test can reproduce a run exactly. */
    float nextFloat() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float> (rngState & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    /** -1..1. */
    float nextBipolar() noexcept
    {
        return nextFloat() * 2.0f - 1.0f;
    }

    int nextInterval() noexcept
    {
        const float nominal = static_cast<float> (sampleRate) / densityHz;
        const float jittered = nominal * (1.0f + tuning.spawnJitter * nextBipolar());

        return std::max (1, static_cast<int> (jittered));
    }

    void updateDerived() noexcept
    {
        // Expected number of grains sounding at once. Below one there is nothing
        // to normalise - grains are not even touching - so the divisor floors at
        // unity rather than turning into a boost.
        const float overlap = std::max (1.0f, densityHz * sizeMs * 0.001f);
        normTarget = tuning.outputTrim / std::sqrt (overlap);
    }

    /** Picks a playback rate: one of the three pitch groups in proportion to
        their weights, an interval from that group's table, plus the detune. */
    double pickRate() noexcept
    {
        float semitones = 0.0f;

        const float total = pitchLow + pitchUnison + pitchHigh;

        if (total > 0.0f)
        {
            const float pick = nextFloat() * total;
            const int slot = std::min (3, static_cast<int> (nextFloat() * 4.0f));

            // Four slots per group; the repeats in the table are the weighting.
            if (pick < pitchLow)
            {
                const float down[] = { tuning.downA, tuning.downB, tuning.downC, tuning.downD };
                semitones = down[slot];
            }
            else if (pick >= pitchLow + pitchUnison)
            {
                const float up[] = { tuning.upA, tuning.upB, tuning.upC, tuning.upD };
                semitones = up[slot];
            }
        }

        const float cents = nextBipolar() * detuneCents;
        const double ratio = std::pow (2.0, (static_cast<double> (semitones) + cents * 0.01) / 12.0);

        return std::clamp (ratio, 1.0 / kMaxRate, kMaxRate);
    }

    /** The one place the buffer bookkeeping can go wrong, so all of it is here.

        A forward grain reads towards the write head faster than the head moves
        whenever its rate is above 1, and if it starts too close it catches up
        and reads samples that have not been written yet. A backwards grain has
        the opposite problem: it walks towards the oldest end of the buffer and
        can run off it. Both are prevented by where the grain is allowed to
        start, so the read loop itself never has to check.

        One consequence is visible on the face: a long grain pitched an octave
        up spans a second of source, so it cannot start less than that far back
        however low Spray is set. Spray 0 is a stutter on the present only while
        the grains are short enough to be one. */
    void spawnGrain() noexcept
    {
        Grain* slot = nullptr;

        for (auto& g : grains)
        {
            if (! g.active)
            {
                slot = &g;
                break;
            }
        }

        if (slot == nullptr)
        {
            // Pool full: take the grain nearest its own end, which is the one
            // whose window is quietest and so the least audible to cut short.
            float furthest = -1.0f;
            for (auto& g : grains)
            {
                const float progress = static_cast<float> (g.age) / static_cast<float> (std::max (1, g.length));
                if (progress > furthest)
                {
                    furthest = progress;
                    slot = &g;
                }
            }
        }

        if (slot == nullptr)
            return;

        const int length = std::clamp (static_cast<int> (sizeMs * 0.001f * static_cast<float> (sampleRate)),
                                       minGrainSamples, maxGrainSamples);

        const bool backwards = nextFloat() < reverse;
        const double rate = pickRate();

        // Source samples this grain spans, whichever way it runs.
        const int consumed = static_cast<int> (std::ceil (rate * length)) + config::kGrainReadMarginSamples;
        const int margin = config::kGrainReadMarginSamples;

        // How far behind the write head the read may start, in samples.
        int minOffset = margin;
        int maxOffset = size - length - margin;

        if (backwards)
            maxOffset -= consumed;              // room to walk backwards without leaving the buffer
        else
            minOffset = std::max (margin, consumed - length + margin);   // stay behind the write head

        if (maxOffset <= minOffset)
            return;

        const int decaySamples = static_cast<int> (decayMs * 0.001f * static_cast<float> (sampleRate));
        const int spread = std::min (decaySamples, maxOffset - minOffset);

        int offset = minOffset + (spread > 0 ? static_cast<int> (nextFloat() * static_cast<float> (spread)) : 0);
        bool fromAttack = false;

        // Most grains come from the last attack, if there was one recently
        // enough to still be in the window. That is what keeps the cloud
        // sounding like the note that was struck rather than like its sustain.
        if (attackIndex >= 0 && sinceAttack <= decaySamples && nextFloat() < tuning.attackShare)
        {
            const int jitter = static_cast<int> (config::kAttackJitterMs * 0.001f * static_cast<float> (sampleRate));
            const int wanted = sinceAttack + (jitter > 0 ? static_cast<int> (nextFloat() * static_cast<float> (jitter))
                                                         : 0);

            // Only if the attack sits somewhere this grain is allowed to read
            // from - the buffer rules are not negotiable.
            if (wanted >= minOffset && wanted <= maxOffset)
            {
                offset = wanted;
                fromAttack = true;
            }
        }

        double position = static_cast<double> (writeIndex - offset);
        if (position < 0.0)
            position += static_cast<double> (size);

        // Alternating rather than random: successive grains ping left, right,
        // left, right. Random placement clusters - three in a row on the same
        // side is common - and the ear hears that as the image wandering
        // rather than as a stereo effect.
        panLeft = ! panLeft;
        const float pan = (panLeft ? -1.0f : 1.0f) * stereo;
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;

        // The older the source, the quieter the grain. This is the audible half
        // of Decay: without it a long window is just a wider scatter at the
        // same level, and the cloud stops rather than fading.
        // How far across the Decay window this grain's source sits, and the
        // level that costs it. Measured against the window rather than in
        // absolute seconds, so Decay is a real tail length: at 8 s the cloud
        // fades over eight seconds instead of being gone inside the first one.
        //
        // Anchored on the newest source these settings can reach, not on the
        // present, which keeps the headroom - a grain is never louder than
        // unity - and stops a long grain pitched up, which cannot start near
        // the present, from being quiet purely because of that.
        //
        // A grain taken from the attack is exempt: it is placed where it is on
        // purpose, and fading it by how long ago the note was struck would make
        // the whole feature inaudible a second in.
        const float span = static_cast<float> (std::max (1, spread));
        const float age = std::min (1.0f, static_cast<float> (offset - minOffset) / span);
        const float ageGain = fromAttack ? 1.0f : std::pow (config::kOldestGrainGain, age);

        slot->position = position;
        slot->rate = backwards ? -rate : rate;
        slot->length = length;
        slot->age = 0;
        slot->windowPhase = 0.0f;
        slot->windowInc = static_cast<float> (config::kWindowPoints - 1) / static_cast<float> (length);
        slot->gainL = std::cos (angle) * ageGain;
        slot->gainR = std::sin (angle) * ageGain;
        slot->active = true;
    }

    //==========================================================================

    double sampleRate = 44100.0;

    std::vector<float> buffer;
    int size = 0;
    int writeIndex = 0;

    std::array<Grain, config::kMaxGrains> grains {};
    int spawnCountdown = 1;

    int minGrainSamples = 1;
    int maxGrainSamples = 1;

    float sizeMs = config::kDefaultGrainMs;
    float densityHz = config::kDefaultDensityHz;
    float decayMs = config::kDefaultDecayMs;
    float reverse = config::kDefaultReversePct * 0.01f;
    float stereo = config::kDefaultStereoPct * 0.01f;
    float detuneCents = config::kDefaultDetuneCents;

    float pitchLow = config::kDefaultPitchLowPct;
    float pitchUnison = config::kDefaultPitchUnisonPct;
    float pitchHigh = config::kDefaultPitchHighPct;

    GrainerTuning tuning;

    float normTarget = 1.0f;
    float smoothedNorm = 1.0f;

    std::uint32_t rngState = kRngSeed;

    // Attack tracking: where the last onset landed, and how far the write head
    // has moved past it. -1 means nothing has been detected yet.
    OnsetGate onsetGate;
    float follower = 0.0f;
    float followerCoeff = 0.0f;
    int attackIndex = -1;
    int sinceAttack = 0;

    /** Which side the next grain goes to - see the pan comment in spawnGrain. */
    bool panLeft = false;
};

} // namespace ee::dsp
