#pragma once

#include <array>

namespace ee::dsp
{

/**
 * Voicing for SpaceReverb - BitBit Reverb's and BitBit Alpine's Studio engine.
 *
 * Fitted against NI Raum's Airy mode at Size 70 %, Diffusion 25 %, Modulation
 * ~18 %, measured offline through ee_plugin_render (the installed AU, driven
 * like a host). The three reference bounces this was matched to were Decay
 * 2 s / Damp 25 / Mix 20, 8 s / 60 / 30 and 2.5 s / 86 / 35; the tables below
 * were then filled from a 5 x 5 Decay x Damp grid of impulse responses. What
 * the measurements showed, and so what the engine is:
 *
 *   - Two strong discrete echoes, then a sparse field that thickens over
 *     ~150 ms. Each input has an echo on its own side and one on the other
 *     side a few ms later; 84 % of the first 40 ms is in those few samples.
 *     The early part is identical at every Decay.
 *   - Decay is the RT60 of the low mids, literally.
 *   - Damp tilts the top down inside the loop: only the top shortens, and
 *     how much depends on Decay as well as Damp (hence a table, not a formula).
 *   - The wet is spectrally flat - no body shelf, no scoop.
 *   - Fully decorrelated left/right after the first echoes.
 *
 * A struct rather than loose constants so the offline fit tool can override
 * any field; the defaults are the tuned voicing.
 */
struct SpaceVoicing
{
    static constexpr int kLines = 16;
    static constexpr int kDecayPoints = 5; // 0.5, 1, 2, 4, 8 s
    static constexpr int kDampPoints = 5;  // 0, 25, 50, 75, 100 %

    // --------------------------------------------------------------- early
    // Where each input's two echoes land, after the Pre-delay knob. Same side
    // first, then across; the two cross paths are a couple of ms apart so a
    // centred source still opens up.
    float earlySameMs = 17.70f;
    float earlyLeftToRightMs = 20.10f;
    float earlyRightToLeftMs = 22.06f;
    float earlyGain = 0.266f;

    // The echoes pass the Damp filter once, as if they had made one trip of a
    // loop this long - at the damping of a Decay of sqrt (2 s x Decay), because
    // Raum's early echoes darken with Damp far less at short decays than its
    // tail does (5.6 dB at 0.5 s and Damp 100, against the tail's collapse).
    float earlyDampTripMs = 9.0f;

    // ---------------------------------------------------------------- late
    // Sixteen lines and a Hadamard matrix, with no allpasses in the loop.
    // Sixteen, not eight: with eight (25-53 ms) every line came back into
    // itself at 1/8 of the loop's energy per trip, and the tail repeated -
    // its autocorrelation held a peak of ~0.3 at one line's length (35 ms) in
    // the 500 Hz - 1 kHz octaves all the way down, against Raum's 0.10-0.15,
    // which is its noise floor. Heard as the sound bouncing in the room at a
    // long Decay and a wet Mix. Sixteen halves that recurrence and the wide
    // spread (22-122 ms, geometric) doubles the mode density again; together
    // they bring every octave to Raum's floor. Neither permuting the matrix nor
    // an allpass inside each line moved it. Mutually prime-ish, the shortest
    // just behind the early echoes so the field arrives after them.
    std::array<float, kLines> lineMs { 21.73f, 24.44f, 27.83f, 30.96f, 34.17f, 38.65f, 43.34f, 48.06f,
                                      54.75f, 60.18f, 67.95f, 76.37f, 85.30f, 95.96f, 107.94f, 121.65f };

    // Slow, shallow line movement - enough to stop the modes standing still,
    // well short of a chorus. 0.35 ms spreads a held 1 kHz tone in the tail by
    // ~4 Hz, which is Raum's own spread (4.7 Hz); 0.5 ms was twice that.
    std::array<float, kLines> lfoHz { 0.31f, 0.67f, 0.43f, 0.89f, 0.53f, 1.03f, 0.61f, 0.37f,
                                     0.97f, 0.71f, 0.47f, 1.09f, 0.83f, 0.57f, 0.29f, 0.77f };
    float modDepthMs = 0.35f;

    // Gain on the late taps, at a 2 s Decay (it is 1/sqrt(lines) on top, and
    // the output level of a network falls with its total length, hence > 1). The late level follows Decay as
    // Raum's does - about +2.5 dB per doubling, less at the long end where the
    // top has already died - which this network overshoots slightly on its own;
    // the exponent trims it: gain x (2 s / Decay) ^ exponent. Set per octave,
    // 250 Hz - 4 kHz, not broadband: a white impulse's energy is mostly its
    // top octave, and matching that hid a tail 1-2 dB thin where music lives.
    float lateGain = 1.30f;
    float lateGainDecayExponent = 0.06f;

    // Allpass diffusers on what feeds the lines, one chain for the mid and one
    // for the side (lengths differ so the two stay apart). Without them each
    // line's first return arrived as a bare spike: 40 ms after a hit the field
    // measured an echo density of 0.04 against Raum's 0.39, which at a long
    // Decay and a wet Mix is heard as the sound bouncing around the room. An
    // allpass passes all its energy, so decay and tone are untouched. 0.3
    // tracks Raum's density through the first 80 ms (0.27 / 0.36 / 0.42 at
    // 30 / 40 / 60 ms against 0.26 / 0.39 / 0.42); 0.5 was already denser than
    // Raum by 30 ms.
    static constexpr int kFeedDiffusers = 4;
    std::array<float, kFeedDiffusers> feedDiffuserMidMs { 1.53f, 2.87f, 4.61f, 6.83f };
    std::array<float, kFeedDiffusers> feedDiffuserSideMs { 1.79f, 3.17f, 5.03f, 7.41f };
    float feedDiffusion = 0.3f;

    // One-pole lowpass on what feeds the lines - Dattorro's "input bandwidth".
    // Raum's tail carries ~4 dB less at 16 kHz than its echoes do; without this
    // the network's own flat feed left the tail bright and, with the level
    // matched broadband, 1-2 dB thin everywhere a mix actually lives.
    float lateBandwidthHz = 9000.0f;

    // RT60 = Decay x this, in the low mids, by Decay (0.5, 1, 2, 4, 8 s).
    // Raum rings a little past its knob at the short end and exactly on it
    // from 4 s up.
    std::array<float, kDecayPoints> decayScale { 1.085f, 1.08f, 1.05f, 0.98f, 0.99f };

    // ------------------------------------------------------------- damping
    // Excess absorption over the mid-band decay, in dB per second, at 2 kHz,
    // 5.66 kHz and 16 kHz, by Decay (rows: 0.5, 1, 2, 4, 8 s) and Damp (columns: 0, 25,
    // 50, 75, 100 %). Interpolated on log2(Decay) and Damp. Read straight off
    // Raum's impulse responses (octave-band T20 against the 250/500 Hz decay).
    //
    // Each line gets two first-order high shelves in series, solved so its trip
    // loses exactly the tabled amount at all three frequencies. Raum's damping
    // is a gentle tilt, not a lowpass - at Damp 100 the loss per trip rises
    // under 1 dB an octave through the mids - and a one-pole cannot draw that;
    // one shelf pinned at the two ends bowed, leaving 4-8 kHz ~20 % short at
    // high Damp, which the middle pin fixes. Pinned this wide on purpose: pinned at 8 kHz, a steep table
    // entry left the shelf climbing on above it and everything past 10 kHz -
    // half the energy of a bright source - died in a few trips. The column at
    // Damp 0 is not zero: Raum loses the top a little faster than the mids even
    // there.
    //
    // What a first-order section cannot do is follow both the mids and the top
    // at Damp 100; there the 1-2 kHz decay runs ~15 % long at 4-8 s. Everywhere
    // else the octave RTs land within ~10 % of Raum's from 125 Hz to 16 kHz.
    float dampLowHz = 2000.0f;
    float dampHighHz = 16000.0f;
    using DampTable = std::array<std::array<float, kDampPoints>, kDecayPoints>;
    DampTable dampLow { {
        { 9.6f, 11.0f, 20.4f, 47.7f, 56.5f },  // 0.5 s
        { 1.0f, 1.0f, 2.0f, 12.2f, 17.4f },  // 1 s
        { 3.2f, 3.4f, 4.5f, 10.2f, 19.8f },  // 2 s
        { 0.1f, 0.2f, 0.5f, 2.2f, 23.3f },  // 4 s
        { 0.3f, 0.4f, 1.1f, 2.9f, 26.0f },  // 8 s
    } };
    // At the geometric middle of the two (5.66 kHz): Raum's curve between them
    // is close to - but not quite - a power law, so the middle is tabled too
    // rather than assumed. See SpaceReverb::designTrip.
    DampTable dampMid { {
        { 14.5f, 26.4f, 68.4f, 158.0f, 351.0f },
        { 0.5f, 1.8f, 15.9f, 59.2f, 132.5f },
        { 5.1f, 6.9f, 11.9f, 30.3f, 65.9f },
        { 2.9f, 3.9f, 6.1f, 14.4f, 52.9f },
        { 3.1f, 3.9f, 8.2f, 17.8f, 51.2f },
    } };
    DampTable dampHigh { {
        { 42.2f, 96.7f, 207.8f, 389.5f, 1640.7f },
        { 6.7f, 28.8f, 77.9f, 183.7f, 775.3f },
        { 15.2f, 22.8f, 41.7f, 95.3f, 324.6f },
        { 12.5f, 14.8f, 19.6f, 45.0f, 272.6f },
        { 16.4f, 18.9f, 34.7f, 53.5f, 267.3f },
    } };
};

} // namespace ee::dsp
