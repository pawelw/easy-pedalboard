#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "Tremolo.h"

namespace ee::dsp
{

/** One breakpoint in a Mod-tab LFO shape: `x` is a phase position in [0, 1)
    (points kept sorted ascending, cyclic - the segment after the last point
    runs back to the first, one cycle later), `y` is the value there in
    [-1, 1], `curve` shapes the OUTGOING segment from this point (0 = linear,
    negative = fast-start/ease-out, positive = slow-start/ease-in), and
    `hold` makes that segment stay flat at this point's `y` until the next
    point's `x`, then jump - what gives Square and Random their staircase
    look on the same evaluator the smooth shapes use.

    This is the audio-side twin of plugins/peak-grain/jsui/src/lfoShapes.js's
    breakpoint model and its evalBreakpoints() - kept numerically in step by
    hand, the same relationship packages/pedal-ui/src/lfo.js has with this
    header's own Lfo.h. */
struct LfoBreakpoint
{
    float x = 0.0f;
    float y = 0.0f;
    float curve = 0.0f;
    bool hold = false;
};

/** A breakpoint-shaped LFO for Peak Grain's Mod tab: a sparse, user-editable
    point list evaluated per sample, with the same phase-accumulation and
    tempo-sync alignment ee::dsp::Tremolo uses (copied from it rather than
    shared, since Tremolo's own phase machinery is private to its ducking/pan
    laws) - reusing its Transport struct rather than inventing a parallel one.

    The breakpoint list is set from the message thread (a UI edit, a preset
    load) and read from the audio thread every block; a juce::SpinLock guards
    the swap (libc++ on this toolchain has no std::atomic<std::shared_ptr<T>>
    specialization to reach for instead), held only long enough to copy or
    replace the pointer, never across the per-sample evaluation itself.

    Not real-time-allocating itself: advance() only ticks the phase and
    evaluates the currently-installed shape. */
class BreakpointLfo
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sr = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        lfoPhase = 0.0;
        expectedPpq = 0.0;
        haveExpectedPpq = false;
        wasPlaying = false;
        uiPhase.store (0.0f, std::memory_order_relaxed);
    }

    /** Message-thread only. Sorted and defaulted to a single flat point if
        handed an empty list, so evaluate() never has to special-case zero
        points on the audio thread. */
    void setBreakpoints (std::vector<LfoBreakpoint> points)
    {
        if (points.empty())
            points.push_back ({ 0.0f, 0.0f, 0.0f, false });

        std::sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.x < b.x; });
        auto next = std::make_shared<const std::vector<LfoBreakpoint>> (std::move (points));

        const juce::SpinLock::ScopedLockType lock (breakpointsLock);
        breakpoints = std::move (next);
    }

    /** One LFO cycle's length. The caller works this out from its own Rate
        knob and, when synced, the host tempo - same shape as
        ee::dsp::Tremolo::setPeriodSeconds. */
    void setPeriodSeconds (float seconds) noexcept { periodSeconds = juce::jmax (1.0e-4f, seconds); }

    /** Advances the phase by one block and re-evaluates the shape at the new
        phase - call once per processBlock. Audio-thread only. */
    void advance (int numSamples, const Tremolo::Transport& transport) noexcept
    {
        if (numSamples <= 0)
            return;

        double phaseInc = 1.0 / (static_cast<double> (periodSeconds) * sr);
        // Same guard as Tremolo::process: a non-finite or negative increment
        // would wedge the audio thread spinning the wrap below.
        if (! std::isfinite (phaseInc) || phaseInc < 0.0)
            phaseInc = 0.0;
        phaseInc = juce::jmin (phaseInc, 1.0);

        alignToTransport (transport, numSamples);
        healNonFinite();

        lfoPhase += phaseInc * static_cast<double> (numSamples);
        lfoPhase -= std::floor (lfoPhase);

        currentValueCache = evaluate (static_cast<float> (lfoPhase));
        uiPhase.store (static_cast<float> (lfoPhase), std::memory_order_relaxed);
    }

    /** The shape's value at the current phase, bipolar -1..1. Audio-thread
        only - Stage 3's modulation routing reads this. */
    float currentValue() const noexcept { return currentValueCache; }

    /** The current phase, for a UI playhead marker. Safe from any thread -
        backed by an atomic snapshot written once per advance(), the same
        pattern PeakGrainProcessor::lastKnownBpm uses for its own
        audio-thread-written, message-thread-read value. */
    float phase01() const noexcept { return uiPhase.load (std::memory_order_relaxed); }

private:
    /** Copied from ee::dsp::Tremolo::alignToTransport - same free-running-
        phase-plus-gentle-pull model, since this engine's tempo sync ought to
        feel like every other synced LFO in this codebase, not a bespoke one. */
    void alignToTransport (const Tremolo::Transport& transport, int numSamples) noexcept
    {
        if (transport.synced)
        {
            double target = transport.ppqStart * transport.cyclesPerQuarter;
            target -= std::floor (target);

            const bool jumped =
                ! wasPlaying || (haveExpectedPpq && std::abs (transport.ppqStart - expectedPpq) > kJumpPpq);

            if (jumped)
            {
                lfoPhase = target;
            }
            else
            {
                double err = target - lfoPhase;
                err -= std::round (err); // wrap to [-0.5, 0.5]
                lfoPhase += juce::jlimit (-kPhasePullMax, kPhasePullMax, kPhasePullFraction * err);
            }

            expectedPpq = transport.ppqStart + numSamples * transport.ppqPerSample;
            haveExpectedPpq = true;
        }
        else
        {
            haveExpectedPpq = false;
        }

        wasPlaying = transport.playing;
    }

    void healNonFinite() noexcept
    {
        if (! std::isfinite (lfoPhase))
            lfoPhase = 0.0;
    }

    /** curve in [-1, 1] shaping t in [0, 1]: 0 is linear, negative eases out
        (fast start), positive eases in (slow start) - see lfoShapes.js's own
        shapeT, which this mirrors exactly. */
    static float shapeT (float t, float curve) noexcept
    {
        if (curve == 0.0f)
            return t;
        const float k = 1.0f + std::abs (curve) * 4.0f;
        return curve > 0.0f ? std::pow (t, k) : 1.0f - std::pow (1.0f - t, k);
    }

    static float segmentValue (const LfoBreakpoint& p0, const LfoBreakpoint& p1, float phase01) noexcept
    {
        if (p0.hold)
            return p0.y;

        const float span = p1.x - p0.x;
        const float t = span <= 0.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, (phase01 - p0.x) / span);
        return p0.y + (p1.y - p0.y) * shapeT (t, p0.curve);
    }

    float evaluate (float phaseValue) const noexcept
    {
        std::shared_ptr<const std::vector<LfoBreakpoint>> points;
        {
            const juce::SpinLock::ScopedLockType lock (breakpointsLock);
            points = breakpoints;
        }

        if (points == nullptr || points->empty())
            return 0.0f;
        if (points->size() == 1)
            return (*points)[0].y;

        const float p = phaseValue - std::floor (phaseValue);

        for (size_t i = 0; i + 1 < points->size(); ++i)
        {
            const auto& p0 = (*points)[i];
            const auto& p1 = (*points)[i + 1];
            if (p >= p0.x && p <= p1.x)
                return segmentValue (p0, p1, p);
        }

        // Wrap segment: last point back to the first, one cycle later.
        const auto& last = points->back();
        auto wrapped = points->front();
        wrapped.x += 1.0f;
        const float wrappedP = p < points->front().x ? p + 1.0f : p;
        return segmentValue (last, wrapped, wrappedP);
    }

    // Same constants as TremoloConfig.h's kJumpPpq/kPhasePullFraction/
    // kPhasePullMax - this engine's sync feel is meant to match, not diverge.
    static constexpr double kJumpPpq = 0.25;
    static constexpr double kPhasePullFraction = 0.15;
    static constexpr double kPhasePullMax = 0.006;

    double sr = 44100.0;
    float periodSeconds = 1.0f;

    double lfoPhase = 0.0; // [0, 1) - audio-thread only
    double expectedPpq = 0.0;
    bool haveExpectedPpq = false;
    bool wasPlaying = false;

    float currentValueCache = 0.0f;
    std::atomic<float> uiPhase { 0.0f };

    mutable juce::SpinLock breakpointsLock;
    std::shared_ptr<const std::vector<LfoBreakpoint>> breakpoints {
        std::make_shared<const std::vector<LfoBreakpoint>> (std::vector<LfoBreakpoint> { { 0.0f, 0.0f, 0.0f, false } })
    };
};

} // namespace ee::dsp
