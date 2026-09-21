#pragma once

namespace ee::dsp
{

/** Every number that shapes BitBit Grain's character but is not on the face.

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

    // WHERE THE SWELL PEAKS. Smooth's far end is a window that fades in
    // linearly and is cut off soon after its peak, this far through the grain
    // (0..1). Measured off a reference plugin's grains at 1/8: peak at 0.82,
    // a 58 ms rise and a 12 ms fall on an 81 ms grain, every grain alike. The
    // figure is 0.88 rather than 0.82 because the measurement takes the peak
    // where the grain is 10 % up, which lands it a few percent early - this
    // is the value that reproduces those numbers when measured the same way.
    float smoothPeak = 0.88f;

    // How much of that share Smooth takes away at its top: at Smooth 1 the
    // share is attackShare * (1 - this). A cloud that keeps drawing from the
    // pick can only ever sound like the pick, however soft the window around
    // it, and those grains read from wherever the last onset was rather than
    // from the tempo grid - which is what makes a smoothed cloud sound
    // unpredictable against a reference that reads only on the grid. Measured
    // on a vocal at 1/32 (share of 60 ms frames that were replayed onsets):
    // 34 % with the reach at ~0.5 s, 8 % at 0.2 s with Smooth 1 and this at
    // 0.5, 5 % with this at 1. So Smooth 1 is off entirely, and Smooth 0 keeps
    // the pick-anchored cloud that plucked material wants. 0 leaves Smooth as a
    // window control alone.
    float smoothAttackShareCut = 1.0f;

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

    // How far each grain's own window strays from the pair Shape just set.
    // Shape is one envelope and every grain in flight wears it exactly, so at
    // a steady Density the cloud is a train of identical windows and they comb
    // against each other at the spawn rate - which is heard as a pitch sitting
    // on the cloud rather than as texture. The fade-in is scaled by
    // 1 +/- jitter and the decay exponent by a ratio either side of itself, so
    // the stray is even in both directions and the mean energy barely moves;
    // it is deliberately not compensated anywhere.
    float windowJitter = 0.35f;

    // SOURCE LEVELLING. A grain takes whatever the buffer holds where it
    // landed, so one drawn from the tail of a note arrives far quieter than
    // one drawn from the pick, and a cloud spread over a decaying note is
    // front-loaded however even its timing is. This lifts a grain towards how
    // loud the material has been lately, by the exponent below - 0 is off, 1
    // is as flat as the cap allows.
    //
    // Lift only, never cut: pulling the loud grains down would take the
    // transient with them, which is the one thing attackShare exists to keep.
    // Silence is left alone rather than lifted, or a quiet loop would simply
    // bring up its own noise floor.
    float sourceLevelling = 0.5f;

    // The ceiling on that lift, as a gain. 2.0 is +6 dB.
    float sourceLevelMaxBoost = 2.0f;

    // DENSITY FOLLOW. How much the input's own envelope drives the spawn
    // rate: play harder and the cloud thickens, stop and it thins out instead
    // of machine-gunning away at the same rate over nothing. Measured against
    // how loud the material has been lately rather than against an absolute
    // level, so it answers the same at any input gain.
    //
    // Free-running only. With Density synced the spawn timer is locked to the
    // host grid and moving its rate would be moving the grid.
    float densityFollow = 0.5f;

    // The thinnest the follow above may get, as a fraction of the Density
    // knob. Not zero: the cloud has to keep drawing from the note that was
    // struck while it rings, and attackShare has nothing to spawn from if the
    // timer has stopped.
    float densityFollowFloor = 0.15f;

    // FILTER SPRAY. One cloud lowpass shapes every grain the same way, so the
    // cloud diffuses in time but not in frequency and its top end moves as one
    // block. This gives each grain its own gentle tilt instead, at a corner
    // drawn log-uniformly from the span below: half of them come out darker
    // than the source and half brighter, so it is a scatter rather than a tone
    // control and the average is left where the Filter knob put it.
    //
    // The bright half pivots about its corner rather than lifting past it (see
    // Grainer::sprayed) - without that it is a straight +6 dB above the corner
    // on half the grains, which on its own took the engine sweep from 3.92 to
    // 4.30 and was most of why these four together first measured 5.17.
    float filterSpray = 0.3f;
    float filterSprayLowHz = 300.0f;
    float filterSprayHighHz = 4000.0f;

    // BAND SPLIT. A grain carries a pitch only if it holds a couple of cycles
    // of its own lowest content, so one grain length across the whole spectrum
    // is always a compromise: long enough for 40 Hz smears every transient,
    // short enough for a pick attack turns the bass into a thump. This splits
    // each spawn into one grain per band - same read position, so a transient
    // still lands as a single hit - and gives each the length its band's
    // wavelengths need.
    //   0.0 = off, one full-range grain per spawn, exactly as before
    //   1.0 = the full bandLengthRatio spread
    // Deliberately level-neutral: the per-band gain divides the overlap each
    // length change brings with it back out, so this is not a tilt EQ in
    // disguise. Use the Filter knob for tone.
    //
    // The default is held well below 1 because that compensation is
    // level-neutral in RMS but not in peak: the short high-band grain is
    // boosted by the square root of the ratio, and that is exactly where the
    // transients are. DspTests' noise sweep guards the raw engine at peak
    // 4.0, and with everything else at its default below this sits at 3.86 -
    // the split on its own was worth 3.22 at 0, 3.92 at 0.4, 4.90 at 1.0, so
    // it is the most expensive thing in this file per unit of effect. It gave
    // ground to the four features below rather than the other way round
    // because it is the one that measured as NOT helping the reference A/B.
    // Above ~0.3 needs that per-band gain rethinking first.
    float bandSplit = 0.25f;

    // The two crossovers. One-pole, so the bands sum back flat and nothing
    // rings for longer than the grain carrying it lasts.
    float bandLowHz = 200.0f;
    float bandHighHz = 2000.0f;

    // How far apart the band lengths run at bandSplit 1: the low band is this
    // many times the Size knob, the high band that many times shorter.
    float bandLengthRatio = 3.0f;

    // INTERVAL WEIGHTING. The three below all key off how far a grain is
    // transposed, read back out of its playback rate so Mod's drift counts
    // and both spawn paths are covered by one calculation. Every one of them
    // is inert at 0, and the engine takes its untouched path there.

    // Level tilt, in dB per octave of transposition. Negative is the usual
    // direction: an octave-up grain carries the same RMS as its source but
    // reads considerably louder, and an octave-down grain reads thinner, so
    // the cloud's pitch groups only balance against each other once this
    // leans against them. Not level-compensated anywhere - it is a tilt, and
    // the whole point is that it moves the balance.
    float pitchGainDbPerOctave = -3.0f;

    // How much the interval pulls a grain in from the pan position Stereo
    // gave it. Grains an octave or more down come to the centre, unison and
    // anything above keep the full width - so the sub of the cloud survives a
    // mono fold-down instead of half of it cancelling, and the image is built
    // from the material that can actually carry it.
    //   0.0 = pan is Stereo's business alone
    //   1.0 = two octaves down is dead centre
    float pitchPanSpread = 0.6f;

    // Reverb send weight per octave of transposition: the tank hears the
    // pitched-up grains and little of the low ones. This is what a shimmer
    // patch does by hand - a sub in a long tank is mud, an octave up in the
    // same tank is the effect - generalised onto the whole interval range.
    // Nonzero costs a second mono bus out of the engine and its own drive
    // stage; at 0 the reverb is fed exactly as it was before this existed.
    float pitchSendPerOctave = 0.35f;

    // Overlapping grains sum, so the engine divides by the square root of
    // the expected overlap. This trims the result back to roughly unity against
    // the dry signal.
    float outputTrim = 1.4f;

    // The reverb behind the cloud. BitBit Grain runs FdnReverb plain, with only
    // its mix and decay on the face; these two are the rest of its voicing.
    // Low resonance is the smeared, plate-like end, which suits a dense cloud;
    // the low cut is harder than BitBit Reverb idles at because grains stack up
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
    { "scatterMaxJitter", &GrainerTuning::scatterMaxJitter, 0.0f, 1.0f, 3 },
    { "scatterSizeJitter", &GrainerTuning::scatterSizeJitter, 0.0f, 1.0f, 3 },
    { "attackShare", &GrainerTuning::attackShare, 0.0f, 1.0f, 3 },
    { "smoothPeak", &GrainerTuning::smoothPeak, 0.05f, 0.95f, 3 },
    { "smoothAttackShareCut", &GrainerTuning::smoothAttackShareCut, 0.0f, 1.0f, 3 },
    { "grainLevelJitter", &GrainerTuning::grainLevelJitter, 0.0f, 1.0f, 3 },

    { "shapeAttackMsSoft", &GrainerTuning::shapeAttackMsSoft, 0.1f, 20.0f, 2 },
    { "shapeAttackMsHard", &GrainerTuning::shapeAttackMsHard, 0.1f, 20.0f, 2 },
    { "shapeDecayShapeSoft", &GrainerTuning::shapeDecayShapeSoft, 0.5f, 10.0f, 2 },
    { "shapeDecayShapeHard", &GrainerTuning::shapeDecayShapeHard, 0.5f, 10.0f, 2 },
    { "windowJitter", &GrainerTuning::windowJitter, 0.0f, 1.0f, 3 },

    { "sourceLevelling", &GrainerTuning::sourceLevelling, 0.0f, 1.0f, 3 },
    { "sourceLevelMaxBoost", &GrainerTuning::sourceLevelMaxBoost, 1.0f, 8.0f, 2 },

    { "densityFollow", &GrainerTuning::densityFollow, 0.0f, 1.0f, 3 },
    { "densityFollowFloor", &GrainerTuning::densityFollowFloor, 0.01f, 1.0f, 3 },

    { "filterSpray", &GrainerTuning::filterSpray, 0.0f, 1.0f, 3 },
    { "filterSprayLowHz", &GrainerTuning::filterSprayLowHz, 40.0f, 2000.0f, 0 },
    { "filterSprayHighHz", &GrainerTuning::filterSprayHighHz, 500.0f, 16000.0f, 0 },

    { "outputTrim", &GrainerTuning::outputTrim, 0.0f, 3.0f, 3 },

    { "bandSplit", &GrainerTuning::bandSplit, 0.0f, 1.0f, 3 },
    { "bandLowHz", &GrainerTuning::bandLowHz, 40.0f, 1000.0f, 0 },
    { "bandHighHz", &GrainerTuning::bandHighHz, 500.0f, 10000.0f, 0 },
    { "bandLengthRatio", &GrainerTuning::bandLengthRatio, 1.0f, 8.0f, 2 },

    { "pitchGainDbPerOctave", &GrainerTuning::pitchGainDbPerOctave, -12.0f, 12.0f, 2 },
    { "pitchPanSpread", &GrainerTuning::pitchPanSpread, 0.0f, 1.0f, 3 },
    { "pitchSendPerOctave", &GrainerTuning::pitchSendPerOctave, -1.0f, 2.0f, 3 },

    { "verbResonance", &GrainerTuning::verbResonance, 0.0f, 1.0f, 3 },
    { "verbLowCutHz", &GrainerTuning::verbLowCutHz, 20.0f, 800.0f, 0 },
};

} // namespace ee::dsp
