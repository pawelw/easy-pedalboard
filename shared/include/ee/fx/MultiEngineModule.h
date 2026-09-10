#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace ee::fx
{

/** A whole-sample delay applied in place, used only to line the module's
    engines up with each other and with its own dry path. Not a DSP delay line:
    there is no interpolation and no feedback, because nothing here ever moves -
    an engine's latency is fixed by its own construction. */
class AlignDelay
{
public:
    void prepare (int numChannels, int lengthSamples)
    {
        length = juce::jmax (0, lengthSamples);

        if (length == 0)
            return;

        line.setSize (numChannels, length, false, true, true);
        line.clear();
        writeIndex = 0;
    }

    void reset() noexcept
    {
        if (length > 0)
            line.clear();

        writeIndex = 0;
    }

    void process (juce::AudioBuffer<float>& io, int numChannels, int numSamples) noexcept
    {
        if (length == 0)
            return;

        const int numCh = juce::jmin (numChannels, line.getNumChannels());
        int w = writeIndex;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = io.getWritePointer (ch);
                float* stored = line.getWritePointer (ch);

                const float out = stored[w];
                stored[w] = d[i];
                d[i] = out;
            }

            if (++w >= length)
                w = 0;
        }

        writeIndex = w;
    }

    int getLength() const noexcept { return length; }

private:
    juce::AudioBuffer<float> line;
    int length = 0;
    int writeIndex = 0;
};

/** The chrome a switchable effect module has, without the effects.
 *
 * Peak Alpine's Modulation and Reverb modules are the same object at two
 * settings: a list of engines with one selected, a dry/wet Mix, an output
 * Level, and a power toggle. Only the engine list differs, so only the engine
 * list is written twice - everything below is written once, and in particular
 * both modules crossfade between engines identically because there is one
 * crossfade rather than two that were meant to match.
 *
 * **Every engine stays prepared**, and by default every engine also keeps
 * *running*, with only the selected one's output used. A module that tore an
 * engine down on a switch would have to build it again on the way back, on the
 * audio thread; one that merely stopped feeding it would swap in a cold engine,
 * and an engine with a delay line in it comes back cold as silence followed by
 * an abrupt arrival - a step in the middle of the crossfade, which no crossfade
 * can smooth because it happens inside the signal being faded. Peak Alpine's
 * chorus does exactly that. So the engines are kept warm and a switch is a
 * crossfade between two signals that are both already real.
 *
 * `enginesRunWarm` is the override for a module whose engines are expensive and
 * do not need it - see ReverbModule.
 *
 * **Engines are latency-aligned.** A module mixes its wet side against its own
 * dry, so an engine that delays the signal combs against it: Peak Alpine's Tape
 * engine reads off a transport line 9.7 ms long, and at anything short of full
 * wet that arrived as a fixed comb sweeping with the wow - a chorus, on a knob
 * called Flutter. A subclass declares each engine's latency through
 * `engineLatencySamples` and this pads the dry path and every shorter engine out
 * to the longest of them, so the module has one honest latency for the host to
 * compensate and nothing inside it is fighting a copy of itself. A module whose
 * engines are all latency-free (ReverbModule) pays nothing: every line is zero
 * samples long and `process` skips it.
 *
 * A subclass supplies `renderEngine` and `resetEngine`. One virtual call per
 * engine per block is nothing; per sample it would not be, which is why the
 * interface is a block at a time.
 */
class MultiEngineModule
{
public:
    virtual ~MultiEngineModule() = default;

    static constexpr int kMaxChannels = 2;

    /** How long a switch takes to cross from one engine to the other. Twice the
        usual control ramp: this is two unrelated signals being swapped, not a
        gain moving, and a 20 ms swap of a chorus for a phaser is audible as a
        glitch rather than as a change. */
    static constexpr double kEngineFadeSeconds = 0.04;

    /** The dry/wet, level and power ramps. Equal to ee::plugin::kRampSeconds -
        see ee/fx/DelayModuleConfig.h for why it is restated rather than
        included, and who checks the two agree. */
    static constexpr float kGainRampSeconds = 0.02f;

    /** Anything past this at the module output is a runaway, not audio: an
        engine's feedback loop gone marginally unstable climbs to a finite but
        absurd level without ever going non-finite. Scrubbed like a NaN.
        ~+36 dBFS, well clear of any legitimate wet-plus-level peak. */
    static constexpr float kRunawayCeiling = 64.0f;

    void prepare (double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock = juce::jmax (1, maximumExpectedSamplesPerBlock);

        dryBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
        wetBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
        fadeBuffer.setSize (kMaxChannels, maxBlock, false, true, true);
        warmBuffer.setSize (kMaxChannels, maxBlock, false, true, true);

        dryGain.reset (sr, kGainRampSeconds);
        wetGain.reset (sr, kGainRampSeconds);
        levelGain.reset (sr, kGainRampSeconds);
        engageGain.reset (sr, kGainRampSeconds);

        const bool usesMix = engineUsesMix (engine);
        dryGain.setCurrentAndTargetValue (usesMix ? dryTarget() : 0.0f);
        wetGain.setCurrentAndTargetValue (usesMix ? wetTarget() : 1.0f);
        levelGain.setCurrentAndTargetValue (level);
        engageGain.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);

        fadeSamplesLeft = 0;
        outgoingEngine = -1;

        prepareEngines (sr, maxBlock);

        // After prepareEngines, not before: an engine only knows its own
        // latency once it has been prepared at this sample rate.
        alignLatency = 0;
        for (int i = 0; i < engineCount(); ++i)
            alignLatency = juce::jmax (alignLatency, engineLatencySamples (i));

        dryAlign.prepare (kMaxChannels, alignLatency);

        engineAlign.clear();
        engineAlign.resize (static_cast<size_t> (juce::jmax (0, engineCount())));
        for (int i = 0; i < engineCount(); ++i)
            engineAlign[static_cast<size_t> (i)].prepare (kMaxChannels,
                                                          alignLatency - engineLatencySamples (i));
    }

    /** What the module delays the signal by, for the owner's
        `setLatencySamples`. Constant, and the same whichever engine is
        selected - that is the point of the alignment. */
    int latencySamples() const noexcept { return alignLatency; }

    void reset() noexcept
    {
        for (int i = 0; i < engineCount(); ++i)
            resetEngine (i);

        dryAlign.reset();
        for (auto& align : engineAlign)
            align.reset();

        fadeSamplesLeft = 0;
        outgoingEngine = -1;
    }

    /** Which engine is running. Out of range is ignored rather than clamped: it
        means the state came from somewhere odd, and silently landing on a
        neighbour would be a worse answer than staying put. */
    void setEngine (int index) noexcept
    {
        if (index < 0 || index >= engineCount() || index == engine)
            return;

        // Cross from where we are. If a switch is already in flight its
        // outgoing engine is abandoned mid-fade - two switches inside 40 ms is
        // someone spinning the stepper, and chaining the fades would make the
        // control feel like it was lagging behind them.
        outgoingEngine = engine;
        engine = index;
        fadeSamplesLeft = static_cast<int> (kEngineFadeSeconds * sr);
    }

    int currentEngine() const noexcept { return engine; }

    void setMix01 (float v) noexcept { mix = juce::jlimit (0.0f, 1.0f, v); }
    void setLevel (float linear) noexcept { level = juce::jmax (0.0f, linear); }
    void setEngaged (bool v) noexcept { engaged = v; }

    /** Wet, in place. The caller's buffer is read as the dry signal and written
        with the finished one. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        const int numCh = juce::jlimit (0, int { kMaxChannels }, numChannels);

        if (numCh <= 0 || numSamples <= 0)
            return;

        // An engine that opts out of the Mix control (see engineUsesMix) runs
        // fully wet and is never summed against a static dry copy of the input.
        // This is for an engine whose wet path wanders in time - Tape's wow
        // rides a transport delay line - where a fixed dry alongside it is a
        // comb whose notch sweeps with the wow, heard as tremolo at any partial
        // Mix and clean only at the ends. So Mix is simply not offered for it;
        // the module's own power toggle is its dry/wet. Every other engine is
        // unchanged.
        const bool usesMix = engineUsesMix (engine);

        dryGain.setTargetValue (usesMix ? dryTarget() : 0.0f);
        wetGain.setTargetValue (usesMix ? wetTarget() : 1.0f);
        levelGain.setTargetValue (level);
        engageGain.setTargetValue (engaged ? 1.0f : 0.0f);

        for (int offset = 0; offset < numSamples; offset += maxBlock)
        {
            const int chunk = juce::jmin (maxBlock, numSamples - offset);

            for (int ch = 0; ch < numCh; ++ch)
                dryBuffer.copyFrom (ch, 0, buffer, ch, offset, chunk);

            // The selected engine into `wet`, the one being left into `fade`,
            // and - unless the subclass says otherwise - every other engine into
            // a scratch buffer whose contents are thrown away. That last part is
            // the whole point: an engine nobody is listening to still has to see
            // the signal, or it is cold the moment it is selected.
            for (int i = 0; i < engineCount(); ++i)
            {
                // Every engine that runs also runs its own padding, including
                // the warm ones nobody is listening to: a padding line that
                // stopped while its engine kept going would hand back stale
                // audio the moment that engine was selected, which is the very
                // click keeping the engines warm exists to avoid.
                juce::AudioBuffer<float>* into = nullptr;

                if (i == engine)
                    into = &wetBuffer;
                else if (i == outgoingEngine && fadeSamplesLeft > 0)
                    into = &fadeBuffer;
                else if (enginesRunWarm())
                    into = &warmBuffer;

                if (into != nullptr)
                {
                    renderEngine (i, dryBuffer, *into, numCh, chunk);
                    align (i, *into, numCh, chunk);
                }
            }

            // The dry side of the mix, delayed to meet them. `dryBuffer` is
            // also what the engage crossfade reads, so a bypassed module is
            // the input delayed by the module's reported latency rather than
            // the input as it was - which is what the host is compensating.
            dryAlign.process (dryBuffer, numCh, chunk);

            // Mid-switch: the engine being left is still running, and the two
            // are crossed sample by sample. Equal power rather than linear -
            // two different effects are uncorrelated, so a linear crossfade
            // dips in the middle of every switch.
            if (fadeSamplesLeft > 0 && outgoingEngine >= 0)
            {
                const double step = 1.0 / (kEngineFadeSeconds * sr);

                for (int i = 0; i < chunk; ++i)
                {
                    const float t = static_cast<float> (juce::jlimit (
                        0.0, 1.0, 1.0 - static_cast<double> (fadeSamplesLeft - i) * step));
                    const float toNew = std::sin (t * juce::MathConstants<float>::halfPi);
                    const float toOld = std::cos (t * juce::MathConstants<float>::halfPi);

                    for (int ch = 0; ch < numCh; ++ch)
                    {
                        float* w = wetBuffer.getWritePointer (ch);
                        w[i] = w[i] * toNew + fadeBuffer.getReadPointer (ch)[i] * toOld;
                    }
                }

                fadeSamplesLeft -= chunk;

                if (fadeSamplesLeft <= 0)
                {
                    // Only cleared when it is about to go cold anyway. A warm
                    // module deliberately leaves it running and ringing, which
                    // is what makes selecting it again instant.
                    if (! enginesRunWarm())
                        resetEngine (outgoingEngine);

                    outgoingEngine = -1;
                    fadeSamplesLeft = 0;
                }
            }

            for (int i = 0; i < chunk; ++i)
            {
                const float dg = dryGain.getNextValue();
                const float wg = wetGain.getNextValue();
                const float lg = levelGain.getNextValue();
                const float e = engageGain.getNextValue();

                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float dry = dryBuffer.getReadPointer (ch)[i];
                    const float mixed = (dry * dg + wetBuffer.getReadPointer (ch)[i] * wg) * lg;

                    // Bypassed is the untouched input, crossfaded rather than
                    // switched - and it is the *input*, not the dry side of the
                    // mix, so Level and Mix cannot make a bypassed module
                    // anything but unity.
                    float out = dry + (mixed - dry) * e;

                    // Non-finite, or a finite runaway from an engine whose
                    // feedback loop has drifted marginally unstable - its own
                    // guard resets it, but this block's tail has already
                    // climbed. Either way it is not audio.
                    if (! std::isfinite (out) || std::abs (out) > kRunawayCeiling)
                        out = 0.0f;

                    buffer.getWritePointer (ch, offset)[i] = out;
                }
            }
        }
    }

protected:
    /** How many engines the subclass has. */
    virtual int engineCount() const noexcept = 0;

    /** Every engine gets prepared, not just the selected one - see the class
        note on why none of them is ever torn down. */
    virtual void prepareEngines (double sampleRate, int maxBlockSize) = 0;

    /** One engine's wet output for this chunk. `dry` is read-only; `wet` is the
        buffer to fill. Both are at least `numChannels` wide and `numSamples`
        long. An engine that works in place copies dry into wet first. */
    virtual void renderEngine (int index, const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& wet,
                               int numChannels, int numSamples) noexcept = 0;

    virtual void resetEngine (int index) noexcept = 0;

    /** Whether unselected engines keep being fed. True by default, because a
        cold engine is the thing that clicks. A subclass overrides it when its
        engines are expensive enough that running all of them always is the
        bigger problem, *and* it has checked that they do not click cold. */
    virtual bool enginesRunWarm() const noexcept { return true; }

    /** Whether engine `index` is offered the module's Mix control. True by
        default. Return false for an engine that must run fully wet - one whose
        wet output wanders in time (a tape transport's wow), where any partial
        Mix combs a fixed dry against the moving wet and is heard as tremolo.
        Such an engine ignores Mix entirely; the module's power toggle is its
        dry/wet, and the face is expected to drop the Mix knob for it too. */
    virtual bool engineUsesMix (int) const noexcept { return true; }

    /** How many samples engine `index` delays the signal by. Called once, from
        `prepare`, after `prepareEngines` - so an engine that works its latency
        out from the sample rate has already done so. Zero by default, which is
        what every engine that is not a delay line answers. */
    virtual int engineLatencySamples (int) const noexcept { return 0; }

    double sr = 44100.0;
    int maxBlock = 512;

private:
    /** Engine `index`'s output, padded out to the module's own latency. */
    void align (int index, juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
    {
        if (index >= 0 && index < static_cast<int> (engineAlign.size()))
            engineAlign[static_cast<size_t> (index)].process (buffer, numChannels, numSamples);
    }

    /** The same equal-power law the delay's Mix uses, so a Mix knob means the
        same thing everywhere in the plugin. */
    float dryTarget() const noexcept { return std::cos (mix * juce::MathConstants<float>::halfPi); }
    float wetTarget() const noexcept { return std::sin (mix * juce::MathConstants<float>::halfPi); }

    int engine = 0;
    int outgoingEngine = -1;
    int fadeSamplesLeft = 0;

    float mix = 0.0f;
    float level = 1.0f;
    bool engaged = true;

    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> wetGain;
    juce::SmoothedValue<float> levelGain;
    juce::SmoothedValue<float> engageGain;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    juce::AudioBuffer<float> fadeBuffer;

    /** Where a warm-but-unlistened engine's output goes. Written and never
        read - what matters is that the engine advanced its own state. */
    juce::AudioBuffer<float> warmBuffer;

    /** The padding. `dryAlign` is the whole module latency; each engine's is
        the remainder between its own and that, so an engine already at the
        maximum gets a zero-length line and no work. */
    int alignLatency = 0;
    AlignDelay dryAlign;
    std::vector<AlignDelay> engineAlign;
};

} // namespace ee::fx
