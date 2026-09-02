#pragma once

namespace ee::dsp
{

/** Every number that shapes Peak Grain's character but is not on the face.

    The seven knobs say how long the grains are, how many, how far back they
    come from and how far the pitch spreads. These say what a grain *is* - how
    many run backwards, where they land across the image, which intervals the
    pitched ones snap to, how ragged the timing is.

    Kept as a struct rather than constants so the development tuning panel can
    drive them live, and so the whole voicing can be read at a glance. The
    defaults below are a tuned setting; build with -DEE_GRAIN_TUNER=ON to bring
    the live panel back.

    The knob *ranges*, the buffer size and the voice count stay in
    GrainerConfig.h - they are structural rather than voicing, and changing one
    live would mean reallocating underneath the audio thread.
*/
struct GrainerTuning
{
    // How much the gap between grains wanders, as a fraction of the nominal
    // gap. 0 is a metronome, and at low densities it sounds like one.
    float spawnJitter = 0.25f;

    // Share of grains drawn from the last detected attack rather than from a
    // random point in the Decay window. A plucked string is mostly its first
    // fifty milliseconds; a cloud built from the sustain alone loses whatever
    // made the note identifiable.
    float attackShare = 0.70f;

    // Overlapping Hann grains sum, so the engine divides by the square root of
    // the expected overlap. This trims the result back to roughly unity against
    // the dry signal.
    float outputTrim = 1.4f;

    // The intervals the Low and High pitch groups draw from, in semitones -
    // four slots each, and the repeats are the weighting. Octaves only by
    // default: anything else and a cloud with Low or High dialled in stops
    // sounding like the note that was played and starts sounding like a wrong
    // one. Put a 7 or a 24 in a slot from the tuning panel if you want them.
    // (The Unison group is always exactly 0, plus the Detune knob.)
    //
    // Anything beyond about +/- 19 semitones is clamped by the engine: a grain
    // faster than that would span more source than the buffer guarantees.
    float upA = 12.0f;
    float upB = 12.0f;
    float upC = 12.0f;
    float upD = 12.0f;

    float downA = -12.0f;
    float downB = -12.0f;
    float downC = -12.0f;
    float downD = -12.0f;

    // The reverb behind the cloud. Peak Grain runs FdnReverb plain, with only
    // its mix and decay on the face; these two are the rest of its voicing.
    // Low resonance is the smeared, plate-like end, which suits a dense cloud;
    // the low cut is harder than Peak Reverb idles at because grains stack up
    // and a flat reverb under them turns to mud fast.
    float verbResonance = 0.35f;
    float verbLowCutHz = 120.0f;
};

/** Describes a field for the tuning panel, and names it as the source does. */
struct GrainerTuningEntry
{
    const char* name;
    float GrainerTuning::* member;
    float minimum;
    float maximum;
    int decimals;
};

inline constexpr GrainerTuningEntry kGrainerTuningEntries[] = {
    { "spawnJitter",     &GrainerTuning::spawnJitter,       0.0f,     1.0f,  3 },
    { "attackShare",     &GrainerTuning::attackShare,       0.0f,     1.0f,  3 },

    { "outputTrim",      &GrainerTuning::outputTrim,        0.0f,     3.0f,  3 },

    { "upA",             &GrainerTuning::upA,             -19.0f,    19.0f,  0 },
    { "upB",             &GrainerTuning::upB,             -19.0f,    19.0f,  0 },
    { "upC",             &GrainerTuning::upC,             -19.0f,    19.0f,  0 },
    { "upD",             &GrainerTuning::upD,             -19.0f,    19.0f,  0 },

    { "downA",           &GrainerTuning::downA,           -19.0f,    19.0f,  0 },
    { "downB",           &GrainerTuning::downB,           -19.0f,    19.0f,  0 },
    { "downC",           &GrainerTuning::downC,           -19.0f,    19.0f,  0 },
    { "downD",           &GrainerTuning::downD,           -19.0f,    19.0f,  0 },

    { "verbResonance",   &GrainerTuning::verbResonance,     0.0f,     1.0f,  3 },
    { "verbLowCutHz",    &GrainerTuning::verbLowCutHz,     20.0f,   800.0f,  0 },
};

} // namespace ee::dsp
