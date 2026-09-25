#pragma once

#include <array>

namespace ee::dsp::eq
{

/** The shapes one band of ee::dsp::Equaliser can take, in the order the face's
    type menu lists them (Ableton EQ Eight's order: the cuts outside, the
    shelves inside them, bell and notch in the middle). This order is also a
    parameter's choice list - BitBit Alpine's `eq.bN.type` - so it is frozen:
    append, never reorder. */
enum class FilterType
{
    lowCut48,
    lowCut12,
    lowShelf,
    bell,
    notch,
    highShelf,
    highCut12,
    highCut48,
};

inline constexpr int kNumTypes = 8;

constexpr bool isCut (FilterType t) noexcept
{
    return t == FilterType::lowCut48 || t == FilterType::lowCut12 || t == FilterType::highCut12
        || t == FilterType::highCut48;
}

/** Whether a shape has a gain at all. The cuts and the notch do not: their
    Gain is ignored, and the face parks their dot on the 0 dB line. */
constexpr bool hasGain (FilterType t) noexcept
{
    return t == FilterType::lowShelf || t == FilterType::bell || t == FilterType::highShelf;
}

// --------------------------------------------------------------------- travel
inline constexpr float kMinHz     = 20.0f;
inline constexpr float kMaxHz     = 20000.0f;
inline constexpr float kHzSkewCentre = 632.0f; // geometric middle of 20..20k, so the knob's centre is its log centre
inline constexpr float kMaxGainDb = 15.0f;     // +/- on every band, as EQ Eight's
inline constexpr float kMinQ      = 0.1f;
inline constexpr float kMaxQ      = 18.0f;
inline constexpr float kQSkewCentre = 1.0f;
inline constexpr float kDefaultQ  = 0.71f;
// The cuts' resonance stops here (+15.6 dB at the corner) whatever the Q knob
// says - the pre-EQ feeds Alpine's drive stages, and the full kMaxQ on a cut
// is a +25 dB spike into them. Bells and the notch keep the whole range.
inline constexpr float kMaxCutQ   = 6.0f;

// ----------------------------------------------------------------- the Simple
// Three fixed bands at Ableton EQ Three's default split - FreqLow 250 Hz,
// FreqHi 2.5 kHz - with the mid centred between them (their geometric mean).
// Shelves and a broad bell rather than EQ Three's crossover: each knob moves
// the same region, and a band left at 0 dB is not in the signal at all.
inline constexpr float kSimpleLowHz   = 250.0f;
inline constexpr float kSimpleMidHz   = 790.0f;
inline constexpr float kSimpleHighHz  = 2500.0f;
inline constexpr float kSimpleShelfQ  = 0.71f;
inline constexpr float kSimpleMidQ    = 0.5f; // ~2.6 octaves - covers the region between the two shelves

// ---------------------------------------------------------------- the Advanced
inline constexpr int kAdvancedBands = 8;

struct BandDefault
{
    FilterType type;
    float hz;
    bool on;
};

/** Where the eight bands start: EQ Eight's spread, the two cuts parked off at
    the ends. Every gain starts at 0 dB and every Q at kDefaultQ, so a fresh
    instance is flat and - see Equaliser - not in the signal path at all. */
inline constexpr std::array<BandDefault, kAdvancedBands> kAdvancedDefaults { {
    { FilterType::lowCut48,  40.0f,    false },
    { FilterType::lowShelf,  100.0f,   true },
    { FilterType::bell,      250.0f,   true },
    { FilterType::bell,      600.0f,   true },
    { FilterType::bell,      1500.0f,  true },
    { FilterType::bell,      3500.0f,  true },
    { FilterType::highShelf, 8000.0f,  true },
    { FilterType::highCut48, 16000.0f, false },
} };

// ------------------------------------------------------------------ smoothing
inline constexpr float kSmoothSeconds  = 0.02f;  // freq / gain / Q glide, one-pole
inline constexpr float kFadeSeconds    = 0.015f; // a band fading in or out (on/off, type change)
inline constexpr int   kControlInterval = 16;    // samples between coefficient updates while gliding
inline constexpr float kNeutralDb      = 0.01f;  // a shelf or bell this close to 0 dB is left out of the path

// 8th-order Butterworth, one Q per biquad section - the 48 dB/oct cuts. The
// band's own Q scales the last (resonant) section only, so kDefaultQ is a
// flat Butterworth corner and more Q is a peak at the corner, as on EQ Eight.
inline constexpr std::array<double, 4> kButterworth8Q { { 0.50979558, 0.60134489, 0.89997622, 2.56291545 } };
inline constexpr double kButterworthQ = 0.70710678;

} // namespace ee::dsp::eq
