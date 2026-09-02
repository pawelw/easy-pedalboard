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

/** Granular delay: stereo in, stereo out, wet only.

    Input is summed to mono and recorded into a circular buffer. On a jittered
    timer a grain is spawned - a windowed voice that reads that buffer from a
    point Time behind the write head (scattered a little either side), at a
    random rate which is its pitch, in a random direction, placed at a random
    pan position. Many overlap, and the sum is a cloud of fragments of what was
    played Time ago.

    Feedback writes that cloud back into the buffer, so each repeat is
    granulated again on the way round. Freeze stops the recording and holds the
    buffer; the read head then scans the frozen capture at the Stretch rate -
    forwards, held still, or backwards - and a loud enough input retriggers a
    fresh capture.

    The feedback path means this is no longer strictly feed-forward, so it could
    in principle latch a non-finite value. Four things stop it: the feedback
    gain is hard-capped below unity (config::kMaxFeedback), the fed-back sample
    is run through tanh so its magnitude is always < 1, the value written into
    the buffer is zeroed if it is not finite, and the processor guards its own
    output on top. `ee_grain_stress` sweeps the feedback range against DC and
    noise to keep this honest.

    Everything about the character that is not on a knob - how Scatter and Shape
    map onto the engine, the interval tables, the output trim - is in
    GrainerTuning.h, and can be driven live by the development panel.
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

        updateTimeOffset();
        updateDerived();
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
        readHead = 0.0;

        for (auto& g : grains)
            g.active = false;

        spawnCountdown = 1;
        rngState = kRngSeed;
        smoothedNorm = normTarget;
        feedbackSample = 0.0f;
        recordedSamples = 0;

        frozen = false;
        capturing = false;
        captureRemaining = 0;
        pendingCaptureLen = 0;
        scanPos = 0.0;
        freezeLoopStart = 0;
        freezeLoopLen = 1;

        onsetGate.reset();
        follower = 0.0f;
        attackIndex = -1;
        sinceAttack = 0;
    }

    //==========================================================================
    // The knobs. All are latched by the next grain to spawn (or, for Feedback
    // and Stretch, read per sample); none of them disturbs a grain already in
    // flight, so none of them can click.

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

    /** The delay: how far behind the write head grains are tapped from. */
    void setTimeMs (float ms) noexcept
    {
        timeMs = std::clamp (ms, config::kMinTimeMs, config::kMaxTimeMs);
        updateTimeOffset();
    }

    /** Share of the granulated output written back into the buffer, 0 to
        config::kMaxFeedback. */
    void setFeedback (float amount01) noexcept
    {
        feedback = std::clamp (amount01, 0.0f, config::kMaxFeedback);
    }

    /** Read-head scan rate while frozen, in multiples of realtime. +1 forward,
        0 held, -1 backwards. Ignored while playing live. */
    void setStretch (float rate) noexcept
    {
        stretch = std::clamp (rate, -1.0f, 1.0f);
    }

    /** Freeze the buffer: stop recording and hold it, and let Stretch scan the
        capture. A loud input still retriggers a fresh capture. */
    void setFreeze (bool shouldFreeze) noexcept
    {
        if (shouldFreeze && ! frozen)
        {
            // Loop the most recent audio, never more of the buffer than has
            // actually been written - so Stretch cannot scan into the unwritten
            // tail and read silence.
            const int cap = std::max (1, size - 2 * config::kGrainReadMarginSamples);
            const int wanted = static_cast<int> (config::kFreezeLoopSeconds * sampleRate);
            const int len = recordedSamples > 0 ? std::min (recordedSamples, std::min (wanted, cap)) : cap;

            beginFreezeWindow (len);
        }

        frozen = shouldFreeze;
        if (! frozen)
            capturing = false;
    }

    /** Grain-envelope lean, 0 (soft) to 1 (plucky). */
    void setShape (float amount01) noexcept
    {
        shape = std::clamp (amount01, 0.0f, 1.0f);
        updateDerived();
    }

    /** Timing randomness, 0 (metronomic, identical grains) to 1. Drives both
        the spawn-gap jitter and the per-grain size jitter. */
    void setScatter (float amount01) noexcept
    {
        scatter = std::clamp (amount01, 0.0f, 1.0f);
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

            const bool recording = ! frozen || capturing;

            if (recording)
            {
                float written = sample + feedback * feedbackSample;
                if (! std::isfinite (written))
                    written = 0.0f;

                buffer[static_cast<size_t> (writeIndex)] = written;

                if (recordedSamples < size)
                    ++recordedSamples;

                // Mark where the attack landed, so live grains can be drawn from
                // it rather than from wherever the tap window happens to reach.
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

                readHead = static_cast<double> (writeIndex);

                if (capturing && --captureRemaining <= 0)
                {
                    // Fresh capture done - re-freeze and loop just what was
                    // grabbed, from its start.
                    capturing = false;
                    beginFreezeWindow (pendingCaptureLen);
                }
            }
            else
            {
                // Frozen: the buffer is held. Still watch the input so a loud
                // enough note can start the capture cycle over again.
                const float rectified = std::abs (sample);
                follower = rectified > follower ? rectified : rectified + followerCoeff * (follower - rectified);

                if (onsetGate (follower))
                {
                    const int wanted = timeOffsetSamples + maxGrainSamples + config::kGrainReadMarginSamples;
                    pendingCaptureLen = std::clamp (wanted,
                                                    static_cast<int> (config::kMinRecaptureSeconds * sampleRate),
                                                    std::max (1, size - 2 * config::kGrainReadMarginSamples));
                    captureRemaining = pendingCaptureLen;
                    capturing = true;
                }

                // Scan within the freeze window and wrap inside it, so Stretch
                // never runs off the captured audio.
                scanPos += static_cast<double> (stretch) * config::kStretchMax;
                scanPos = std::fmod (scanPos, static_cast<double> (freezeLoopLen));
                if (scanPos < 0.0)
                    scanPos += static_cast<double> (freezeLoopLen);

                readHead = std::fmod (static_cast<double> (freezeLoopStart) + scanPos, static_cast<double> (size));
            }

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

                const float windowed = read (g.position) * envelopeOf (g);

                sumL += windowed * g.gainL;
                sumR += windowed * g.gainR;

                g.position += g.rate;
                if (g.position >= static_cast<double> (size))
                    g.position -= static_cast<double> (size);
                else if (g.position < 0.0)
                    g.position += static_cast<double> (size);

                if (++g.age >= g.length)
                    g.active = false;
            }

            // The normaliser tracks the overlap, which moves whenever Size or
            // Density does. Smoothed so those knobs do not step the level.
            smoothedNorm += (normTarget - smoothedNorm) * kNormSmoothing;

            const float wetL = sumL * smoothedNorm;
            const float wetR = sumR * smoothedNorm;

            outL[i] = wetL;
            outR[i] = wetR;

            // What goes back round next sample. tanh bounds it to (-1, 1)
            // whatever the cloud does, so the recirculation cannot build
            // without limit; a non-finite cloud feeds back nothing.
            const float cloudMono = 0.5f * (wetL + wetR);
            feedbackSample = std::isfinite (cloudMono) ? std::tanh (cloudMono) : 0.0f;
        }
    }

    /** How long the cloud keeps going after the input stops. A frozen buffer
        never stops on its own, so that is reported as a long fixed tail. */
    float getTailSeconds() const noexcept
    {
        if (frozen)
            return kFrozenTailSeconds;

        const float repeats = 1.0f / std::max (0.08f, 1.0f - feedback);
        const float seconds = (timeMs * repeats + sizeMs * static_cast<float> (kMaxRate + 1.0)) * 0.001f;
        return std::min (seconds, kFrozenTailSeconds);
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
        int attackSamples = 1;   // length of the fade-in
        float decayEnv = 1.0f;   // running exponential, stepped once per sample
        float decayMul = 1.0f;
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

    // A frozen buffer rings for ever; the host still wants a number.
    static constexpr float kFrozenTailSeconds = 30.0f;

    /** RMS of a Hann window, which is the envelope `outputTrim` was originally
        calibrated against. Keeping it as the reference means the trim still
        means what it used to, and only the envelope's own energy is divided
        out. sqrt(3/8). */
    static constexpr float kEnvelopeReferenceRms = 0.6124f;

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

    /** Amplitude of a grain at its current age, and steps its decay on.

        Asymmetric on purpose. A symmetric window fades the grain in over its
        whole first half, which is fatal here: the transient a plucked string is
        mostly made of gets thrown away, and what is left is a swell that sounds
        for all the world like the note was reversed. So the fade-in is only as
        long as Shape asks for - a millisecond or three - and everything after
        it is an exponential decay.

        The decay is stepped by a multiply rather than recomputed, and it is
        offset so it reaches exactly zero at the end of the grain. Zero at both
        ends is what makes a grain unable to click whatever its content. */
    float envelopeOf (Grain& g) const noexcept
    {
        if (g.age < g.attackSamples)
            return static_cast<float> (g.age) / static_cast<float> (g.attackSamples);

        const float env = (g.decayEnv - decayFloor) * decayScale;
        g.decayEnv *= g.decayMul;

        return env > 0.0f ? env : 0.0f;
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
        const float jitterFrac = scatter * tuning.scatterMaxJitter;
        const float nominal = static_cast<float> (sampleRate) / densityHz;
        const float jittered = nominal * (1.0f + jitterFrac * nextBipolar());

        return std::max (1, static_cast<int> (jittered));
    }

    /** Point the frozen read head at the last `len` samples before the write
        head and start it scanning from the tap point inside that window. */
    void beginFreezeWindow (int len) noexcept
    {
        freezeLoopLen = std::clamp (len, 1, std::max (1, size - 2 * config::kGrainReadMarginSamples));

        int start = (writeIndex - freezeLoopLen) % size;
        if (start < 0)
            start += size;
        freezeLoopStart = start;

        scanPos = static_cast<double> (std::min (timeOffsetSamples, freezeLoopLen - 1));
        if (scanPos < 0.0)
            scanPos = 0.0;

        readHead = std::fmod (static_cast<double> (freezeLoopStart) + scanPos, static_cast<double> (size));
    }

    void updateTimeOffset() noexcept
    {
        if (size <= 0)
            return;

        const int wanted = static_cast<int> (timeMs * 0.001f * static_cast<float> (sampleRate));
        const int minOff = config::kGrainReadMarginSamples + 1;
        const int maxOff = size - maxGrainSamples - config::kGrainReadMarginSamples - 1;

        timeOffsetSamples = std::clamp (wanted, minOff, std::max (minOff, maxOff));
    }

    void updateDerived() noexcept
    {
        // Shape morphs the grain envelope between the two ends the tuning names.
        curDecayShape = tuning.shapeDecayShapeSoft + shape * (tuning.shapeDecayShapeHard - tuning.shapeDecayShapeSoft);
        curAttackMs = tuning.shapeAttackMsSoft + shape * (tuning.shapeAttackMsHard - tuning.shapeAttackMsSoft);

        // Expected number of grains sounding at once. Below one there is nothing
        // to normalise - grains are not even touching - so the divisor floors at
        // unity rather than turning into a boost.
        const float overlap = std::max (1.0f, densityHz * sizeMs * 0.001f);

        // Offset and rescale the decay so it starts at exactly 1 and lands on
        // exactly 0, whatever shape is dialled in.
        decayFloor = std::exp (-curDecayShape);
        decayScale = 1.0f / std::max (1.0e-6f, 1.0f - decayFloor);

        // A steeper decay puts less energy in the grain, so without this the
        // Shape control would double as a volume control and there would be no
        // judging it by ear. The envelope's own RMS is known in closed form:
        // for env(u) = (e^-ku - f) / (1 - f) with f = e^-k,
        //
        //   mean square = [ (1-f^2)/2k - 2f(1-f)/k + f^2 ] / (1-f)^2
        //
        // The short attack is ignored - a millisecond against a grain measured
        // in tens of them.
        const float k = std::max (0.05f, curDecayShape);
        const float f = decayFloor;
        const float meanSquare =
            ((1.0f - f * f) / (2.0f * k) - 2.0f * f * (1.0f - f) / k + f * f) / std::max (1.0e-6f, (1.0f - f) * (1.0f - f));
        const float envelopeRms = std::sqrt (std::max (1.0e-6f, meanSquare));

        normTarget = tuning.outputTrim * (kEnvelopeReferenceRms / envelopeRms) / std::sqrt (overlap);
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

    /** Where the next grain reads from, and the bookkeeping that keeps that
        read legal.

        Live, a forward grain reads towards the write head faster than the head
        moves whenever its rate is above 1, and if it starts too close it
        catches up and reads samples that have not been written yet. A backwards
        grain has the opposite problem: it walks towards the oldest end of the
        buffer and can run off it. Both are prevented by where the grain is
        allowed to start.

        Frozen, the buffer is full and static, so any position holds real
        content - the read loop wraps and there is nothing to guard. */
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

        // Grain length, strayed from Size by Scatter.
        float lengthF = sizeMs * 0.001f * static_cast<float> (sampleRate);
        lengthF *= 1.0f + scatter * tuning.scatterSizeJitter * nextBipolar();
        const int length = std::clamp (static_cast<int> (lengthF), minGrainSamples, maxGrainSamples);

        const bool backwards = nextFloat() < reverse;
        const double rate = pickRate();

        double position = 0.0;

        if (frozen && ! capturing)
        {
            // Scan position, scattered a little either side.
            const double jitter =
                static_cast<double> (scatter) * static_cast<double> (timeOffsetSamples) * 0.5 * static_cast<double> (nextBipolar());

            position = std::fmod (readHead + jitter, static_cast<double> (size));
            if (position < 0.0)
                position += static_cast<double> (size);
        }
        else
        {
            // Source samples this grain spans, whichever way it runs.
            const int consumed = static_cast<int> (std::ceil (rate * length)) + config::kGrainReadMarginSamples;
            const int margin = config::kGrainReadMarginSamples;

            int minOffset = margin;
            int maxOffset = size - length - margin;

            if (backwards)
                maxOffset -= consumed;
            else
                minOffset = std::max (margin, consumed - length + margin);

            if (maxOffset <= minOffset)
                return;

            int offset = timeOffsetSamples;

            const int spread = static_cast<int> (scatter * static_cast<float> (timeOffsetSamples) * 0.5f);
            if (spread > 0)
                offset += static_cast<int> (nextBipolar() * static_cast<float> (spread));

            // Most grains come from the last attack, if there was one recently
            // enough that the note is still ringing. That is what keeps the
            // cloud sounding like the note that was struck rather than like its
            // sustain - but it lapses after kAttackReachSeconds so a long
            // silence really does fall silent.
            const int attackReach = static_cast<int> (config::kAttackReachSeconds * static_cast<float> (sampleRate));

            if (attackIndex >= 0 && sinceAttack <= attackReach && sinceAttack <= maxOffset
                && nextFloat() < tuning.attackShare)
            {
                const int jitterSamples = static_cast<int> (config::kAttackJitterMs * 0.001f * static_cast<float> (sampleRate));
                const int wanted =
                    sinceAttack + (jitterSamples > 0 ? static_cast<int> (nextFloat() * static_cast<float> (jitterSamples)) : 0);

                if (wanted >= minOffset && wanted <= maxOffset)
                    offset = wanted;
            }

            offset = std::clamp (offset, minOffset, maxOffset);

            position = static_cast<double> (writeIndex - offset);
            if (position < 0.0)
                position += static_cast<double> (size);
        }

        const float pan = nextBipolar() * stereo;
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;

        slot->position = position;
        slot->rate = backwards ? -rate : rate;
        slot->length = length;
        slot->age = 0;

        // Just enough fade-in not to click, and never more than half the grain -
        // a 20 ms grain cannot afford a 5 ms attack.
        const int attackSamples = std::clamp (
            static_cast<int> (curAttackMs * 0.001f * static_cast<float> (sampleRate)), 1, std::max (1, length / 2));

        slot->attackSamples = attackSamples;
        slot->decayEnv = 1.0f;
        slot->decayMul = std::exp (-curDecayShape / static_cast<float> (std::max (1, length - attackSamples)));
        slot->gainL = std::cos (angle);
        slot->gainR = std::sin (angle);
        slot->active = true;
    }

    //==========================================================================

    double sampleRate = 44100.0;

    std::vector<float> buffer;
    int size = 0;
    int writeIndex = 0;
    double readHead = 0.0;

    std::array<Grain, config::kMaxGrains> grains {};
    int spawnCountdown = 1;

    int minGrainSamples = 1;
    int maxGrainSamples = 1;
    int timeOffsetSamples = 1;

    float sizeMs = config::kDefaultGrainMs;
    float densityHz = config::kDefaultDensityHz;
    float timeMs = config::kDefaultTimeMs;
    float feedback = config::kDefaultFeedbackPct * 0.01f;
    float stretch = config::kDefaultStretchPct * 0.01f;
    float shape = config::kDefaultShapePct * 0.01f;
    float scatter = config::kDefaultScatterPct * 0.01f;
    float reverse = config::kDefaultReversePct * 0.01f;
    float stereo = config::kDefaultStereoPct * 0.01f;
    float detuneCents = config::kDefaultDetuneCents;

    float pitchLow = config::kDefaultPitchLowPct;
    float pitchUnison = config::kDefaultPitchUnisonPct;
    float pitchHigh = config::kDefaultPitchHighPct;

    bool frozen = false;
    bool capturing = false;
    int captureRemaining = 0;
    int pendingCaptureLen = 0;

    // Samples of real audio written since the last reset, so a Freeze never
    // loops more of the buffer than has been recorded into.
    int recordedSamples = 0;

    // The stretch of buffer a Freeze loops: [freezeLoopStart, +freezeLoopLen),
    // modulo size. scanPos is the read head's offset into it.
    double scanPos = 0.0;
    int freezeLoopStart = 0;
    int freezeLoopLen = 1;

    float feedbackSample = 0.0f;

    GrainerTuning tuning;

    float normTarget = 1.0f;
    float smoothedNorm = 1.0f;

    float curDecayShape = 4.0f;
    float curAttackMs = 1.0f;
    float decayFloor = 0.0f;
    float decayScale = 1.0f;

    std::uint32_t rngState = kRngSeed;

    // Attack tracking: where the last onset landed, and how far the write head
    // has moved past it. -1 means nothing has been detected yet.
    OnsetGate onsetGate;
    float follower = 0.0f;
    float followerCoeff = 0.0f;
    int attackIndex = -1;
    int sinceAttack = 0;
};

} // namespace ee::dsp
