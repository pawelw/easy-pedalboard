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
    With Grid (Transport::grid) read points stay on the tempo grid: frozen, the
    capture follows the bar line and every grain replays a sixteenth taken from
    the current bar's start; live, the Time tap moves in whole sixteenths - see
    GrainerConfig.h's GRID.

    The feedback path means this is no longer strictly feed-forward, so it could
    in principle latch a non-finite value. Four things stop it: the feedback
    gain is hard-capped below unity (config::kMaxFeedback), the fed-back sample
    is run through tanh so its magnitude is always < 1, the value written into
    the buffer is zeroed if it is not finite, and the processor guards its own
    output on top. `ee_grain_stress` sweeps the feedback range against DC and
    noise to keep this honest.

    Everything about the character that is not on a knob - how Scatter and Shape
    map onto the engine, the output trim - is in GrainerTuning.h, and can be
    driven live by the development panel. Which intervals the pitched grains
    snap to is on a knob (Scale/Root, see setScale() and GrainerConfig.h's
    SCALE section) rather than fixed tuning.
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

        // One envelope reading per kEnvTraceDecim samples of the buffer, so a
        // grain can ask how loud its own read point was without walking the
        // audio at spawn time - see sourceLevelGain().
        envTraceSize = size / kEnvTraceDecim + 2;
        envTrace.assign (static_cast<size_t> (envTraceSize), 0.0f);
        loudRefCoeff = std::exp (-1.0f / (fs * kLoudRefSeconds));

        updateCloudFilter();

        // So the candidate tables are populated even before the processor's
        // first call to setScale() - a no-op if it was already called with
        // something other than the default (see that function's guard).
        setScale (config::kDefaultScaleIndex, config::kDefaultRootSemitone);

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
        spawnPhase = 0.0;
        expectedSpawnPpq = 0.0;
        haveExpectedSpawnPpq = false;
        wasSpawnPlaying = false;
        rngState = kRngSeed;
        smoothedNorm = normTarget;
        feedbackSample = 0.0f;
        cloudHpX1L = cloudHpY1L = cloudHpX1R = cloudHpY1R = 0.0f;
        cloudLpZL = cloudLpZR = 0.0f;
        cloudHpX1S = cloudHpY1S = 0.0f;
        cloudLpZS = 0.0f;
        recordedSamples = 0;
        wowPhase = 0.0;

        frozen = false;
        capturing = false;
        barLocked = false;
        liveGrid = false;
        attackStackPending = false;
        samplesIntoBar = 0.0;
        samplesPerSixteenth = 1.0;
        captureRemaining = 0;
        pendingCaptureLen = 0;
        scanPos = 0.0;
        freezeLoopStart = 0;
        freezeLoopLen = 1;

        onsetGate.reset();
        follower = 0.0f;
        attackIndex = -1;
        sinceAttack = 0;

        std::fill (envTrace.begin(), envTrace.end(), 0.0f);
        loudRef = 0.0f;
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
    void setFeedback (float amount01) noexcept { feedback = std::clamp (amount01, 0.0f, config::kMaxFeedback); }

    /** How long after an attack grains may still be drawn from it - see
        pickPosition()'s own note. Starts at config::kAttackReachSeconds, the
        figure this was a compile-time constant at before Window went on the
        face; independent of Feedback, which is its own knob and its own
        recirculating tail (see the class note above) - `ee_dsp_tests`'
        testGrainerAttackCapture and testGrainerFeedbackLengthensTail each
        hold one of those two apart from the other on purpose. */
    void setAttackReachSeconds (float seconds) noexcept
    {
        attackReachSeconds = std::clamp (seconds, config::kMinWindowSeconds, config::kMaxWindowSeconds);
    }

    /** Read-head scan rate while frozen, in multiples of realtime. +1 forward,
        0 held, -1 backwards. Ignored while playing live. */
    void setStretch (float rate) noexcept { stretch = std::clamp (rate, -1.0f, 1.0f); }

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

    /** Grain-window swell, 0 to 1. At 0 the window is exactly Shape's - a quick
        fade-in and a decay, so the start of every grain is heard. At 1 it is
        the reverse: a linear swell that peaks late in the grain
        (GrainerTuning::smoothPeak) and is cut off shortly after, the same
        every time - the start of each grain is buried in the fade-in and what
        you hear is the vowel, not the onset. On the way it also fades out the
        per-grain randomness (window and level jitter) and the band split's
        different grain lengths, both of which would otherwise make every
        grain a different shape. Blended per grain, not per sample: a grain
        wears the value it was born with. */
    void setSmooth (float amount01) noexcept
    {
        smooth = std::clamp (amount01, 0.0f, 1.0f);
        updateDerived();
    }

    /** Timing randomness, 0 (metronomic, identical grains) to 1. Drives both
        the spawn-gap jitter and the per-grain size jitter. */
    void setScatter (float amount01) noexcept { scatter = std::clamp (amount01, 0.0f, 1.0f); }

    /** Share of grains that play backwards, 0 to 1. */
    void setReverse (float amount01) noexcept { reverse = std::clamp (amount01, 0.0f, 1.0f); }

    /** Width of the random pan placement, 0 (centred) to 1 (hard left/right). */
    void setStereo (float amount01) noexcept { stereo = std::clamp (amount01, 0.0f, 1.0f); }

    /** The Filter knob: a plain lowpass cutoff and nothing else. 1 is fully
        open at config::kCloudLowpassHz, 0 fully closed at
        config::kCloudLowpassMinHz, and the sweep between them is geometric so
        equal knob steps are equal musical intervals. One pole, 6 dB/oct -
        deliberately gentle rather than a ladder, and there is no resonance
        anywhere in this engine, so the response really is just the rolloff.

        The highpass is no longer on the knob. It is a fixed, hidden trap at
        config::kCloudHighpassHz that keeps the cloud tight underneath, and
        nothing on the face moves it. Cheap to call every block; it only
        recomputes when the value actually moved. */
    void setCloudFilter (float openness) noexcept
    {
        const float clamped = std::clamp (openness, 0.0f, 1.0f);

        if (std::abs (clamped - cloudFilterAmount) < 1.0e-6f)
            return;

        cloudFilterAmount = clamped;
        updateCloudFilter();
    }

    /** Which scale (config::kScales index) and root (0 = C .. 11 = B, any
        octave) the Low/High pitch groups' interval choices are quantized to -
        see GrainerConfig.h's SCALE section for what Root can and cannot mean
        without pitch tracking, and pickRate() for where the result is used.
        Cheap to call every block: only recomputes the candidate tables when
        the scale or root actually changed. */
    void setScale (int scaleIndex, int rootSemitone) noexcept
    {
        const int root = ((rootSemitone % 12) + 12) % 12;
        const int clampedScale = std::clamp (scaleIndex, 0, config::kNumScales - 1);

        if (clampedScale == currentScaleIndex && root == currentRootSemitone)
            return;

        currentScaleIndex = clampedScale;
        currentRootSemitone = root;

        const auto& scale = config::kScales[static_cast<size_t> (clampedScale)];

        upCount = 0;
        downCount = 0;

        // High: the scale, an octave up. Anchored at +12 rather than at
        // unison, because drawing from the whole range either side let most
        // grains land a semitone or two off the note - which is not a
        // transposition anybody can hear, and is what made the knob read as
        // doing nothing at all. Root still rotates the pattern, so the
        // octave itself is only a candidate when it belongs to the key: it
        // does at Root C, it does not at Root F#.
        for (int n = 12; n <= config::kMaxScaleSemitones; ++n)
            if (isScaleMember (scale, n, root) && upCount < kMaxScaleCandidates)
                upCandidates[static_cast<size_t> (upCount++)] = static_cast<float> (n);

        // Low: whole octaves down, and nothing else - an octave is consonant
        // against anything, so the bottom of the cloud adds weight without
        // ever landing on a wrong note, whatever the scale says. Two of them:
        // -24 is rate 0.25, which is exactly what kMinRate is set to.
        downCandidates[0] = -12.0f;
        downCandidates[1] = -24.0f;
        downCount = 2;

        updateIntervalNorm();
    }

    /** How much of the scale the High group takes: 0 is a plain octave up for
        every grain whatever setScale() was told, 1 draws each one from that
        Scale/Root table, and in between it is the odds of any one grain taking
        a scale degree rather than the octave. This is Pitch Mix, and colouring
        the interval is the *only* thing it and the Scale switch do - neither
        touches the weight of the three pitch groups, so High stays as loud as
        it was dialled however the scale is set. */
    void setScaleBlend (float amount01) noexcept
    {
        scaleBlend = std::clamp (amount01, 0.0f, 1.0f);
        updateIntervalNorm();
    }

    /** Drift: every grain spawned samples the same slow shared sine
        (config::kModWowHz) as a pitch bend, up to config::kModMaxCents at
        full travel - see pickRate(). The whole cloud's pitch rises and falls
        together over one cycle, the way ee::dsp::TapeDelay's own Mod knob
        wobbles its one continuously-playing tap. 0 is exactly bypassed. */
    void setMod (float amount01) noexcept { modAmount = std::clamp (amount01, 0.0f, 1.0f); }

    /** Crush: each active grain sample-and-holds independently, at the rate
        config::bitHoldNFor maps the knob to - the same reduction BitBit
        Artifact's Amp engine applies to its own Bit knob. 0 is exactly
        bypassed (every grain reads unheld). */
    void setBit (float amount01) noexcept
    {
        bitAmount = std::clamp (amount01, 0.0f, 1.0f);
        bitHoldN = config::bitHoldNFor (bitAmount, sampleRate);
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
        updateIntervalNorm();
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

    /** What the host transport is doing this block, for the Density spawn timer
        alone - Size, Time and everything else stay exactly as their knobs say.
        Mirrors ee::dsp::Tremolo::Transport: `synced` is the caller's decision
        ("the Density Sync switch is on AND the host gave us a finite ppq AND it
        is playing"), not a parameter, and `playing` is the transport's own state
        so a run of unsynced-but-playing blocks still counts as playing when Sync
        is switched on mid-take. A default-constructed Transport, `{}`, gets a
        caller that never touches sync exactly the old free-running spawn timer. */
    struct Transport
    {
        bool synced = false;
        bool playing = false;
        double ppqStart = 0.0;
        double cyclesPerQuarter = 1.0;
        double ppqPerSample = 0.0;

        // Grid (see GrainerConfig.h's GRID). BitBit Grain always sets it; there
        // is no switch. The live tap only needs ppqPerSample (a tempo), so it
        // holds with the transport stopped or absent; the frozen bar-locked
        // capture also needs a ppq that is really moving, and takes it only
        // while `synced`. barStartPpq is the ppq of any bar line,
        // quartersPerBar the bar's length.
        bool grid = false;
        double barStartPpq = 0.0;
        double quartersPerBar = 4.0;
    };

    //==========================================================================

    /** Writes the wet grain cloud to outL/outR, spawn timer free-running off
        Density and Scatter exactly as it always has - the overload below is the
        one to reach for once a caller cares about locking that timer to a host
        transport. `inR` may be null for a mono source. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
    {
        process (inL, inR, outL, outR, nullptr, numSamples, Transport {});
    }

    /** Writes the wet grain cloud to outL/outR. The caller keeps its own dry.
        In and out may alias: every input sample is read before its output slot
        is written. `inR` may be null for a mono source.

        `transport` only matters while Density is synced: it phase-locks the
        spawn timer to the host grid (a hard snap on the first playing block or
        after a relocate, otherwise a gentle per-block pull) so the same bar
        always spawns a grain on the same beat, the way ee::dsp::Tremolo locks
        its LFO. A default-constructed `Transport{}`, as the overload above
        passes, leaves the timer free-running exactly as before sync existed. */
    void process (const float* inL,
                  const float* inR,
                  float* outL,
                  float* outR,
                  int numSamples,
                  const Transport& transport) noexcept
    {
        process (inL, inR, outL, outR, nullptr, numSamples, transport);
    }

    /** True while the interval send weighting is dialled in, so a caller can
        decide whether the extra bus below is worth asking for at all. */
    bool hasIntervalSend() const noexcept { return tuning.pitchSendPerOctave != 0.0f; }

    /** As above, plus a mono send bus: the same cloud, but with every grain
        scaled by what its interval is worth as a reverb send (see
        GrainerTuning::pitchSendPerOctave). It leaves the engine at the same
        point the main output does - after the cloud filter, before anything
        the caller puts downstream - so a caller wanting the two coloured alike
        has to run its own stages over both. `sendOut` may be null, which is
        what the overloads above pass and costs nothing. */
    void process (const float* inL,
                  const float* inR,
                  float* outL,
                  float* outR,
                  float* sendOut,
                  int numSamples,
                  const Transport& transport) noexcept
    {
        if (size <= 0 || outL == nullptr || outR == nullptr)
            return;

        const bool wantSend = sendOut != nullptr;

        const bool spawnSynced = transport.synced;

        double spawnPhaseInc = spawnSynced ? transport.cyclesPerQuarter * transport.ppqPerSample : 0.0;
        if (! std::isfinite (spawnPhaseInc) || spawnPhaseInc < 0.0)
            spawnPhaseInc = 0.0;
        spawnPhaseInc = std::min (spawnPhaseInc, 1.0);

        const bool spawnOnArrival = alignSpawnToTransport (transport, numSamples, spawnPhaseInc);

        // Grid. Frozen and bar-locked, the buffer keeps recording and
        // spawnGrain() anchors each grain to the current bar line; leaving it
        // while still frozen holds whatever was just recorded, the way engaging
        // a plain Freeze does. Live, spawnGrain() only rounds the Time tap.
        const bool gridOn = transport.grid && transport.ppqPerSample > 0.0 && transport.quartersPerBar > 0.0 &&
                            std::isfinite (transport.ppqStart) && std::isfinite (transport.barStartPpq);
        const bool nowBarLocked = frozen && gridOn && transport.synced;

        if (nowBarLocked && ! barLocked)
        {
            capturing = false;
        }
        else if (barLocked && ! nowBarLocked && frozen)
        {
            frozen = false;
            setFreeze (true);
        }

        barLocked = nowBarLocked;
        liveGrid = gridOn && ! frozen;
        if (gridOn)
            samplesPerSixteenth = 0.25 / transport.ppqPerSample;

        for (int i = 0; i < numSamples; ++i)
        {
            // Mod's shared drift phase, advanced here (ahead of any grain
            // that spawns this sample) so pickRate() always reads "now" -
            // see its own note and GrainerConfig.h's on why this moves pitch
            // rather than read position.
            if (modAmount > 0.0f)
            {
                wowPhase += static_cast<double> (config::kModWowHz) / sampleRate;
                if (wowPhase >= 1.0)
                    wowPhase -= 1.0;
            }

            const float l = inL != nullptr ? inL[i] : 0.0f;
            const float r = inR != nullptr ? inR[i] : l;
            const float mono = 0.5f * (l + r);
            const float sample = std::isfinite (mono) ? mono : 0.0f;

            const bool recording = ! frozen || capturing || barLocked;

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
                updateLoudRef();

                // ...and how loud the buffer was here, for the grains that
                // will later be drawn from this point.
                if (envTraceSize > 0)
                    envTrace[static_cast<size_t> (writeIndex / kEnvTraceDecim)] = follower;

                if (onsetGate (follower))
                {
                    attackIndex = writeIndex;
                    sinceAttack = 0;
                    attackStackPending = true;
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
                updateLoudRef();

                if (onsetGate (follower))
                {
                    const int wanted = timeOffsetSamples + maxGrainSamples + config::kGrainReadMarginSamples;
                    pendingCaptureLen =
                        std::clamp (wanted, static_cast<int> (config::kMinRecaptureSeconds * sampleRate),
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

            bool spawnNow = false;

            if (i == 0 && spawnOnArrival)
            {
                // Starting or relocating landed (to within a sample) right on
                // the beat Density is synced to - drop a grain there instead of
                // leaving the first cycle silent while spawnPhase counts up from
                // the top all over again.
                spawnNow = true;
            }
            else if (spawnSynced)
            {
                spawnPhase += spawnPhaseInc;
                if (spawnPhase >= 1.0)
                {
                    spawnPhase -= std::floor (spawnPhase);
                    spawnNow = true;
                }
            }
            else if (--spawnCountdown <= 0)
            {
                spawnNow = true;
                spawnCountdown = nextInterval();
            }

            if (spawnNow)
            {
                if (barLocked)
                {
                    // How far this sample is past the current bar line, in
                    // samples - read once per grain, not per sample.
                    const double ppqNow = transport.ppqStart + static_cast<double> (i) * transport.ppqPerSample;
                    double into = std::fmod (ppqNow - transport.barStartPpq, transport.quartersPerBar);
                    if (into < 0.0)
                        into += transport.quartersPerBar;

                    const double snap =
                        static_cast<double> (config::kGridBarSnapMs) * 0.001 * sampleRate * transport.ppqPerSample;
                    if (transport.quartersPerBar - into < snap)
                        into = 0.0;

                    samplesIntoBar = into / transport.ppqPerSample;
                }

                // The first grain after a struck note leads with the octave
                // below, or all three octaves at once - see GrainerConfig.h's
                // ATTACK OCTAVES. Anything else spawns exactly as it always has.
                const int attackReach = static_cast<int> (attackReachSeconds * static_cast<float> (sampleRate));
                const bool stack =
                    attackStackPending && ! frozen && pitchLow > 0.0f && attackIndex >= 0 && sinceAttack <= attackReach;
                attackStackPending = false;

                if (stack)
                    spawnAttackStack();
                else
                    spawnGrain();
            }

            float sumL = 0.0f;
            float sumR = 0.0f;
            float sumSend = 0.0f;

            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                float raw = read (g.position);
                if (bitHoldN > 1)
                    raw = crushed (g, raw, bitHoldN);

                const float windowed = sprayed (g, bandedSample (g, raw)) * envelopeOf (g);

                sumL += windowed * g.gainL;
                sumR += windowed * g.gainR;

                if (wantSend)
                    sumSend += windowed * 0.5f * (g.gainL + g.gainR) * g.sendGain;

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

            // Cloud filter runs on the sum, before the feedback tap, so a
            // recirculating repeat is shaped again on the way round instead
            // of accumulating rumble or top-end untouched.
            const float filteredL = cloudLowpass (cloudHighpass (wetL, cloudHpX1L, cloudHpY1L), cloudLpZL);
            const float filteredR = cloudLowpass (cloudHighpass (wetR, cloudHpX1R, cloudHpY1R), cloudLpZR);

            outL[i] = filteredL;
            outR[i] = filteredR;

            if (wantSend)
                sendOut[i] = cloudLowpass (cloudHighpass (sumSend * smoothedNorm, cloudHpX1S, cloudHpY1S), cloudLpZS);

            // What goes back round next sample. tanh bounds it to (-1, 1)
            // whatever the cloud does, so the recirculation cannot build
            // without limit; a non-finite cloud feeds back nothing.
            const float cloudMono = 0.5f * (filteredL + filteredR);
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
        const float feedbackTail = timeMs * repeats * 0.001f;
        // Window (attackReachSeconds) can outlast the feedback-recirculation
        // estimate above on its own - the two are independent (see the class
        // note) - so the host-facing figure is whichever runs longer, plus
        // the grain-length margin either one needs.
        const float seconds =
            std::max (feedbackTail, attackReachSeconds) + sizeMs * static_cast<float> (kMaxRate + 1.0) * 0.001f;
        return std::min (seconds, kFrozenTailSeconds) + config::kCloudFilterSettleSeconds;
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

    /** For the tests - the semitone offsets setScale() currently allows in
        each direction, so the quantization can be checked directly rather
        than inferred from rendered audio. */
    int getUpCandidateCount() const noexcept { return upCount; }
    int getDownCandidateCount() const noexcept { return downCount; }
    float getUpCandidate (int i) const noexcept { return upCandidates[static_cast<size_t> (i)]; }
    float getDownCandidate (int i) const noexcept { return downCandidates[static_cast<size_t> (i)]; }

private:
    // Band split - see GrainerTuning::bandSplit. One grain per band per spawn,
    // each as long as its band's wavelengths need. Declared before Grain so
    // its `band` member can name kBandFull.
    static constexpr int kBandLow = 0;
    static constexpr int kBandMid = 1;
    static constexpr int kBandHigh = 2;
    static constexpr int kBandFull = 3;
    static constexpr int kNumSplitBands = 3;

    struct Grain
    {
        double position = 0.0; // fractional index into buffer
        double rate = 1.0;     // samples of source per sample of output; negative plays backwards
        int attackSamples = 1; // length of the fade-in

        // Smooth, as it stood when this grain was born; 0 leaves envelopeOf()
        // on exactly the path it had before Smooth existed. invDecayLen turns
        // the age past the fade-in into a 0..1 position along the decay.
        float smooth = 0.0f;
        float invDecayLen = 0.0f;
        float decayEnv = 1.0f; // running exponential, stepped once per sample
        float decayMul = 1.0f;

        // This grain's own decay offset and rescale, so windowJitter can give
        // it a curve of its own rather than the one every other grain wears.
        float envFloor = 0.0f;
        float envScale = 1.0f;
        float gainL = 0.0f;
        float gainR = 0.0f;
        int age = 0;
        int length = 0;
        bool active = false;

        // Bit's own sample-and-hold state, per grain - see crushed(). Reset
        // at spawn so a reused slot never carries over a previous grain's
        // held value or phase.
        int bitCounter = 0;
        float bitHeld = 0.0f;

        // Which band this grain carries, and its two crossover states - see
        // bandedSample(). kBandFull means the split is off and nothing filters.
        int band = kBandFull;
        float bandZ1 = 0.0f;
        float bandZ2 = 0.0f;

        // What this grain's interval is worth as a reverb send - see
        // GrainerTuning::pitchSendPerOctave. Scales the send bus only; the
        // main output never sees it.
        float sendGain = 1.0f;

        // Filter spray: this grain's own tilt, its one-pole and that pole's
        // state. Zero amount is an exact pass-through - see sprayed().
        float sprayAmount = 0.0f;
        float sprayAlpha = 0.0f;
        float sprayNorm = 1.0f;
        float sprayZ = 0.0f;
    };

    // A grain never exceeds this rate, which bounds how much source one spans
    // and therefore how far behind the write head it has to start. The scale
    // candidate table (see setScale()) tops out at +19 semitones (2.997x) for
    // exactly this reason - config::kMaxScaleSemitones matches this.
    static constexpr double kMaxRate = 3.2;

    // The other end. Not 1/kMaxRate: a slow grain reads *less* source than it
    // produces, so nothing about the read-ahead guard bounds it - this only
    // has to be low enough for the deepest interval setScale() offers, which
    // is -24 semitones.
    static constexpr double kMinRate = 0.25;

    static constexpr float kNormSmoothing = 0.0005f;
    static constexpr float kTwoPi = 6.28318530718f;

    // Generous: the chromatic scale over +/-19 semitones gives 19 members
    // each way, the most any scale choice can produce.
    static constexpr int kMaxScaleCandidates = 20;

    // A frozen buffer rings for ever; the host still wants a number.
    static constexpr float kFrozenTailSeconds = 30.0f;

    /** RMS of a Hann window, which is the envelope `outputTrim` was originally
        calibrated against. Keeping it as the reference means the trim still
        means what it used to, and only the envelope's own energy is divided
        out. sqrt(3/8). */
    static constexpr float kEnvelopeReferenceRms = 0.6124f;

    // Envelope follower feeding the attack detector, and the detector's own
    // voicing. The same numbers BitBit Wah's retrigger uses - a pluck is a pluck.
    static constexpr float kFollowerSeconds = 0.010f;
    static constexpr float kOnsetEnvDecayMs = 120.0f;
    static constexpr float kOnsetAttackWidthMs = 15.0f;
    static constexpr float kOnsetRiseRatioOn = 0.55f;
    static constexpr float kOnsetRiseRatioOff = 0.15f;
    static constexpr float kOnsetMinRise = 0.005f;
    static constexpr float kOnsetLockoutMs = 90.0f;
    static constexpr std::uint32_t kRngSeed = 0x9E3779B9u;

    // One envelope reading per this many samples of buffer. 64 is about
    // 1.5 ms - finer than the follower itself moves, so nothing is lost, and
    // it keeps the trace under 8k entries for the whole 10.5 s buffer.
    static constexpr int kEnvTraceDecim = 64;

    // How long the recent-loudness reference takes to forget a loud passage.
    // Long enough to hold across a phrase, short enough that a quiet piece
    // after a loud one is not measured against the loud one for ever.
    static constexpr float kLoudRefSeconds = 1.5f;

    // Below this the material is silence rather than a quiet note, and both
    // the source levelling and the density follow leave it alone: lifting it
    // would only bring up whatever the loop's own noise floor is. -80 dB.
    static constexpr float kLoudnessFloor = 1.0e-4f;

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
        {
            return static_cast<float> (g.age) / static_cast<float> (g.attackSamples);
        }

        float env = (g.decayEnv - g.envFloor) * g.envScale;
        g.decayEnv *= g.decayMul;

        if (g.smooth > 0.0f)
        {
            // Smooth blends the decay toward a straight line down. Both start
            // at 1 and land on 0, so any blend of them does too and the grain
            // still cannot click at either end.
            const float v = std::clamp (static_cast<float> (g.age - g.attackSamples) * g.invDecayLen, 0.0f, 1.0f);

            env += g.smooth * ((1.0f - v) - env);
        }

        return env > 0.0f ? env : 0.0f;
    }

    /** This grain's own tilt about the corner it drew at spawn - see
        GrainerTuning::filterSpray. A positive amount leans on the lowpass and
        comes out darker than the source; a negative one subtracts it, which
        alone would be a straight boost of everything above the corner rather
        than a tilt, so the bright side is divided back down to pivot about
        the corner instead of lifting past it. Amount 0 returns x untouched,
        which is the whole of the off path. */
    static float sprayed (Grain& g, float x) noexcept
    {
        if (g.sprayAmount == 0.0f)
            return x;

        g.sprayZ += g.sprayAlpha * (x - g.sprayZ);

        return (x + g.sprayAmount * (g.sprayZ - x)) * g.sprayNorm;
    }

    /** Bit's sample-and-hold, one grain's own state. Holds x for bitHoldN
        samples then re-samples - bitHoldN == 1 holds every sample, which is
        exactly x back out, so Bit at rest is a bit-exact pass-through the
        same way BitCrusher's own fast path is. */
    static float crushed (Grain& g, float x, int holdN) noexcept
    {
        if (g.bitCounter <= 0)
        {
            g.bitHeld = x;
            g.bitCounter = holdN;
        }
        --g.bitCounter;
        return g.bitHeld;
    }

    /** One-pole DC blocker (Smith's classic form) - the cloud's highpass.

        Squelches below 1e-20: a pole this close to 1 (60 Hz corner) never
        reaches an exact float32 zero through rounding alone once it is down
        in denormal territory - repeated multiplication by the coefficient
        rounds back to the same representable value and latches there
        (confirmed by simulation: still nonzero after 100+ seconds). The
        lowpass stage does not need this - its coefficient is far enough from
        1 to underflow to zero in well under a millisecond. */
    float cloudHighpass (float x, float& x1, float& y1) const noexcept
    {
        float y = x - x1 + cloudHpCoeff * y1;
        x1 = x;
        if (std::abs (y) < 1.0e-20f)
            y = 0.0f;
        y1 = y;
        return y;
    }

    /** Both cloud filter coefficients, from cloudFilterAmount. The sweep is
        exponential (a ratio raised to the amount) rather than linear in hertz,
        because a filter's travel only sounds even when it moves by octaves.
        Highpass as a DC blocker (Smith's one-pole: R relates to corner as
        fc ~= (1-R) * sr / 2*pi), lowpass as the usual one-pole. */
    void updateCloudFilter() noexcept
    {
        const float sr = static_cast<float> (sampleRate);

        // Fixed and hidden - the knob does not reach it, see setCloudFilter().
        const float hpHz = config::kCloudHighpassHz;

        // Geometric from open to closed, so the knob's bottom half is not all
        // crammed into the last few hundred Hz the way a linear sweep would
        // leave it.
        const float lpHz = config::kCloudLowpassHz *
                           std::pow (config::kCloudLowpassMinHz / config::kCloudLowpassHz, 1.0f - cloudFilterAmount);

        // Clamped: the highpass form above is a forward-Euler approximation
        // that only holds for a corner well under Nyquist, and a swept one
        // climbs far closer to it than the fixed 60 Hz ever did.
        cloudHpCoeff = std::clamp (1.0f - kTwoPi * hpHz / sr, 0.0f, 0.9999f);
        cloudLpCoeff = std::clamp (1.0f - std::exp (-kTwoPi * lpHz / sr), 0.0f, 1.0f);
    }

    /** One-pole lowpass - the cloud's lowpass, cascaded after the highpass. */
    float cloudLowpass (float x, float& z) const noexcept
    {
        z += cloudLpCoeff * (x - z);
        return z;
    }

    /** Whether transposing by `semitones` (either sign) lands on a member of
        `scale`, given `root` (0-11) is the semitone above unison the scale's
        own tonic sits at - see setScale()'s note on what that does and does
        not promise without pitch tracking. */
    static bool isScaleMember (const config::ScaleDegrees& scale, int semitones, int root) noexcept
    {
        const int pitchClass = ((semitones - root) % 12 + 12) % 12;
        for (int d = 0; d < scale.count; ++d)
            if (scale.degrees[d] == pitchClass)
                return true;
        return false;
    }

    /** Four-point Hermite read, the same interpolator ModDelayLine uses. Linear
        would lose the top octave off every backwards grain. */
    float read (double position) const noexcept
    {
        const int i1 = static_cast<int> (position);
        const float frac = static_cast<float> (position - static_cast<double> (i1));

        const int i0 = i1 > 0 ? i1 - 1 : size - 1;
        int i2 = i1 + 1;
        if (i2 >= size)
            i2 -= size;
        int i3 = i2 + 1;
        if (i3 >= size)
            i3 -= size;

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
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

    /** The spawn phase always free-runs (see the per-sample loop); this only
        nudges it onto the host grid while Density is synced to a running
        transport. A hard snap on the first playing block or a transport jump
        (loop / relocate), otherwise a gentle per-block pull capped small - the
        same reasoning as ee::dsp::Tremolo::alignToTransport, applied to a spawn
        instant instead of an LFO phase.

        Returns true when a snap has landed (to within one sample) right on the
        beat Density is synced to - starting or relocating exactly on a bar - so
        the caller can drop a grain there immediately rather than leaving the
        first cycle silent while spawnPhase counts up from the top all over
        again. A snap that lands mid-cycle (an ordinary relocate) does not: the
        next grain is still wherever the grid's next boundary falls. */
    bool alignSpawnToTransport (const Transport& transport, int numSamples, double phaseInc) noexcept
    {
        bool spawnOnArrival = false;

        if (transport.synced)
        {
            double target = transport.ppqStart * transport.cyclesPerQuarter;
            target -= std::floor (target);

            const bool jumped =
                ! wasSpawnPlaying ||
                (haveExpectedSpawnPpq && std::abs (transport.ppqStart - expectedSpawnPpq) > config::kSpawnJumpPpq);

            if (jumped)
            {
                spawnPhase = target;
                spawnOnArrival = phaseInc > 0.0 && target < phaseInc;
            }
            else
            {
                double err = target - spawnPhase;
                err -= std::round (err); // wrap to [-0.5, 0.5]
                spawnPhase += std::clamp (config::kSpawnPhasePullFraction * err, -config::kSpawnPhasePullMax,
                                          config::kSpawnPhasePullMax);
            }

            expectedSpawnPpq = transport.ppqStart + numSamples * transport.ppqPerSample;
            haveExpectedSpawnPpq = true;
        }
        else
        {
            haveExpectedSpawnPpq = false; // next playing block re-aligns from scratch
        }

        wasSpawnPlaying = transport.playing;

        if (! std::isfinite (spawnPhase))
            spawnPhase = 0.0;

        return spawnOnArrival;
    }

    int nextInterval() noexcept
    {
        const float jitterFrac = scatter * tuning.scatterMaxJitter;
        const float nominal = static_cast<float> (sampleRate) / (densityHz * densityFollowFactor());
        const float jittered = nominal * (1.0f + jitterFrac * nextBipolar());

        return std::max (1, static_cast<int> (jittered));
    }

    /** Instantly, what the input's own envelope is doing to the spawn rate -
        see GrainerTuning::densityFollow. Measured against how loud the
        material has been lately rather than against any absolute level, so it
        answers the same whatever the input gain is. */
    float densityFollowFactor() const noexcept
    {
        const float amount = std::clamp (tuning.densityFollow, 0.0f, 1.0f);

        if (amount <= 0.0f || loudRef <= kLoudnessFloor)
            return 1.0f;

        const float floor = std::clamp (tuning.densityFollowFloor, 0.01f, 1.0f);
        const float env = std::max (floor, std::clamp (follower / loudRef, 0.0f, 1.0f));

        return 1.0f - amount * (1.0f - env);
    }

    /** How loud the material has been lately: the envelope follower's own
        peaks, held and released slowly. Everything that has to be level
        independent measures against this. */
    void updateLoudRef() noexcept
    {
        loudRef = follower > loudRef ? follower : follower + loudRefCoeff * (loudRef - follower);
    }

    /** How far a grain starting here is lifted towards that reference - see
        GrainerTuning::sourceLevelling. Lift only, and silence is left where it
        is. */
    float sourceLevelGain (double position) const noexcept
    {
        const float amount = std::clamp (tuning.sourceLevelling, 0.0f, 1.0f);

        if (amount <= 0.0f || envTraceSize <= 0 || loudRef <= kLoudnessFloor)
            return 1.0f;

        const int index = std::clamp (static_cast<int> (position) / kEnvTraceDecim, 0, envTraceSize - 1);
        const float here = envTrace[static_cast<size_t> (index)];

        if (here <= kLoudnessFloor)
            return 1.0f;

        const float ratio = std::clamp (loudRef / here, 1.0f, std::max (1.0f, tuning.sourceLevelMaxBoost));

        return std::pow (ratio, amount);
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

        // Where a decay of this shape would land at the end of a grain. Each
        // grain rescales itself off its own jittered exponent (see
        // startVoice); this is only the input to the energy estimate below.
        decayFloor = std::exp (-curDecayShape);

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
        const float meanSquare = ((1.0f - f * f) / (2.0f * k) - 2.0f * f * (1.0f - f) / k + f * f) /
                                 std::max (1.0e-6f, (1.0f - f) * (1.0f - f));
        float envelopeRms = std::sqrt (std::max (1.0e-6f, meanSquare));

        // Smooth changes how much energy the window holds, and a longer fade-in
        // is exactly the kind of change that would otherwise turn the knob into
        // a volume control. No closed form for the blend, so it is measured -
        // and only when Smooth or the decay it blends against actually moved,
        // since this runs every chunk.
        if (smooth > 0.0f)
        {
            if (smooth != smoothRmsFor || curDecayShape != smoothRmsDecay || tuning.smoothPeak != smoothRmsPeak)
            {
                smoothRmsFor = smooth;
                smoothRmsDecay = curDecayShape;
                smoothRmsPeak = tuning.smoothPeak;
                smoothRms = smoothedEnvelopeRms (smooth, k, f);
            }

            envelopeRms = smoothRms;
        }

        // Per-grain level jitter (see spawnGrain) scales every grain by a
        // random 1 - j*u, u uniform in [0,1). That distribution's RMS is
        // sqrt(E[L^2]) with E[L^2] = 1 - j + j^2/3, divided back out here so
        // the cloud sits at the same level whatever the jitter is - the same
        // move as dividing out the envelope's own RMS above.
        const float j = levelJitterNow();
        const float levelRms = std::sqrt (std::max (1.0e-6f, 1.0f - j + j * j / 3.0f));

        normTarget = tuning.outputTrim * (kEnvelopeReferenceRms / envelopeRms) / (std::sqrt (overlap) * levelRms);

        // Band split. Lengths scale with wavelength - the low band gets the
        // ratio times the Size knob, the high band that much less - and the
        // per-band gain divides out the overlap that length change brings with
        // it, so turning the split up is level-neutral rather than a tilt EQ.
        const float split = bandSplitNow();
        const float ratio = 1.0f + split * (std::max (1.0f, tuning.bandLengthRatio) - 1.0f);

        bandLengthScale[kBandLow] = ratio;
        bandLengthScale[kBandMid] = 1.0f;
        bandLengthScale[kBandHigh] = 1.0f / ratio;

        for (int b = 0; b < kNumSplitBands; ++b)
            bandGain[b] = 1.0f / std::sqrt (bandLengthScale[b]);

        bandLowCoeff = onePoleCoeff (std::min (tuning.bandLowHz, tuning.bandHighHz));
        bandHighCoeff = onePoleCoeff (std::max (tuning.bandLowHz, tuning.bandHighHz));

        updateIntervalNorm();
    }

    /** RMS of the window at this Smooth, over a grain's whole length: a linear
        fade-in over the first `peak * s` of it (the short fixed attack is
        ignored, as in the closed form above) and, after it, the decay blended
        with a straight line down. Midpoint rule, 64 slices. */
    float smoothedEnvelopeRms (float s, float k, float f) const noexcept
    {
        constexpr int kSlices = 64;

        const float attackFrac = std::clamp (std::clamp (tuning.smoothPeak, 0.05f, 0.95f) * s, 0.01f, 0.95f);
        float sum = 0.0f;

        for (int i = 0; i < kSlices; ++i)
        {
            const float u = (static_cast<float> (i) + 0.5f) / static_cast<float> (kSlices);
            float env;

            if (u < attackFrac)
            {
                env = u / attackFrac;
            }
            else
            {
                const float v = (u - attackFrac) / (1.0f - attackFrac);
                const float decay = (std::exp (-k * v) - f) / std::max (1.0e-6f, 1.0f - f);

                env = decay + s * ((1.0f - v) - decay);
            }

            sum += env * env;
        }

        return std::sqrt (std::max (1.0e-6f, sum / static_cast<float> (kSlices)));
    }

    /** The per-grain level jitter and the band split as Smooth leaves them:
        both fade out linearly and are gone at Smooth 1 (see setSmooth). At
        Smooth 0 they are exactly the tuning's own figures. */
    float levelJitterNow() const noexcept { return tuning.grainLevelJitter * (1.0f - smooth); }
    float bandSplitNow() const noexcept { return std::clamp (tuning.bandSplit, 0.0f, 1.0f) * (1.0f - smooth); }

    /** One-pole lowpass coefficient for a corner in Hz. */
    float onePoleCoeff (float hz) const noexcept
    {
        const float f = std::clamp (hz, 20.0f, static_cast<float> (sampleRate) * 0.45f);
        return std::exp (-kTwoPi * f / static_cast<float> (sampleRate));
    }

    /** The share of `x` this grain's band carries.

        Complementary one-poles, so the three grains of one spawn event sum
        back to the unsplit grain. Gentle slopes on purpose: a steeper filter
        rings for longer than a short high-band grain lasts, which would put
        back exactly the smear the split exists to remove. */
    float bandedSample (Grain& g, float x) const noexcept
    {
        if (g.band == kBandFull)
            return x;

        g.bandZ1 += (1.0f - bandLowCoeff) * (x - g.bandZ1);

        if (g.band == kBandLow)
            return g.bandZ1;

        const float aboveLow = x - g.bandZ1;
        g.bandZ2 += (1.0f - bandHighCoeff) * (aboveLow - g.bandZ2);

        return g.band == kBandMid ? g.bandZ2 : aboveLow - g.bandZ2;
    }

    /** Picks a playback rate: one of the three pitch groups in proportion to
        their weights, then (for Low/High) a semitone offset uniformly at
        random from that direction's scale-quantized candidate table - see
        setScale(). */
    double pickRate() noexcept
    {
        float semitones = 0.0f;

        const float total = pitchLow + pitchUnison + pitchHigh;

        if (total > 0.0f)
        {
            const float pick = nextFloat() * total;

            if (pick < pitchLow)
            {
                if (downCount > 0)
                    semitones = downCandidates[static_cast<size_t> (
                        std::min (downCount - 1, static_cast<int> (nextFloat() * static_cast<float> (downCount))))];
            }
            else if (pick >= pitchLow + pitchUnison)
            {
                semitones = pickHighSemitones();
            }
        }

        return rateForSemitones (semitones);
    }

    /** The High group's interval for one grain. The octave up is where this
        group lives; the scale is a colour laid over it, dialled by
        setScaleBlend(). The octave is the fallback rather than unison so that
        closing the blend (or switching Scale off) still transposes - High
        reading as unison was the bug that made the knob inaudible. */
    float pickHighSemitones() noexcept
    {
        if (upCount > 0 && nextFloat() < scaleBlend)
            return upCandidates[static_cast<size_t> (
                std::min (upCount - 1, static_cast<int> (nextFloat() * static_cast<float> (upCount))))];
        return 12.0f;
    }

    /** Semitones -> playback rate, with Mod's drift on top. */
    double rateForSemitones (float semitones) const noexcept
    {
        // Mod's drift: sampled from the shared slow phase, not this grain's
        // own RNG, so every grain spawned near the same point in the cycle
        // bends the same way - see setMod()'s note. Cents, not semitones -
        // this is a wobble on top of whatever note was picked, not a second
        // interval choice.
        float cents = 0.0f;
        if (modAmount > 0.0f)
            cents = modAmount * config::kModMaxCents * std::sin (kTwoPi * static_cast<float> (wowPhase));

        const double ratio = std::pow (2.0, (static_cast<double> (semitones) + cents * 0.01) / 12.0);

        return std::clamp (ratio, kMinRate, kMaxRate);
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
        // Grain length, strayed from Size by Scatter.
        float lengthF = sizeMs * 0.001f * static_cast<float> (sampleRate);
        lengthF *= 1.0f + scatter * tuning.scatterSizeJitter * nextBipolar();
        const int length = std::clamp (static_cast<int> (lengthF), minGrainSamples, maxGrainSamples);

        const bool backwards = nextFloat() < reverse;
        const double rate = pickRate();

        double position = 0.0;

        // -1 means frozen: the buffer is static, so every position holds real
        // content and a longer band grain has nothing to run into.
        int guardOffset = -1;

        if (frozen && ! capturing && ! barLocked)
        {
            // Scan position, scattered a little either side.
            const double jitter = static_cast<double> (scatter) * static_cast<double> (timeOffsetSamples) * 0.5 *
                                  static_cast<double> (nextBipolar());

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

            if (barLocked)
            {
                // Grid, frozen: counted back to this bar's line, then forward
                // by the sixteenth Scatter picks. A slice still in the future
                // clamps to minOffset below, which reads it live.
                const float choices = scatter * static_cast<float> (config::kGridMaxSlices) + 1.0f;
                const int slice = std::min (config::kGridMaxSlices, static_cast<int> (nextFloat() * choices));
                const double back = samplesIntoBar - static_cast<double> (slice) * samplesPerSixteenth;

                offset = static_cast<int> (std::lround (std::clamp (back, -1.0, static_cast<double> (size))));
            }
            else
            {
                const int spread = static_cast<int> (scatter * static_cast<float> (timeOffsetSamples) * 0.5f);

                if (liveGrid)
                {
                    // Grid, live: the tap in whole sixteenths, never less than
                    // one, and Scatter as a count of sixteenths either side -
                    // the same kGridMaxSlices scale the frozen branch uses, so
                    // the knob answers across its whole travel whatever the
                    // tempo or Time. The attack grains below keep their own
                    // timing on purpose.
                    const double step = samplesPerSixteenth;
                    double tap = std::max (1.0, std::round (static_cast<double> (timeOffsetSamples) / step));
                    if (scatter > 0.0f)
                        tap = std::max (1.0, tap + std::round (static_cast<double> (nextBipolar() * scatter) *
                                                               config::kGridMaxSlices));

                    offset = static_cast<int> (std::lround (std::min (tap * step, static_cast<double> (size))));
                }
                else if (spread > 0)
                {
                    offset += static_cast<int> (nextBipolar() * static_cast<float> (spread));
                }

                // Most grains come from the last attack, if there was one recently
                // enough that the note is still ringing. That is what keeps the
                // cloud sounding like the note that was struck rather than like its
                // sustain - but it lapses after attackReachSeconds (Window) so a
                // long silence really does fall silent.
                const int attackReach = static_cast<int> (attackReachSeconds * static_cast<float> (sampleRate));

                if (attackIndex >= 0 && sinceAttack <= attackReach && sinceAttack <= maxOffset &&
                    nextFloat() < tuning.attackShare * (1.0f - tuning.smoothAttackShareCut * smooth))
                {
                    // The attack is the anchor; Scatter says how far past it into
                    // the note this particular grain starts. Without a spread here
                    // every attack grain starts on the very same sample, because
                    // sinceAttack advances in lockstep with the write head - see
                    // GrainerConfig.h's note on this window for why that is the
                    // difference between a cloud and a stutter.
                    const float windowMs = config::kAttackJitterMs + scatter * config::kAttackSpreadMs;
                    const int windowSamples = static_cast<int> (windowMs * 0.001f * static_cast<float> (sampleRate));
                    const int preRoll =
                        static_cast<int> (config::kAttackPreRollMs * 0.001f * static_cast<float> (sampleRate));

                    // Subtracting walks *forward* into the note: offset counts back
                    // from the write head, so a smaller one is later audio.
                    const int into =
                        windowSamples > 0 ? static_cast<int> (nextFloat() * static_cast<float> (windowSamples)) : 0;
                    const int wanted = sinceAttack + preRoll - into;

                    if (wanted >= minOffset && wanted <= maxOffset)
                        offset = wanted;
                }
            }

            offset = std::clamp (offset, minOffset, maxOffset);
            guardOffset = offset;

            position = static_cast<double> (writeIndex - offset);
            if (position < 0.0)
                position += static_cast<double> (size);
        }

        const float pan = nextBipolar() * stereo;

        // Per-grain level, folded into the pan gains rather than carried as a
        // field of its own. Downward only, so the loudest grain is no louder
        // than it was before this existed; updateDerived() divides the
        // distribution's own RMS back out, so dialling it in does not quieten
        // the cloud.
        const float level = 1.0f - levelJitterNow() * nextFloat();

        emitGrains (position, backwards ? -rate : rate, length, pan, level, guardOffset, backwards);
    }

    /** The first grains after a struck note, in place of one random grain -
        see GrainerConfig.h's ATTACK OCTAVES. Every voice starts together, at
        the attack, forwards, with one shared length, pan and level. */
    void spawnAttackStack() noexcept
    {
        float semitones[3];
        int voices = 0;

        semitones[voices++] = downCount > 0 ? downCandidates[0] : -12.0f;
        if (pitchHigh > 0.0f)
        {
            if (pitchUnison > 0.0f)
                semitones[voices++] = 0.0f;
            semitones[voices++] = pickHighSemitones();
        }

        float lengthF = sizeMs * 0.001f * static_cast<float> (sampleRate);
        lengthF *= 1.0f + scatter * tuning.scatterSizeJitter * nextBipolar();
        const int length = std::clamp (static_cast<int> (lengthF), minGrainSamples, maxGrainSamples);

        // Same anchor as an attack-drawn grain in spawnGrain(), drawn once so
        // every voice starts on the same sample of the note.
        const float windowMs = config::kAttackJitterMs + scatter * config::kAttackSpreadMs;
        const int windowSamples = static_cast<int> (windowMs * 0.001f * static_cast<float> (sampleRate));
        const int preRoll = static_cast<int> (config::kAttackPreRollMs * 0.001f * static_cast<float> (sampleRate));
        const int into = windowSamples > 0 ? static_cast<int> (nextFloat() * static_cast<float> (windowSamples)) : 0;
        const int wanted = sinceAttack + preRoll - into;

        const float pan = nextBipolar() * stereo;
        const float level = 1.0f - levelJitterNow() * nextFloat();

        const int margin = config::kGrainReadMarginSamples;

        for (int v = 0; v < voices; ++v)
        {
            const double rate = rateForSemitones (semitones[v]);
            int voiceLength = length;

            // Faster than realtime, a grain this close to the write head would
            // catch it up and read audio not yet recorded. Shorten it to what
            // the attack has actually delivered so far rather than moving it
            // back before the attack; only if even the shortest grain will not
            // fit does the clamp below move it.
            if (rate > 1.0)
            {
                const double fits = static_cast<double> (wanted - 2 * margin) / (rate - 1.0);
                voiceLength = std::clamp (static_cast<int> (fits), minGrainSamples, length);
            }

            const int consumed = static_cast<int> (std::ceil (rate * voiceLength)) + margin;
            const int minOffset = std::max (margin, consumed - voiceLength + margin);
            const int maxOffset = size - voiceLength - margin;

            if (maxOffset <= minOffset)
                continue;

            const int offset = std::clamp (wanted, minOffset, maxOffset);

            double position = static_cast<double> (writeIndex - offset);
            if (position < 0.0)
                position += static_cast<double> (size);

            emitGrains (position, rate, voiceLength, pan, level, offset, false);
        }
    }

    /** A free grain, or - pool full - the one nearest its own end, which is the
        one whose window is quietest and so the least audible to cut short. */
    Grain* claimSlot() noexcept
    {
        for (auto& g : grains)
            if (! g.active)
                return &g;

        Grain* slot = nullptr;
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
        return slot;
    }

    void startVoice (Grain& slot,
                     double position,
                     double rate,
                     int length,
                     float pan,
                     float level,
                     int band,
                     float bandLevel,
                     float sendGain) noexcept
    {
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;

        slot.position = position;
        slot.rate = rate;
        slot.length = length;
        slot.age = 0;

        // This grain's own window, strayed either side of the pair Shape set -
        // see GrainerTuning::windowJitter. The decay exponent strays by a
        // ratio rather than by an amount, so the same jitter means the same
        // thing at both ends of the Shape travel.
        float attackMs = curAttackMs;
        float decayShape = curDecayShape;

        // Smooth fades the stray out: a swell that is different every time is
        // not the swell it is meant to be.
        const float windowStray = tuning.windowJitter * (1.0f - smooth);

        if (windowStray > 0.0f)
        {
            const float j = std::clamp (windowStray, 0.0f, 1.0f);

            attackMs = std::max (0.1f, attackMs * (1.0f + j * nextBipolar()));
            decayShape = std::clamp (decayShape * std::exp (j * nextBipolar()), 0.25f, 12.0f);
        }

        // Just enough fade-in not to click, and never more than half the grain -
        // a 20 ms grain cannot afford a 5 ms attack.
        // Smooth lengthens that fade-in until the peak sits smoothPeak of the way
        // through the grain, and 0 leaves the figure exactly as it was. The cap
        // is half the grain otherwise (a 20 ms grain cannot afford a 5 ms
        // attack) and the swell's own peak position with Smooth on.
        float attackWanted = attackMs * 0.001f * static_cast<float> (sampleRate);
        int attackCap = std::max (1, length / 2);

        if (smooth > 0.0f)
        {
            const float peak = std::clamp (tuning.smoothPeak, 0.05f, 0.95f);

            attackWanted += smooth * (peak * static_cast<float> (length) - attackWanted);
            attackCap = std::max (attackCap, static_cast<int> (peak * static_cast<float> (length)));
        }

        const int attackSamples = std::clamp (static_cast<int> (attackWanted), 1, attackCap);

        slot.attackSamples = attackSamples;
        slot.smooth = smooth;
        slot.invDecayLen = 1.0f / static_cast<float> (std::max (1, length - attackSamples));
        slot.decayEnv = 1.0f;
        slot.decayMul = std::exp (-decayShape / static_cast<float> (std::max (1, length - attackSamples)));

        // Offset and rescaled so this grain's curve starts at exactly 1 and
        // lands on exactly 0 whatever its own exponent turned out to be.
        slot.envFloor = std::exp (-decayShape);
        slot.envScale = 1.0f / std::max (1.0e-6f, 1.0f - slot.envFloor);

        slot.gainL = std::cos (angle) * level * bandLevel;
        slot.gainR = std::sin (angle) * level * bandLevel;
        slot.active = true;

        // Fresh hold state, so a reused slot's Bit crush starts from this
        // grain's own first sample rather than wherever the previous grain
        // that lived in this slot left its counter.
        slot.bitCounter = 0;
        slot.bitHeld = 0.0f;

        slot.band = band;
        slot.bandZ1 = 0.0f;
        slot.bandZ2 = 0.0f;

        slot.sendGain = sendGain;

        // Filter spray: a tilt either way and a corner drawn log-uniformly, so
        // the span reads as evenly covered rather than crowded at its top.
        slot.sprayZ = 0.0f;
        slot.sprayAmount = 0.0f;

        if (tuning.filterSpray > 0.0f)
        {
            const float lo = std::max (20.0f, std::min (tuning.filterSprayLowHz, tuning.filterSprayHighHz));
            const float hi = std::max (lo * 1.01f, std::max (tuning.filterSprayLowHz, tuning.filterSprayHighHz));

            slot.sprayAmount = std::clamp (tuning.filterSpray, 0.0f, 1.0f) * nextBipolar();
            slot.sprayAlpha = 1.0f - onePoleCoeff (lo * std::pow (hi / lo, nextFloat()));
            slot.sprayNorm = 1.0f / (1.0f + std::max (0.0f, -slot.sprayAmount));
        }
    }

    /** The longest a grain may be at this rate and offset without its read
        head running into audio not yet written (forwards) or off the oldest
        end of the buffer (backwards) - the same guard spawnGrain() applies
        when it picks an offset, restated so the band split can lengthen a
        grain after the fact. A negative offset means a frozen buffer, where
        every position holds real content and nothing needs guarding. */
    int lengthThatFits (int wanted, double rate, int offset, bool backwards) const noexcept
    {
        double limit = static_cast<double> (wanted);

        if (offset >= 0)
        {
            const double margin = static_cast<double> (config::kGrainReadMarginSamples);
            const double absRate = std::abs (rate);

            if (backwards)
            {
                limit = std::min (limit, (static_cast<double> (size - offset) - 2.0 * margin) / (1.0 + absRate));
            }
            else
            {
                limit = std::min (limit, static_cast<double> (size - offset) - margin);

                if (absRate > 1.0)
                    limit = std::min (limit, (static_cast<double> (offset) - 2.0 * margin) / (absRate - 1.0));
            }
        }

        return std::clamp (static_cast<int> (limit), minGrainSamples, maxGrainSamples);
    }

    /** One spawn event. Either a single full-range grain, or - with the band
        split dialled in - one grain per band from the *same* read position,
        each the length its band's wavelengths need. Sharing the position is
        what keeps a transient landing as one hit rather than as three
        separate effects. */
    void
    emitGrains (double position, double rate, int length, float pan, float level, int offset, bool backwards) noexcept
    {
        // Every interval weighting is read back out of the rate rather than
        // taken from the semitones that produced it, so Mod's drift counts and
        // one calculation covers both spawn paths.
        const float octaves = std::log2 (static_cast<float> (std::max (1.0e-6, std::abs (rate))));

        const float tiltedLevel = level * intervalGain (octaves) * intervalGainNormInv * sourceLevelGain (position);
        const float tiltedPan = pan * intervalPanScale (octaves);
        const float send = intervalSendGain (octaves);

        if (bandSplitNow() <= 0.0f)
        {
            if (Grain* slot = claimSlot())
                startVoice (*slot, position, rate, length, tiltedPan, tiltedLevel, kBandFull, 1.0f, send);
            return;
        }

        for (int band = 0; band < kNumSplitBands; ++band)
        {
            const int wanted = static_cast<int> (static_cast<float> (length) * bandLengthScale[band]);
            const int fitted = lengthThatFits (wanted, rate, offset, backwards);

            if (Grain* slot = claimSlot())
                startVoice (*slot, position, rate, fitted, tiltedPan, tiltedLevel, band, bandGain[band], send);
        }
    }

    /** How much louder this grain is for being transposed. Deliberately not
        compensated anywhere: moving the balance between the pitch groups is
        the whole point of it. */
    float intervalGain (float octaves) const noexcept
    {
        if (tuning.pitchGainDbPerOctave == 0.0f)
            return 1.0f;

        return std::clamp (std::pow (10.0f, tuning.pitchGainDbPerOctave * octaves * 0.05f), 0.25f, 4.0f);
    }

    /** What share of the pan position Stereo picked this grain keeps. An
        octave down holds half of it and two octaves none, so the sub of the
        cloud sits in the middle and survives a mono fold-down; unison and
        anything above keep the full width. */
    float intervalPanScale (float octaves) const noexcept
    {
        const float spread = std::clamp (tuning.pitchPanSpread, 0.0f, 1.0f);

        if (spread <= 0.0f)
            return 1.0f;

        return 1.0f + spread * (std::clamp (1.0f + octaves * 0.5f, 0.0f, 1.0f) - 1.0f);
    }

    /** This grain's weight on the send bus. */
    float intervalSendGain (float octaves) const noexcept
    {
        if (tuning.pitchSendPerOctave == 0.0f)
            return 1.0f;

        return std::clamp (1.0f + tuning.pitchSendPerOctave * octaves, 0.0f, 4.0f);
    }

    /** RMS of the interval gain over the pitch groups as they are currently
        weighted, divided back out at spawn.

        Without it the tilt is a volume control as well: every grain of an
        all-Low cloud is transposed the same way, so the whole cloud simply
        arrives 6 dB louder and the recirculation runs away with it
        (ee_grain_stress catches exactly that). Dividing the distribution's own
        RMS out - the same move grainLevelJitter gets in updateDerived() -
        leaves the tilt doing the one thing it is for: changing how the groups
        sit against each other when more than one of them is sounding. */
    void updateIntervalNorm() noexcept
    {
        if (tuning.pitchGainDbPerOctave == 0.0f)
        {
            intervalGainNormInv = 1.0f;
            return;
        }

        const float total = pitchLow + pitchUnison + pitchHigh;
        const float wLow = total > 0.0f ? pitchLow / total : 0.0f;
        const float wHigh = total > 0.0f ? pitchHigh / total : 0.0f;
        const float wUnison = total > 0.0f ? pitchUnison / total : 1.0f;

        const auto meanPower =
            [this] (const std::array<float, kMaxScaleCandidates>& candidates, int count, float fallbackOctaves)
        {
            if (count <= 0)
            {
                const float g = intervalGain (fallbackOctaves);
                return g * g;
            }

            float sum = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const float g = intervalGain (candidates[static_cast<size_t> (i)] / 12.0f);
                sum += g * g;
            }
            return sum / static_cast<float> (count);
        };

        float power = wUnison;

        if (wLow > 0.0f)
            power += wLow * meanPower (downCandidates, downCount, -1.0f);

        if (wHigh > 0.0f)
        {
            // High takes the plain octave whenever the scale blend does not
            // land - see pickHighSemitones().
            const float octave = intervalGain (1.0f);
            power +=
                wHigh * (scaleBlend * meanPower (upCandidates, upCount, 1.0f) + (1.0f - scaleBlend) * octave * octave);
        }

        intervalGainNormInv = 1.0f / std::sqrt (std::max (1.0e-6f, power));
    }

    //==========================================================================

    double sampleRate = 44100.0;

    std::vector<float> buffer;
    int size = 0;
    int writeIndex = 0;
    double readHead = 0.0;

    std::array<Grain, config::kMaxGrains> grains {};
    int spawnCountdown = 1;

    // The synced spawn timer's own phase accumulator [0, 1) - see
    // alignSpawnToTransport(). Unused, and untouched, while free-running.
    double spawnPhase = 0.0;
    double expectedSpawnPpq = 0.0;
    bool haveExpectedSpawnPpq = false;
    bool wasSpawnPlaying = false;

    int minGrainSamples = 1;
    int maxGrainSamples = 1;
    int timeOffsetSamples = 1;

    float sizeMs = config::kDefaultGrainMs;
    float densityHz = config::kDefaultDensityHz;
    float timeMs = config::kDefaultTimeMs;
    float feedback = config::kDefaultFeedbackPct * 0.01f;
    float attackReachSeconds = config::kAttackReachSeconds;
    float stretch = config::kDefaultStretchPct * 0.01f;
    float shape = config::kDefaultShapePct * 0.01f;
    float smooth = config::kDefaultSmoothPct * 0.01f;

    // The last (smooth, decay) pair smoothedEnvelopeRms() was run for, and what
    // it said - see updateDerived().
    float smoothRmsFor = -1.0f;
    float smoothRmsDecay = -1.0f;
    float smoothRmsPeak = -1.0f;
    float smoothRms = 1.0f;
    float scatter = config::kDefaultScatterPct * 0.01f;
    float reverse = config::kDefaultReversePct * 0.01f;
    float stereo = config::kDefaultStereoPct * 0.01f;

    // Scale candidate tables - see setScale(). Sentinels of -1 so the first
    // call (made from prepare()) always populates them.
    std::array<float, kMaxScaleCandidates> upCandidates {};
    std::array<float, kMaxScaleCandidates> downCandidates {};
    int upCount = 0;
    int downCount = 0;
    int currentScaleIndex = -1;
    int currentRootSemitone = -1;

    // Pitch Mix - see setScaleBlend(). 0 is the plain octave, which is what an
    // engine driven without ever calling it should sound like.
    float scaleBlend = 0.0f;

    // Mod: the shared drift phase every spawning grain samples its pitch
    // bend from - see setMod()/pickRate().
    float modAmount = config::kDefaultModPct * 0.01f;
    double wowPhase = 0.0;

    // Bit: the per-grain sample-and-hold count at the current knob position.
    // 1 holds every sample, i.e. passes through unheld - see setBit().
    float bitAmount = config::kDefaultBitPct * 0.01f;
    int bitHoldN = 1;

    float pitchLow = config::kDefaultPitchLowPct;
    float pitchUnison = config::kDefaultPitchUnisonPct;
    float pitchHigh = config::kDefaultPitchHighPct;

    bool frozen = false;
    bool capturing = false;

    // Grid, from the last process() call's Transport, and where the grain
    // about to spawn sits in its bar - see GrainerConfig.h's GRID.
    bool barLocked = false;
    bool liveGrid = false;

    // Set by the onset detector, spent by the next spawn - see
    // GrainerConfig.h's ATTACK OCTAVES.
    bool attackStackPending = false;
    double samplesIntoBar = 0.0;
    double samplesPerSixteenth = 1.0;
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

    // Cloud filter (see GrainerConfig.h's CLOUD FILTER section): a highpass
    // then a lowpass, run once on the summed cloud rather than per grain.
    // Coefficients set once in prepare(); state per channel, reset with
    // everything else.
    float cloudHpCoeff = 0.0f;
    float cloudLpCoeff = 0.0f;
    // The Filter knob, 0..1. Open rather than closed, so an engine nobody has
    // called setCloudFilter() on sounds like one with the knob where it rests
    // - under the old bipolar meaning 0 was the resting pair, but here it is
    // the shut end of the sweep.
    float cloudFilterAmount = 1.0f;
    float cloudHpX1L = 0.0f, cloudHpY1L = 0.0f;
    float cloudHpX1R = 0.0f, cloudHpY1R = 0.0f;
    float cloudLpZL = 0.0f, cloudLpZR = 0.0f;

    // The send bus carries the same filter, so it is coloured like the cloud
    // it is a weighted copy of rather than arriving at the tank raw.
    float cloudHpX1S = 0.0f, cloudHpY1S = 0.0f;
    float cloudLpZS = 0.0f;

    float curDecayShape = 4.0f;
    float curAttackMs = 1.0f;
    float decayFloor = 0.0f;

    // Band split, derived in updateDerived() from the tuning fields.
    float bandLengthScale[kNumSplitBands] = { 1.0f, 1.0f, 1.0f };
    float bandGain[kNumSplitBands] = { 1.0f, 1.0f, 1.0f };
    float bandLowCoeff = 0.0f;
    float bandHighCoeff = 0.0f;

    // Reciprocal of the interval tilt's own RMS over the current pitch mix -
    // see updateIntervalNorm().
    float intervalGainNormInv = 1.0f;

    // How loud the buffer was at each decimated position, and how loud the
    // material has been lately - see sourceLevelGain() and
    // densityFollowFactor().
    std::vector<float> envTrace;
    int envTraceSize = 0;
    float loudRef = 0.0f;
    float loudRefCoeff = 0.0f;

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
