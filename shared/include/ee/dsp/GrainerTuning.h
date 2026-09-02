#pragma once

namespace ee::dsp
{

/** Every number that shapes Peak Grain's character but is not on the face.

    The face knobs say how long the grains are, how many, how far back they are
    tapped from (Time), how much comes back round (Feedback), how the read head
    scans a frozen buffer (Stretch), how the grain envelope leans (Shape) and
    how ragged the timing is (Scatter). These say the rest: how the Scatter and
    Shape knobs map onto the engine, which intervals the pitched grains snap to,
    and the two reverb fields that are not on the face.

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
    // How the Scatter knob maps onto the timing randomness. At Scatter 100 %
    // the gap between grains wanders by this fraction of the nominal gap...
    float scatterMaxJitter = 0.5f;

    // ...and each grain's length strays from Size by up to this fraction.
    float scatterSizeJitter = 0.4f;

    // Share of grains drawn from the last detected attack rather than from the
    // Time window. Live only - a frozen buffer plays from wherever Stretch has
    // the read head. A plucked string is mostly its first fifty milliseconds; a
    // cloud built from the sustain alone loses whatever made the note
    // identifiable.
    float attackShare = 0.70f;

    // The two ends the Shape knob morphs the grain envelope between. A
    // symmetric window - a Hann, say - fades a grain in over its whole first
    // half, which throws away the transient and leaves a swell: a plucked
    // string comes back sounding like it was played backwards. So even the soft
    // end keeps a short fade-in and spends the rest of the grain decaying.
    //
    // shapeAttackMs* is the fade-in. Under about 0.5 ms it starts to tick on
    // low material; much over 5 ms and the pluck goes soft.
    float shapeAttackMsSoft = 3.0f;
    float shapeAttackMsHard = 1.0f;

    // shapeDecayShape* is the exponent of the decay that fills the rest of the
    // grain. The curve always reaches exactly zero at the end, so this only
    // changes how front-loaded it is.
    //   1.0 = nearly a straight fade, the grain stays present to the end
    //   8.0 = a click with a tail
    float shapeDecayShapeSoft = 1.0f;
    float shapeDecayShapeHard = 8.0f;

    // Overlapping grains sum, so the engine divides by the square root of
    // the expected overlap. This trims the result back to roughly unity against
    // the dry signal.
    float outputTrim = 1.4f;

    // The intervals the Low and High pitch groups draw from, in semitones -
    // four slots each, and the repeats are the weighting. Mostly octaves, with
    // a fifth up / a fourth down in one slot each so a cloud with Low or High
    // dialled in leans consonant rather than landing on a wrong note. Put a 24
    // or a 0 in a slot from the tuning panel to change the spread.
    //
    // Anything beyond about +/- 19 semitones is clamped by the engine: a grain
    // faster than that would span more source than the buffer guarantees.
    float upA = 12.0f;
    float upB = 12.0f;
    float upC = 7.0f;
    float upD = 12.0f;

    float downA = -12.0f;
    float downB = -12.0f;
    float downC = -5.0f;
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
    { "scatterMaxJitter",   &GrainerTuning::scatterMaxJitter,    0.0f,     1.0f,  3 },
    { "scatterSizeJitter",  &GrainerTuning::scatterSizeJitter,   0.0f,     1.0f,  3 },
    { "attackShare",        &GrainerTuning::attackShare,         0.0f,     1.0f,  3 },

    { "shapeAttackMsSoft",  &GrainerTuning::shapeAttackMsSoft,   0.1f,    20.0f,  2 },
    { "shapeAttackMsHard",  &GrainerTuning::shapeAttackMsHard,   0.1f,    20.0f,  2 },
    { "shapeDecayShapeSoft", &GrainerTuning::shapeDecayShapeSoft, 0.5f,   10.0f,  2 },
    { "shapeDecayShapeHard", &GrainerTuning::shapeDecayShapeHard, 0.5f,   10.0f,  2 },

    { "outputTrim",         &GrainerTuning::outputTrim,          0.0f,     3.0f,  3 },

    { "upA",                &GrainerTuning::upA,               -19.0f,    19.0f,  0 },
    { "upB",                &GrainerTuning::upB,               -19.0f,    19.0f,  0 },
    { "upC",                &GrainerTuning::upC,               -19.0f,    19.0f,  0 },
    { "upD",                &GrainerTuning::upD,               -19.0f,    19.0f,  0 },

    { "downA",              &GrainerTuning::downA,             -19.0f,    19.0f,  0 },
    { "downB",              &GrainerTuning::downB,             -19.0f,    19.0f,  0 },
    { "downC",              &GrainerTuning::downC,             -19.0f,    19.0f,  0 },
    { "downD",              &GrainerTuning::downD,             -19.0f,    19.0f,  0 },

    { "verbResonance",      &GrainerTuning::verbResonance,       0.0f,     1.0f,  3 },
    { "verbLowCutHz",       &GrainerTuning::verbLowCutHz,       20.0f,   800.0f,  0 },
};

} // namespace ee::dsp
