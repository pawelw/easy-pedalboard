// ee::fx::ModulationModule and ee::fx::ReverbModule: the two switchable modules
// Peak Alpine's side panels run.
//
// Not a *_regress tool - there is no "before" to diff against, because this is
// new code rather than moved code. What it checks instead is the contract the
// shared MultiEngineModule promises, and the thing a module made of six other
// people's engines is most likely to get wrong: that switching between them
// does not click, and that nothing anywhere in the parameter space produces a
// non-finite sample or runs away.
#include <cstdio>

#include "ee/fx/ModulationModule.h"
#include "ee/fx/ReverbModule.h"

#include "RegressHarness.h"

namespace
{
using namespace ee::regress;

constexpr double kSampleRate = 48000.0;

int failures = 0;

void check (bool condition, const char* what)
{
    std::printf ("  %s  %s\n", condition ? "ok  " : "FAIL", what);
    if (! condition)
        ++failures;
}

/** The largest jump between one output sample and the next. A switch between
    two unrelated effects has to be crossfaded; if it is not, this is where it
    shows up - as a step far larger than anything the signal itself contains. */
float largestStep (const juce::AudioBuffer<float>& buffer, int from, int to)
{
    float worst = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = juce::jmax (1, from); i < to; ++i)
            worst = juce::jmax (worst, std::abs (d[i] - d[i - 1]));
    }

    return worst;
}

/** Runs a module over a buffer in ragged blocks, optionally switching engine
    partway. Returns the buffer it wrote. */
template <typename Module>
void run (Module& module, juce::AudioBuffer<float>& buffer, int switchTo = -1, int switchAt = -1)
{
    int blockIndex = 0;

    for (int offset = 0; offset < buffer.getNumSamples();)
    {
        const int chunk = juce::jmin (nextBlockSize (blockIndex++), buffer.getNumSamples() - offset);

        if (switchTo >= 0 && switchAt >= 0 && offset >= switchAt)
        {
            module.setEngine (switchTo);
            switchTo = -1;
        }

        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), offset, chunk);
        module.process (slice, buffer.getNumChannels(), chunk);

        offset += chunk;
    }
}

void setModulationDefaults (ee::fx::ModulationModule& m)
{
    m.setTape (0.5f, 0.4f, 0.5f, 0.2f);
    m.setTremolo (0.7f, 0.25f, 0.5f, 0.3f);
    m.setChorus (0.6f, 0.5f, 90.0f);
    m.setPhaser (0.4f, 0.6f);
}

void setReverbDefaults (ee::fx::ReverbModule& m)
{
    m.setSpace (2.0f, 0.2f, 100.0f, 0.5f);
    m.setSpring (2.0f, 0.5f, 60.0f);
}


// ---------------------------------------------------------------- the checks

/** The module's own latency, as a lag to compare against: everything the module
    emits is the input delayed by `latencySamples()`, because the dry path is
    padded out to meet its longest engine. Zero for a module whose engines have
    none, in which case this is the plain comparison it used to be. */
template <typename Module>
bool matchesDelayedInput (Module& module, const juce::AudioBuffer<float>& out,
                          const juce::AudioBuffer<float>& input, int from)
{
    const int lag = module.latencySamples();

    for (int ch = 0; ch < 2; ++ch)
        for (int i = juce::jmax (from, lag); i < input.getNumSamples(); ++i)
            if (out.getSample (ch, i) != input.getSample (ch, i - lag))
                return false;

    return true;
}

/** Mix at 0 with Level at 1 must return the input untouched, bit for bit. Not
    "close": the mix law is cos/sin, cos(0) is exactly 1 and sin(0) exactly 0,
    so any drift here is a bug in the mix rather than a rounding cost. Bit for
    bit *at the module's own latency* - the alignment delay is a whole number of
    samples copied through a buffer, which does not change a value. */
template <typename Module>
void checkMixZeroIsDry (Module& module, const char* name)
{
    juce::AudioBuffer<float> input (2, 8192);
    fillTestSignal (input, kSampleRate);

    juce::AudioBuffer<float> out;
    out.makeCopyOf (input);

    module.setMix01 (0.0f);
    module.setLevel (1.0f);
    module.setEngaged (true);
    run (module, out);

    std::printf ("%s (latency %d):\n", name, module.latencySamples());
    check (matchesDelayedInput (module, out, input, 0), "mix 0 returns the input bit for bit");
}

/** The whole reason the alignment exists: at a *partial* Mix, an engine that
    delays the signal is being summed with the module's own dry, and if the two
    are not lined up that sum is a comb filter. Tape at rest is bypassed at every
    knob and is a bit-exact pass-through with a fixed latency, so the sum has to
    come back as the input and nothing else - one sample out and this fails,
    which is the state the Modulation module's Flutter knob shipped in.

    "The input and nothing else" is not "the input at the same level". The mix
    law is equal-power, so at Mix 0.4 the two gains are cos and sin of the same
    angle and they sum to 1.397 rather than to 1 - correct for the uncorrelated
    pair it is written for, and a plain 2.9 dB lift for a wet side that happens
    to *be* the dry. So what is checked is the shape: the output has to be that
    one constant times the input, with no frequency-dependent term in it. A comb
    would fail here however the level was scaled. */
void checkTapeAtRestIsDry()
{
    ee::fx::ModulationModule module;
    module.prepare (kSampleRate, 512);
    module.setEngine (ee::fx::ModulationModule::Tape);
    module.setTape (0.0f, 0.0f, 0.0f, 0.0f);
    module.setMix01 (0.4f);
    module.setLevel (1.0f);
    module.setEngaged (true);

    juce::AudioBuffer<float> input (2, 8192);
    fillTestSignal (input, kSampleRate);

    juce::AudioBuffer<float> out;
    out.makeCopyOf (input);
    run (module, out);

    // Past the mix ramp, which starts at the module's resting Mix rather than
    // at this one - the first 20 ms is that ramp, not the alignment.
    const int settled = module.latencySamples() + static_cast<int> (kSampleRate * 0.05);

    const float angle = 0.4f * juce::MathConstants<float>::halfPi;
    const float gain = std::cos (angle) + std::sin (angle);

    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = settled; i < input.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (out.getSample (ch, i)
                                                 - gain * input.getSample (ch, i - module.latencySamples())));

    std::printf ("  %s  Tape at rest is the input at Mix 40 %% (worst %.7f)\n", worst <= 1.0e-5f ? "ok  " : "FAIL",
                 worst);
    if (worst > 1.0e-5f)
        ++failures;
}

/** Bypassed must also return the input untouched, and must do it whatever Mix
    and Level say - the engage crossfade goes back to the *input*, not to the
    dry side of the mix, so a module turned off cannot be made loud by leaving
    Level up. The first few ms are the ramp and are skipped. */
template <typename Module>
void checkBypassIsUnity (Module& module, const char* name)
{
    juce::AudioBuffer<float> input (2, 8192);
    fillTestSignal (input, kSampleRate);

    juce::AudioBuffer<float> out;
    out.makeCopyOf (input);

    module.setMix01 (1.0f);
    module.setLevel (2.0f);
    module.setEngaged (false);
    run (module, out);

    const int settled = static_cast<int> (kSampleRate * 0.1);

    check (matchesDelayedInput (module, out, input, settled),
           "bypassed is the input, whatever Mix and Level say");
    (void) name;
}

/** Switching engines mid-signal must not step. The bound is generous on
    purpose - the test signal has 0.6-amplitude clicks in it, so the honest
    question is whether a switch is worse than the material, not whether the
    output is smooth. */
template <typename Module>
void checkSwitchDoesNotClick (Module& module, int from, int to, const char* what)
{
    juce::AudioBuffer<float> buffer (2, static_cast<int> (kSampleRate));

    // A steady tone, deliberately without the harness's clicks: this is
    // measuring the switch, so nothing else in the signal may step.
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float v = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f
                                         * static_cast<float> (i) / static_cast<float> (kSampleRate));
        buffer.setSample (0, i, v);
        buffer.setSample (1, i, v * 0.8f);
    }

    module.setMix01 (1.0f);
    module.setLevel (1.0f);
    module.setEngaged (true);
    module.setEngine (from);

    const int switchAt = buffer.getNumSamples() / 2;
    run (module, buffer, to, switchAt);

    // What the signal itself does either side of the switch, well clear of it.
    const int guard = static_cast<int> (kSampleRate * 0.1);
    const float quiet = juce::jmax (largestStep (buffer, guard, switchAt - guard),
                                    largestStep (buffer, switchAt + guard, buffer.getNumSamples()));
    const float across = largestStep (buffer, switchAt - 64, switchAt + static_cast<int> (kSampleRate * 0.06));

    const bool ok = allFinite (buffer) && across <= juce::jmax (0.02f, quiet * 3.0f);

    std::printf ("  %s  %s (step across %.5f, elsewhere %.5f)\n", ok ? "ok  " : "FAIL", what, across, quiet);
    if (! ok)
        ++failures;
}

/** Every engine, over a coarse sweep of everything it has, watching for a
    non-finite sample or a runaway. */
void sweepModulation()
{
    std::printf ("\nModulation sweep:\n");

    int cases = 0;
    float worstPeak = 0.0f;
    bool clean = true;

    for (int engine = 0; engine < ee::fx::ModulationModule::NumEngines; ++engine)
        for (float a : { 0.0f, 0.5f, 1.0f })
            for (float b : { 0.0f, 1.0f })
                for (float mix : { 0.0f, 0.5f, 1.0f })
                {
                    ee::fx::ModulationModule module;
                    module.prepare (kSampleRate, 512);
                    module.setEngine (engine);
                    module.setMix01 (mix);
                    module.setLevel (1.0f);
                    module.setEngaged (true);

                    module.setTape (a, b, a, b);
                    module.setTremolo (a, 0.01f + b * 1.5f, a, b);
                    module.setChorus (0.05f + a * 8.0f, b, a * 180.0f);
                    module.setPhaser (0.05f + a * 8.0f, b);

                    juce::AudioBuffer<float> buffer (2, static_cast<int> (kSampleRate / 2));
                    fillTestSignal (buffer, kSampleRate);
                    run (module, buffer);

                    ++cases;
                    worstPeak = juce::jmax (worstPeak, buffer.getMagnitude (0, buffer.getNumSamples()));
                    clean = allFinite (buffer) && clean;
                }

    std::printf ("  %d cases, worst peak %.3f\n", cases, worstPeak);
    check (clean, "every Modulation case finite");
    check (worstPeak < 8.0f, "nothing ran away");
}

void sweepReverb()
{
    std::printf ("\nReverb sweep:\n");

    int cases = 0;
    float worstPeak = 0.0f;
    bool clean = true;

    for (int engine = 0; engine < ee::fx::ReverbModule::NumEngines; ++engine)
        for (float decay : { 0.5f, 4.0f, 8.0f })
            for (float a : { 0.0f, 1.0f })
                for (float mix : { 0.0f, 1.0f })
                {
                    ee::fx::ReverbModule module;
                    module.prepare (kSampleRate, 512);
                    module.setEngine (engine);
                    module.setMix01 (mix);
                    module.setLevel (1.0f);
                    module.setEngaged (true);

                    module.setSpace (decay, a, 20.0f + a * 780.0f, a);
                    module.setSpring (decay, a, 20.0f + a * 780.0f);

                    juce::AudioBuffer<float> buffer (2, static_cast<int> (kSampleRate));
                    fillTestSignal (buffer, kSampleRate);
                    run (module, buffer);

                    ++cases;
                    worstPeak = juce::jmax (worstPeak, buffer.getMagnitude (0, buffer.getNumSamples()));
                    clean = allFinite (buffer) && clean;
                }

    std::printf ("  %d cases, worst peak %.3f\n", cases, worstPeak);
    check (clean, "every Reverb case finite");
    check (worstPeak < 8.0f, "nothing ran away");
}
} // namespace

int main()
{
    std::printf ("=== Peak Alpine module stress ===\n\n");

    {
        ee::fx::ModulationModule module;
        module.prepare (kSampleRate, 512);
        setModulationDefaults (module);
        checkMixZeroIsDry (module, "Modulation");
    }
    {
        ee::fx::ModulationModule module;
        module.prepare (kSampleRate, 512);
        setModulationDefaults (module);
        checkBypassIsUnity (module, "Modulation");
    }
    checkTapeAtRestIsDry();
    {
        ee::fx::ReverbModule module;
        module.prepare (kSampleRate, 512);
        setReverbDefaults (module);
        checkMixZeroIsDry (module, "Reverb");
    }
    {
        ee::fx::ReverbModule module;
        module.prepare (kSampleRate, 512);
        setReverbDefaults (module);
        checkBypassIsUnity (module, "Reverb");
    }

    std::printf ("\nEngine switches:\n");

    // Every neighbouring pair the stepper can reach, plus the wrap.
    const int modPairs[][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 0, 2 } };
    for (const auto& pair : modPairs)
    {
        ee::fx::ModulationModule module;
        module.prepare (kSampleRate, 512);
        setModulationDefaults (module);

        char what[64];
        std::snprintf (what, sizeof (what), "Modulation %d -> %d", pair[0], pair[1]);
        checkSwitchDoesNotClick (module, pair[0], pair[1], what);
    }

    {
        ee::fx::ReverbModule module;
        module.prepare (kSampleRate, 512);
        setReverbDefaults (module);
        checkSwitchDoesNotClick (module, 0, 1, "Reverb Space -> Spring");
    }
    {
        ee::fx::ReverbModule module;
        module.prepare (kSampleRate, 512);
        setReverbDefaults (module);
        checkSwitchDoesNotClick (module, 1, 0, "Reverb Spring -> Space");
    }

    sweepModulation();
    sweepReverb();

    std::printf ("\n%s\n", failures == 0 ? "OK - all module checks passed"
                                         : "MODULE CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
