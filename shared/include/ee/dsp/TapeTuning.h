#pragma once

namespace ee::dsp
{

/** Every number that shapes the tape stage, in one place.

    These are the values at Tape 100 %; the knob scales them. Kept as a struct
    rather than constants in the class so the development tuning panel can drive
    them live, and so the whole voicing can be read at a glance.
*/
struct TapeTuning
{
    // Wow and flutter. Three one-pole filters on white noise, at corners spread
    // apart, approximate the 1/f spread a real machine has.
    float modRmsSeconds = 0.000195f;
    float modCornerLowHz = 2.0f;
    float modCornerMidHz = 12.0f;
    float modCornerHighHz = 70.0f;
    float wobbleLimitSamples = 48.0f;

    // Saturation and head loss.
    float maxDrive = 5.53f;
    float maxBias = 0.0126f;
    float maxShelfLoss = 0.04f;
    float maxTrim = 0.086f;
    float shelfCornerHz = 1600.0f;
    float dcCornerHz = 18.0f;

    // Gap loss: a 12 dB/octave lowpass mixed in linearly with the knob, so it
    // is always on the tape signal and never on the untouched one.
    float hiCutHz = 8430.0f;
    float hiCutQ = 0.53f;

    // Grit. It rides the programme, so it needs an envelope and a reference
    // level to be calibrated against.
    float maxNoise = 0.0f;
    float noiseHighpassHz = 3000.0f;
    float noiseLowpassHz = 3600.0f;
    float envelopeCornerHz = 6.0f;
    float envReference = 0.02f;
    float maxTilt = 3.0f;
};

/** Describes a field for the tuning panel, and names it as the source does. */
struct TapeTuningEntry
{
    const char* name;
    float TapeTuning::* member;
    float minimum;
    float maximum;
    int decimals;
};

inline constexpr TapeTuningEntry kTapeTuningEntries[] = {
    { "modRmsSeconds",      &TapeTuning::modRmsSeconds,      0.0f,    0.0012f, 6 },
    { "modCornerLowHz",     &TapeTuning::modCornerLowHz,     0.2f,   10.0f,    2 },
    { "modCornerMidHz",     &TapeTuning::modCornerMidHz,     2.0f,   40.0f,    2 },
    { "modCornerHighHz",    &TapeTuning::modCornerHighHz,   20.0f,  300.0f,    1 },
    { "wobbleLimitSamples", &TapeTuning::wobbleLimitSamples, 4.0f,  200.0f,    0 },

    { "maxDrive",           &TapeTuning::maxDrive,           0.0f,    6.0f,    3 },
    { "maxBias",            &TapeTuning::maxBias,            0.0f,    0.3f,    4 },
    { "maxShelfLoss",       &TapeTuning::maxShelfLoss,       0.0f,    0.6f,    4 },
    { "maxTrim",            &TapeTuning::maxTrim,           -0.3f,    0.5f,    4 },
    { "shelfCornerHz",      &TapeTuning::shelfCornerHz,    200.0f, 12000.0f,   0 },
    { "dcCornerHz",         &TapeTuning::dcCornerHz,         5.0f,  200.0f,    1 },

    { "hiCutHz",            &TapeTuning::hiCutHz,         2000.0f, 20000.0f,   0 },
    { "hiCutQ",             &TapeTuning::hiCutQ,             0.2f,    2.0f,    2 },

    { "maxNoise",           &TapeTuning::maxNoise,           0.0f,    2.0f,    4 },
    { "noiseHighpassHz",    &TapeTuning::noiseHighpassHz,  200.0f, 12000.0f,   0 },
    { "noiseLowpassHz",     &TapeTuning::noiseLowpassHz,   200.0f, 16000.0f,   0 },
    { "envelopeCornerHz",   &TapeTuning::envelopeCornerHz,   0.5f,   60.0f,    2 },
    { "envReference",       &TapeTuning::envReference,       0.002f,  0.2f,    4 },
    { "maxTilt",            &TapeTuning::maxTilt,            1.0f,   12.0f,    2 },
};

} // namespace ee::dsp
