#pragma once

namespace ee::dsp
{

/** Every number that shapes Easy Reverb's shimmer path, in one place.

    A pitch-shifted tap of the wet tail is folded back into the network, so each
    pass round the loop stacks another octave on top; the Shimmer knob is the
    feedback gain of that path. Two shifters run in parallel, read a predelay a
    Haas offset apart, for a wide, moving image.

    Kept as a struct rather than constants so the development tuning panel can
    drive them live, and so the whole voicing can be read at a glance. The
    defaults below are a tuned setting; build with -DEE_SHIMMER_TUNER=ON to
    bring the live panel back.
*/
struct ShimmerTuning
{
    // Interval both shifters centre on (+12 = one octave), and how far either
    // side of it they sit.
    float semitones = 12.0f;
    float detuneSemis = 0.0f;

    // Feedback gain at the top of the knob, and the knob taper. Held below 1.0
    // so the octave stack cannot build without bound. Skew > 1 pushes the
    // useful part of the range towards the top of the knob's travel.
    float maxFeedback = 0.555f;
    float skew = 3.0f;

    // Band limits on each side of the feedback.
    float lowCutHz = 20.0f;
    float highCutHz = 3200.0f;

    // A low shelf on the feedback for body / weight under the octave. bass is
    // the lift amount (0 = flat, ~0.5 = +3.5 dB), bassHz the corner. It
    // compounds every pass, so a high value fights the sub the highpass is
    // there to keep out - keep it under ~0.8 or raise lowCutHz to match.
    float bass = 0.0f;
    float bassHz = 200.0f;

    // A gentle high shelf on top for sparkle - it compounds as the stack
    // builds. sparkle is the lift amount (0 = flat), sparkleHz the corner.
    float sparkle = 0.0f;
    float sparkleHz = 800.0f;

    // The octave feedback is predelayed this much (scaled off the decay knob
    // between the two) before re-injection, so it blooms behind the dry note.
    float predelayMinMs = 38.4f;
    float predelayMaxMs = 147.1f;

    // How far apart the two sides read the predelay line, for stereo width on
    // top of the detune.
    float haasMs = 0.44f;

    // How much of the L/R difference is injected. 1.0 is the full decorrelated
    // difference; above that over-drives the sides for a wider image. Past ~2
    // the difference feeds enough energy back into the loop to stretch the
    // tail, so this is about as far as it goes at this feedback setting.
    float width = 2.0f;

    // DaisySP's internal random delay modulation (its "fun" control). With the
    // detune at zero this is what decorrelates the two shifters.
    float flutter = 0.817f;
};

/** Describes a field for the tuning panel, and names it as the source does. */
struct ShimmerTuningEntry
{
    const char* name;
    float ShimmerTuning::* member;
    float minimum;
    float maximum;
    int decimals;
};

inline constexpr ShimmerTuningEntry kShimmerTuningEntries[] = {
    { "semitones",     &ShimmerTuning::semitones,       0.0f,    24.0f,   2 },
    { "detuneSemis",   &ShimmerTuning::detuneSemis,     0.0f,     0.5f,   3 },

    { "maxFeedback",   &ShimmerTuning::maxFeedback,     0.0f,     0.98f,  3 },
    { "skew",          &ShimmerTuning::skew,            0.5f,     3.0f,   2 },

    { "lowCutHz",      &ShimmerTuning::lowCutHz,       20.0f,   600.0f,   0 },
    { "highCutHz",     &ShimmerTuning::highCutHz,    2000.0f, 20000.0f,   0 },

    { "bass",          &ShimmerTuning::bass,            0.0f,     2.0f,   3 },
    { "bassHz",        &ShimmerTuning::bassHz,         60.0f,   800.0f,   0 },

    { "sparkle",       &ShimmerTuning::sparkle,         0.0f,     2.0f,   3 },
    { "sparkleHz",     &ShimmerTuning::sparkleHz,     800.0f, 12000.0f,   0 },

    { "predelayMinMs", &ShimmerTuning::predelayMinMs,   0.0f,   200.0f,   1 },
    { "predelayMaxMs", &ShimmerTuning::predelayMaxMs,  20.0f,   300.0f,   1 },
    { "haasMs",        &ShimmerTuning::haasMs,          0.0f,    30.0f,   2 },

    { "width",         &ShimmerTuning::width,           0.0f,     3.0f,   3 },
    { "flutter",       &ShimmerTuning::flutter,         0.0f,     1.0f,   3 },
};

/** Predelay buffer is sized for this so the panel can push the predelay up to
    its slider maximum without the delay line clamping. */
inline constexpr float kShimmerPredelayCeilingMs = 360.0f;

} // namespace ee::dsp
