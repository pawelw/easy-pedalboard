#pragma once

namespace ee::dsp
{

/** Every number that shapes Peak Grain's character but is not on the face.

    The face knobs say how long the grains are, how many, how far back they are
    tapped from (Time), how much comes back round (Feedback), how the read head
    scans a frozen buffer (Stretch), how the grain envelope leans (Shape) and
    how ragged the timing is (Scatter). These say the rest: how the Scatter and
    Shape knobs map onto the engine, and the two reverb fields that are not on
    the face. Which intervals the pitched grains snap to is Scale/Root now (a
    face control, not tuning) - see GrainerConfig.h's SCALE section.

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

    // How much each grain's level varies, as a downward fraction of unity: a
    // grain is scaled by 1 - grainLevelJitter * random(0..1). Every grain
    // arriving at exactly the same loudness is a large part of why a cloud can
    // sound sequenced rather than alive - real ones breathe. Downward only, so
    // the loudest grain is no louder than it would have been; the output trim
    // divides this distribution's own RMS back out (see Grainer's
    // updateDerived), so this changes texture rather than level.
    float grainLevelJitter = 0.35f;

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
    { "grainLevelJitter",   &GrainerTuning::grainLevelJitter,    0.0f,     1.0f,  3 },

    { "shapeAttackMsSoft",  &GrainerTuning::shapeAttackMsSoft,   0.1f,    20.0f,  2 },
    { "shapeAttackMsHard",  &GrainerTuning::shapeAttackMsHard,   0.1f,    20.0f,  2 },
    { "shapeDecayShapeSoft", &GrainerTuning::shapeDecayShapeSoft, 0.5f,   10.0f,  2 },
    { "shapeDecayShapeHard", &GrainerTuning::shapeDecayShapeHard, 0.5f,   10.0f,  2 },

    { "outputTrim",         &GrainerTuning::outputTrim,          0.0f,     3.0f,  3 },

    { "verbResonance",      &GrainerTuning::verbResonance,       0.0f,     1.0f,  3 },
    { "verbLowCutHz",       &GrainerTuning::verbLowCutHz,       20.0f,   800.0f,  0 },
};

} // namespace ee::dsp
