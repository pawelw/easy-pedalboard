#include "ee/dsp/SpaceReverb.h"

#include <cmath>

namespace ee::dsp
{
namespace
{
    constexpr float kRampSeconds = 0.05f;
    constexpr float kDelayRampSeconds = 0.30f;

    // Same guards as FdnReverb, for the same reasons: a finite runaway never
    // trips isfinite, so a level no tail can reach is treated as a NaN, and a
    // hard cap on what is written back into a line parks a divergent mode
    // instead of letting it climb.
    constexpr float kRunawayCeiling = 64.0f;
    constexpr float kLineStateCeiling = 48.0f;

    // Each input goes into every line at +/- this, which puts unit energy in.
    const float kInjectGain = 1.0f / std::sqrt (static_cast<float> (SpaceVoicing::kLines));
    const float kTapNorm = 1.0f / std::sqrt (static_cast<float> (SpaceVoicing::kLines));

    // Walsh rows of the Hadamard, as bit masks: the sign of line i in a
    // pattern is the parity of (i & mask). Four distinct non-zero rows, so the
    // mid and side go in orthogonally and the two outputs come out uncorrelated.
    // Mid and side rather than left and right: two orthogonal +/-1 patterns
    // cancel on half the lines when summed, so a centred source fed as L and R
    // reached only four lines, twice as hard - fewer, louder first echoes.
    constexpr int kInMidMask = 0b0111;
    constexpr int kInSideMask = 0b1011;
    constexpr int kOutLeftMask = 0b1101;
    constexpr int kOutRightMask = 0b1110;

    constexpr float walsh (int line, int mask) noexcept
    {
        int bits = line & mask;
        int parity = 0;
        while (bits != 0)
        {
            parity ^= (bits & 1);
            bits >>= 1;
        }
        return parity == 0 ? 1.0f : -1.0f;
    }

    inline void hadamard (float* v) noexcept
    {
        constexpr int n = SpaceVoicing::kLines;
        static_assert ((n & (n - 1)) == 0, "a Sylvester Hadamard needs a power of two");
        for (int stride = 1; stride < n; stride <<= 1)
            for (int i = 0; i < n; i += stride * 2)
                for (int j = i; j < i + stride; ++j)
                {
                    const float a = v[j];
                    const float b = v[j + stride];
                    v[j] = a + b;
                    v[j + stride] = a - b;
                }

        const float norm = 1.0f / std::sqrt (static_cast<float> (n));
        for (int i = 0; i < n; ++i)
            v[i] *= norm;
    }

    /** Bilinear-warped frequency, the variable the shelf is designed in. */
    inline float warped (float hz, double sampleRate) noexcept
    {
        const float fs = static_cast<float> (sampleRate);
        return std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, 0.49f * fs) / fs);
    }

    /** dB lost at warped frequency w by (1 + s/wz) / (1 + s/wp); iz = 1/wz. */
    inline float shelfLossDb (float w, float wp, float iz) noexcept
    {
        const float a = w / wp;
        const float b = w * iz;
        return 10.0f * std::log10 ((1.0f + a * a) / (1.0f + b * b));
    }

    inline float onePoleCoeff (float hz, double sampleRate) noexcept
    {
        const float w = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float> (sampleRate);
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    }

} // namespace

// ------------------------------------------------------------------------ Svf

void SpaceReverb::Svf::setCutoff (float hz, double sampleRate) noexcept
{
    g = std::tan (juce::MathConstants<float>::pi
                  * juce::jmin (hz, 0.45f * static_cast<float> (sampleRate)) / static_cast<float> (sampleRate));
}

void SpaceReverb::Svf::process (float x, float& lp, float& hp) noexcept
{
    constexpr float k = juce::MathConstants<float>::sqrt2; // Q = 1/sqrt(2)
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lp = v2;
    hp = x - k * v1 - v2;
}

// ---------------------------------------------------------------------- Shelf

void SpaceReverb::Shelf::design (float lowDb, float highDb, float lowHz, float highHz, double sampleRate) noexcept
{
    const float w1 = warped (lowHz, sampleRate);
    const float w2 = warped (highHz, sampleRate);

    highDb = juce::jmax (0.0f, highDb);
    lowDb = juce::jlimit (0.0f, highDb, lowDb);

    if (highDb < 1.0e-4f)
    {
        b0 = 1.0f;
        b1 = a1 = 0.0f;
        return;
    }

    // A first-order section's loss can rise by at most 6 dB an octave, so the
    // top point cannot sit further above the bottom one than that allows. Hold
    // the lower frequency - it is the more audible - and give ground at the top.
    const float reach = 20.0f * std::log10 (w2 / w1) - 0.5f;
    highDb = juce::jmin (highDb, lowDb + reach);

    // For a pole wp the zero follows from the top point exactly; the pole is
    // then bisected (in log) until the bottom point lands too. Lower pole,
    // flatter shelf, more loss at the bottom.
    const float wpMax = w2 / std::sqrt (std::pow (10.0f, highDb / 10.0f) - 1.0f);
    const float target = std::pow (10.0f, -highDb / 10.0f);
    const auto zeroFor = [w2, target] (float wp)
    {
        const float r = (1.0f + (w2 / wp) * (w2 / wp)) * target - 1.0f;
        return r > 0.0f ? std::sqrt (r) / w2 : 0.0f; // 1/wz
    };

    float lo = std::log (juce::jmin (wpMax * 0.999f, warped (5.0f, sampleRate)));
    float hi = std::log (wpMax * 0.999f);
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        const float wp = std::exp (mid);
        if (shelfLossDb (w1, wp, zeroFor (wp)) > lowDb)
            lo = mid;
        else
            hi = mid;
    }

    const float wp = std::exp (0.5f * (lo + hi));
    const float ip = 1.0f / wp;
    const float iz = zeroFor (wp);
    const float a0 = 1.0f + ip;
    b0 = (1.0f + iz) / a0;
    b1 = (1.0f - iz) / a0;
    a1 = (1.0f - ip) / a0;
}

float SpaceReverb::Shelf::lossDb (float hz, double sampleRate) const noexcept
{
    const float w = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float> (sampleRate);
    const float c = std::cos (w), sn = std::sin (w);
    // |b0 + b1 e^-jw|^2 / |1 + a1 e^-jw|^2
    const float nr = b0 + b1 * c, ni = -b1 * sn;
    const float dr = 1.0f + a1 * c, di = -a1 * sn;
    return 10.0f * std::log10 ((dr * dr + di * di) / juce::jmax (1.0e-20f, nr * nr + ni * ni));
}

// ----------------------------------------------------------------- lifecycle

void SpaceReverb::prepare (double sampleRate)
{
    sr = sampleRate;

    // Every line's buffer is sized for kMaxSize, whatever the current Size -
    // setSize below only ever moves the read/delay position within it, so
    // Size can move smoothly (dirty -> updateDerived) rather than needing a
    // reset the way a genuine buffer resize would.
    const float longestEarly = juce::jmax (voicing.earlySameMs, voicing.earlyLeftToRightMs, voicing.earlyRightToLeftMs);
    const float inputSeconds = (kMaxPredelayMs + longestEarly * kMaxSize + 10.0f) * 0.001f;
    inputL.prepare (sampleRate, inputSeconds);
    inputR.prepare (sampleRate, inputSeconds);

    for (int i = 0; i < SpaceVoicing::kFeedDiffusers; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        for (auto [ap, ms] : { std::pair { &feedDiffuserMid[idx], voicing.feedDiffuserMidMs[idx] },
                               std::pair { &feedDiffuserSide[idx], voicing.feedDiffuserSideMs[idx] } })
        {
            ap->prepare (sampleRate, (ms * kMaxSize + 2.0f) * 0.001f);
            ap->setCoefficient (voicing.feedDiffusion);
        }
    }

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        lines[idx].prepare (sampleRate, (voicing.lineMs[idx] * kMaxSize + voicing.modDepthMs + 5.0f) * 0.001f);
        lfoPhase[idx] = static_cast<float> (i) / static_cast<float> (kLines);
    }

    predelaySamples.reset (sampleRate, kDelayRampSeconds);
    lowCutSmooth.reset (sampleRate, kRampSeconds);
    highCutSmooth.reset (sampleRate, kRampSeconds);
    lowCutSmooth.setCurrentAndTargetValue (lowCutHz);
    highCutSmooth.setCurrentAndTargetValue (highCutHz);
    for (auto& f : lowCutFilter)
        f.setCutoff (lowCutHz, sampleRate);
    for (auto& f : highCutFilter)
        f.setCutoff (highCutHz, sampleRate);

    reset();

    dirty = true;
    updateDerived();
    predelaySamples.setCurrentAndTargetValue (predelaySamples.getTargetValue());
}

void SpaceReverb::reset()
{
    inputL.reset();
    inputR.reset();
    warmupSamples = 0;
    warmupLength = juce::jmax (1, static_cast<int> (0.01 * sr));
    for (auto& l : lines)
        l.reset();
    allpassState.fill (0.0f);
    bandwidthStateL = bandwidthStateR = 0.0f;
    for (auto& ap : feedDiffuserMid)
        ap.reset();
    for (auto& ap : feedDiffuserSide)
        ap.reset();
    for (auto& f : lineShelf)
        f.reset();
    for (auto& f : earlyShelf)
        f.reset();
    for (auto& f : lowCutFilter)
        f.reset();
    for (auto& f : highCutFilter)
        f.reset();
}

void SpaceReverb::setVoicing (const SpaceVoicing& newVoicing)
{
    voicing = newVoicing;
    prepare (sr);
}

// ------------------------------------------------------------------ controls

void SpaceReverb::setDecayTime (float seconds) noexcept
{
    seconds = juce::jlimit (kMinDecay, kMaxDecay, seconds);
    if (! juce::approximatelyEqual (seconds, decaySeconds))
    {
        decaySeconds = seconds;
        dirty = true;
    }
}

void SpaceReverb::setDamping (float amount01) noexcept
{
    amount01 = juce::jlimit (0.0f, 1.0f, amount01);
    if (! juce::approximatelyEqual (amount01, damping))
    {
        damping = amount01;
        dirty = true;
    }
}

void SpaceReverb::setSize (float scale) noexcept
{
    scale = juce::jlimit (kMinSize, kMaxSize, scale);
    if (! juce::approximatelyEqual (scale, sizeScale))
    {
        sizeScale = scale;
        dirty = true;
    }
}

void SpaceReverb::setPredelay (float ms) noexcept
{
    predelayMs = juce::jlimit (kMinPredelayMs, kMaxPredelayMs, ms);
    // Whole samples at rest, for the same reason as the echo taps; only a
    // Pre-delay on the move reads between them.
    predelaySamples.setTargetValue (std::round (static_cast<float> (predelayMs * 0.001 * sr)));
}

void SpaceReverb::setLowCut (float hz) noexcept
{
    lowCutHz = juce::jlimit (kMinLowCutHz, kMaxLowCutHz, hz);
    lowCutSmooth.setTargetValue (lowCutHz);
}

void SpaceReverb::setHighCut (float hz) noexcept
{
    highCutHz = juce::jlimit (kMinHighCutHz, kMaxHighCutHz, hz);
    highCutSmooth.setTargetValue (highCutHz);
}

// ------------------------------------------------------------------- derived

float SpaceReverb::dampAt (const SpaceVoicing::DampTable& t, float atDecaySeconds) const noexcept
{
    // Bilinear on log2(Decay / 0.5 s) and Damp, both mapped onto the table's
    // 0..4 index range.
    const float x = juce::jlimit (0.0f, 4.0f, std::log2 (atDecaySeconds / 0.5f));
    const float y = juce::jlimit (0.0f, 4.0f, damping * 4.0f);
    const int x0 = juce::jmin (3, static_cast<int> (x));
    const int y0 = juce::jmin (3, static_cast<int> (y));
    const float fx = x - static_cast<float> (x0);
    const float fy = y - static_cast<float> (y0);

    const auto at = [&t] (int r, int c) { return t[static_cast<size_t> (r)][static_cast<size_t> (c)]; };
    const float top = at (x0, y0) + (at (x0, y0 + 1) - at (x0, y0)) * fy;
    const float bottom = at (x0 + 1, y0) + (at (x0 + 1, y0 + 1) - at (x0 + 1, y0)) * fy;
    return juce::jmax (0.0f, top + (bottom - top) * fx);
}

void SpaceReverb::designTrip (TripFilter& filter, float seconds, float atDecaySeconds) const noexcept
{
    // Three pins - the two table frequencies and their geometric middle - held
    // by two first-order shelves in series; one shelf pinned at the ends bows
    // between them. The losses are kept rising with frequency, which is the
    // only shape a pair of high shelves can draw.
    const float lowHz = voicing.dampLowHz;
    const float highHz = voicing.dampHighHz;
    const float midHz = std::sqrt (lowHz * highHz);

    const float lowDb = dampAt (voicing.dampLow, atDecaySeconds) * seconds;
    const float midDb = juce::jmax (lowDb, dampAt (voicing.dampMid, atDecaySeconds) * seconds);
    const float highDb = juce::jmax (midDb, dampAt (voicing.dampHigh, atDecaySeconds) * seconds);

    // Alternate: the lower shelf takes the bottom and middle pins less what the
    // upper one already loses there, the upper one takes the top less what the
    // lower one loses there. Two rounds settle it well inside a tenth of a dB.
    filter.high.design (0.0f, 0.0f, midHz, highHz, sr);
    for (int round = 0; round < 2; ++round)
    {
        filter.low.design (juce::jmax (0.0f, lowDb - filter.high.lossDb (lowHz, sr)),
                           juce::jmax (0.0f, midDb - filter.high.lossDb (midHz, sr)), lowHz, midHz, sr);
        filter.high.design (0.0f, juce::jmax (0.0f, highDb - filter.low.lossDb (highHz, sr)), midHz, highHz, sr);
    }
}

void SpaceReverb::updateDerived() noexcept
{
    dirty = false;

    const float fs = static_cast<float> (sr);

    // The Decay scale, interpolated on the same log2 grid as the damping.
    const float x = juce::jlimit (0.0f, 4.0f, std::log2 (decaySeconds / 0.5f));
    const int x0 = juce::jmin (3, static_cast<int> (x));
    const float scale = voicing.decayScale[static_cast<size_t> (x0)]
                      + (voicing.decayScale[static_cast<size_t> (x0 + 1)] - voicing.decayScale[static_cast<size_t> (x0)])
                            * (x - static_cast<float> (x0));
    const float rt = decaySeconds * scale;

    for (int i = 0; i < kLines; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        // The trip really is this long at the current Size, so both the loss
        // filter and the per-trip gain are designed against the scaled time -
        // otherwise RT60 would drift off the Decay knob as Size moved.
        const float seconds = voicing.lineMs[idx] * 0.001f * sizeScale;
        lineSamples[idx] = seconds * fs;
        lineGain[idx] = std::pow (10.0f, -3.0f * seconds / rt);
        designTrip (lineShelf[idx], seconds, decaySeconds);
        lfoInc[idx] = voicing.lfoHz[idx] / fs;
    }

    for (int i = 0; i < SpaceVoicing::kFeedDiffusers; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        // Whole samples - a fractional Hermite read would dull the top.
        feedDiffuserMid[idx].setDelaySamples (
            juce::jmax (2.0f, std::round (voicing.feedDiffuserMidMs[idx] * 0.001f * fs * sizeScale)));
        feedDiffuserSide[idx].setDelaySamples (
            juce::jmax (2.0f, std::round (voicing.feedDiffuserSideMs[idx] * 0.001f * fs * sizeScale)));
    }

    // Whole samples, so the echoes are read exactly: a fractional Hermite read
    // takes ~2 dB off the top octave, which on a bare echo is audible.
    earlySamples[0] = std::round (voicing.earlySameMs * 0.001f * fs * sizeScale);
    earlySamples[1] = std::round (voicing.earlyLeftToRightMs * 0.001f * fs * sizeScale);
    earlySamples[2] = std::round (voicing.earlyRightToLeftMs * 0.001f * fs * sizeScale);
    for (auto& filter : earlyShelf)
        designTrip (filter, voicing.earlyDampTripMs * 0.001f, std::sqrt (2.0f * decaySeconds));

    modDepthSamples = voicing.modDepthMs * 0.001f * fs;
    bandwidthCoeff = onePoleCoeff (voicing.lateBandwidthHz, sr);
}

// ------------------------------------------------------------------- process

void SpaceReverb::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept
{
    if (dirty)
        updateDerived();

    const float earlyGain = voicing.earlyGain;
    const float lateGain = voicing.lateGain * kTapNorm
                         * std::pow (2.0f / decaySeconds, voicing.lateGainDecayExponent);

    for (int s = 0; s < numSamples; ++s)
    {
        // A non-finite sample stored in a feedback line recirculates forever;
        // scrub it on the way in.
        float xL = std::isfinite (inL[s]) ? inL[s] : 0.0f;
        float xR = std::isfinite (inR[s]) ? inR[s] : 0.0f;
        if (warmupSamples < warmupLength)
        {
            const float ramp = static_cast<float> (++warmupSamples) / static_cast<float> (warmupLength);
            xL *= ramp;
            xR *= ramp;
        }

        inputL.write (xL);
        inputR.write (xR);
        const float pre = predelaySamples.getNextValue();

        // Early: each input's own-side echo, then its crossed one.
        float eL = inputL.read (pre + earlySamples[0]) + inputR.read (pre + earlySamples[2]);
        float eR = inputR.read (pre + earlySamples[0]) + inputL.read (pre + earlySamples[1]);
        eL = earlyShelf[0].process (eL) * earlyGain;
        eR = earlyShelf[1].process (eR) * earlyGain;

        bandwidthStateL += bandwidthCoeff * (inputL.read (pre) - bandwidthStateL);
        bandwidthStateR += bandwidthCoeff * (inputR.read (pre) - bandwidthStateR);
        float feedMid = juce::MathConstants<float>::sqrt2 * 0.5f * (bandwidthStateL + bandwidthStateR);
        float feedSide = juce::MathConstants<float>::sqrt2 * 0.5f * (bandwidthStateL - bandwidthStateR);
        for (auto& ap : feedDiffuserMid)
            feedMid = ap.process (feedMid);
        for (auto& ap : feedDiffuserSide)
            feedSide = ap.process (feedSide);
        inputL.advance();
        inputR.advance();

        std::array<float, kLines> v {};
        float lateL = 0.0f, lateR = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const float mod = std::sin (lfoPhase[idx] * juce::MathConstants<float>::twoPi) * modDepthSamples;
            lfoPhase[idx] += lfoInc[idx];
            if (lfoPhase[idx] >= 1.0f)
                lfoPhase[idx] -= 1.0f;

            // Integer reads (exact - Hermite at zero fraction is the sample
            // itself) and a first-order allpass for the fraction, kept in
            // 0.1..1.1 so its coefficient stays well inside the unit circle.
            const float total = lineSamples[idx] + mod;
            int n = static_cast<int> (total);
            float frac = total - static_cast<float> (n);
            if (frac < 0.1f)
            {
                --n;
                frac += 1.0f;
            }
            const float eta = (1.0f - frac) / (1.0f + frac);
            const float y = eta * lines[idx].read (static_cast<float> (n)) + lines[idx].read (static_cast<float> (n + 1))
                          - eta * allpassState[idx];
            allpassState[idx] = y;

            v[idx] = lineGain[idx] * lineShelf[idx].process (y);

            lateL += walsh (i, kOutLeftMask) * v[idx];
            lateR += walsh (i, kOutRightMask) * v[idx];
        }
        lateL *= lateGain;
        lateR *= lateGain;

        float wetL = eL + lateL;
        float wetR = eR + lateR;

        // Low Cut and Hi Cut, off at the ends of their travel.
        const bool lowCutMoving = lowCutSmooth.isSmoothing();
        const bool highCutMoving = highCutSmooth.isSmoothing();
        const float lc = lowCutSmooth.getNextValue();
        const float hc = highCutSmooth.getNextValue();
        if (lowCutMoving)
            for (auto& f : lowCutFilter)
                f.setCutoff (lc, sr);
        if (highCutMoving)
            for (auto& f : highCutFilter)
                f.setCutoff (hc, sr);

        float lp, hp;
        if (lc > kMinLowCutHz + 0.01f)
        {
            lowCutFilter[0].process (wetL, lp, hp);
            wetL = hp;
            lowCutFilter[1].process (wetR, lp, hp);
            wetR = hp;
        }
        else
        {
            for (auto& f : lowCutFilter)
                f.reset();
        }
        if (hc < kMaxHighCutHz - 0.01f)
        {
            highCutFilter[0].process (wetL, lp, hp);
            wetL = lp;
            highCutFilter[1].process (wetR, lp, hp);
            wetR = lp;
        }
        else
        {
            for (auto& f : highCutFilter)
                f.reset();
        }

        // Non-finite, or a finite runaway: the lines are poisoned. Silence the
        // rest of the block and start clean rather than roar.
        if (! std::isfinite (wetL) || ! std::isfinite (wetR)
            || std::abs (wetL) > kRunawayCeiling || std::abs (wetR) > kRunawayCeiling)
        {
            for (int k = s; k < numSamples; ++k)
                outL[k] = outR[k] = 0.0f;
            reset();
            return;
        }

        outL[s] = wetL;
        outR[s] = wetR;

        hadamard (v.data());

        for (int i = 0; i < kLines; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const float feed = kInjectGain * (walsh (i, kInMidMask) * feedMid + walsh (i, kInSideMask) * feedSide);
            lines[idx].write (juce::jlimit (-kLineStateCeiling, kLineStateCeiling, v[idx] + feed));
            lines[idx].advance();
        }
    }
}

} // namespace ee::dsp
