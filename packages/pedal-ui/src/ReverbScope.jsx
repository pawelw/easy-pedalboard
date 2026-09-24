import "./ModScope.css";

/**
 * One display for all three reverb engines, in the Modulation module's line
 * style: the tail drawn as a plain sine dying away along a centre line, from
 * each engine's own decay model. What moves it:
 *
 *   all      Decay: how long the wave takes to fall to the line (drawn on a dB
 *            scale, so the whole 60 dB reads). Hi Cut and Low Cut: a lid
 *            coming down and a floor coming up, limiting how far the upper and
 *            lower lobes swing - the trace stays one unbroken sine. Damping: the top end dies faster than the body,
 *            so the lid closes further as the tail goes on - a Hi Cut that
 *            tightens over time; nothing at Damping 0. The boundaries are not
 *            drawn - only the wave they leave.
 *   Spring   ee/dsp/SpringConfig.h - the level swells once per trip round the
 *            tank (kSpringMs), the swells fading into the wash. Tension
 *            quickens the wave: the chirp (kChirpStages, kChirpDelayMs,
 *            kChirpCoefficient +/- kTensionSpan) boings higher on a tauter
 *            spring.
 *   Shimmer  ee/dsp/ReverbConfig.h's band ratios and Damping range, and
 *            ShimmerTuning's feedback trip: at +1 the wave quickens trip by
 *            trip, at -1 it slows, at 0 it holds.
 *   Studio   ee/dsp/SpaceConfig.h - decayScale and the Raum-fitted damping
 *            tables (as SpaceReverb::dampAt reads them). Pre-delay is a flat
 *            run before the wave starts. Size scales every travel time in the
 *            room (earlySameMs and the rest), so a bigger room starts later
 *            and swings slower and wider. It does not change the decay - the
 *            engine holds RT60 on the Decay knob at any Size.
 *
 * Every number is a hand copy of the engine's own. The wave is a picture of
 * the tail's motion, not its real pitch. Props are the knobs' normalised 0..1
 * positions, as the face reads them; the ranges and skews below are the
 * processors' NormalisableRanges (BitBit Reverb's and BitBit Alpine's are the
 * same). `octave` is the Shimmer choice index, 0..2 for -1 / 0 / +1. Drawn from
 * the knob positions only - it does not animate.
 */

// ------------------------------------------------------------------ ranges

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, Number.isFinite(v) ? v : lo));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

/** A JUCE NormalisableRange with setSkewForCentre, from normalised to real. */
function fromNormalised(v01, min, max, centre) {
  const v = clamp(v01, 0, 1);
  if (centre === undefined) return min + (max - min) * v;
  const skew = Math.log(0.5) / Math.log((centre - min) / (max - min));
  return min + (max - min) * Math.exp(Math.log(Math.max(v, 1e-9)) / skew);
}

const CUT_RANGE = { lo: [20, 800, 180], hi: [1000, 20000, 6000] };
const DECAY_RANGE = {
  spring: [0.4, 8, 2.2], // spring::kMin/MaxDecaySeconds, kDecaySkewCentre
  shimmer: [4, 10], // ReverbModule::kMin/MaxShimmerDecay, linear
  studio: [0.5, 8, 2], // SpaceReverb::kMin/MaxDecay, skew centre 2 s
};
const STUDIO_SIZE = [0.5, 1.5];
const STUDIO_PREDELAY_MS = [0, 60];

// ------------------------------------------------------------------ axes

// Time is linear for the first TIME_KNEE and compresses after it, so a 60 ms
// pre-delay and a 10 s shimmer both read in 140px.
const T_MAX = 10;
const TIME_KNEE = 0.05;


/** 0 an octave below `corner`, 1 an octave above - a first-order shelf's
    transition, on a log-frequency axis. Two octaves wide, not one: that is
    about what the engines' own shelves take, and one read as a step. */
function shelf(f, corner) {
  const u = clamp((Math.log2(f / corner) + 1) / 2, 0, 1);
  return u * u * (3 - 2 * u);
}

// ------------------------------------------------------------------ Spring

const SPRING_LOW_RATIO = 0.09;
const SPRING_HIGH_RATIO = 0.74;
const SPRING_LOW_CORNER = 198;
const SPRING_HIGH_CORNER = 5760;
const SPRING_TRIP_S = (38.6 + 44.9 + 51.7) / 3 / 1000; // kSpringMs, averaged
const CHIRP_STAGES = 12;
const CHIRP_DELAY_S = 0.3 / 1000;
const CHIRP_COEFF = 0.62; // magnitude; negative in the engine
const TENSION_SPAN = 0.16;

function springModel({ decay01, tension01, lowCut01, highCut01 }) {
  const decay = fromNormalised(decay01, ...DECAY_RANGE.spring);
  const lo = fromNormalised(lowCut01, ...CUT_RANGE.lo);
  const hi = fromNormalised(highCut01, ...CUT_RANGE.hi);

  // How much later the top of a bounce arrives than the bottom: a first-order
  // all-pass's group delay runs (1-a)/(1+a) .. (1+a)/(1-a) sections, over
  // kChirpStages stretched sections of kChirpDelayMs. Tension moves `a`.
  const a = clamp(CHIRP_COEFF + TENSION_SPAN * (2 * clamp(tension01, 0, 1) - 1), 0.05, 0.95);
  const dispersion = CHIRP_STAGES * CHIRP_DELAY_S * ((1 + a) / (1 - a) - (1 - a) / (1 + a));

  return {
    lo,
    hi,
    start: 0,
    // One swell per trip round the tank. A tauter spring chirps harder and
    // boings higher, so Tension quickens the wave.
    bounce: { period: SPRING_TRIP_S + dispersion / 2 },
    period: 1 / (1 + 0.35 * (dispersion / 0.015)),
    t60: (f) =>
      decay *
      Math.min(
        lerp(SPRING_LOW_RATIO, 1, shelf(f, SPRING_LOW_CORNER)),
        lerp(1, SPRING_HIGH_RATIO, shelf(f, SPRING_HIGH_CORNER)),
      ),
  };
}

// ------------------------------------------------------------------ Shimmer

const FDN_LOW_RATIO = 0.4;
const FDN_LOW_CORNER = 450;
const FDN_HIGH_CORNER = 5000;
const FDN_DAMP_MAX_RATIO = 0.75; // Damping 0
const FDN_DAMP_MIN_RATIO = 0.15; // Damping 100
// Where the octave feedback's tilt pivots, and how far it leans per octave
// away from there. Illustrative, not measured: the feedback is band-limited
// (ShimmerTuning::highCutHz) and runs into the same damping, so the lean is
// modest rather than doubling the top's decay.
const FDN_DECAY_SPAN = [0.5, 10]; // FdnReverb::kMin/MaxDecay
const SHIMMER_TRIP_MS = [38.4, 147.1]; // ShimmerTuning::predelayMin/MaxMs, by Decay
const SHIMMER_PIVOT_HZ = 700;
const SHIMMER_TILT = 0.18;

function shimmerModel({ decay01, damping01, lowCut01, highCut01, octave }) {
  const decay = fromNormalised(decay01, ...DECAY_RANGE.shimmer);
  const lo = fromNormalised(lowCut01, ...CUT_RANGE.lo);
  const hi = fromNormalised(highCut01, ...CUT_RANGE.hi);
  const highRatio = lerp(FDN_DAMP_MAX_RATIO, FDN_DAMP_MIN_RATIO, clamp(damping01, 0, 1));
  const shift = clamp(Math.round(octave ?? 2), 0, 2) - 1;
  const along = clamp((decay - FDN_DECAY_SPAN[0]) / (FDN_DECAY_SPAN[1] - FDN_DECAY_SPAN[0]), 0, 1);

  return {
    lo,
    hi,
    start: 0,
    // Each trip of the octave feedback moves the tail's pitch an octave.
    octave: { shift, trip: lerp(...SHIMMER_TRIP_MS, along) / 1000 },
    t60: (f) => {
      const ratio = Math.min(
        lerp(FDN_LOW_RATIO, 1, shelf(f, FDN_LOW_CORNER)),
        lerp(1, highRatio, shelf(f, FDN_HIGH_CORNER)),
      );
      const lean = 1 + shift * SHIMMER_TILT * clamp(Math.log2(f / SHIMMER_PIVOT_HZ), -3, 3);
      return decay * ratio * Math.max(0.3, lean);
    },
  };
}

// ------------------------------------------------------------------ Studio

// SpaceVoicing: decayScale and the three excess-absorption tables (dB/s), rows
// by Decay (0.5, 1, 2, 4, 8 s), columns by Damp (0, 25, 50, 75, 100 %).
const SPACE_DECAY_SCALE = [1.085, 1.08, 1.05, 0.98, 0.99];
const SPACE_DAMP_HZ = [2000, Math.sqrt(2000 * 16000), 16000];
const SPACE_DAMP = [
  [
    [9.6, 11.0, 20.4, 47.7, 56.5],
    [1.0, 1.0, 2.0, 12.2, 17.4],
    [3.2, 3.4, 4.5, 10.2, 19.8],
    [0.1, 0.2, 0.5, 2.2, 23.3],
    [0.3, 0.4, 1.1, 2.9, 26.0],
  ],
  [
    [14.5, 26.4, 68.4, 158.0, 351.0],
    [0.5, 1.8, 15.9, 59.2, 132.5],
    [5.1, 6.9, 11.9, 30.3, 65.9],
    [2.9, 3.9, 6.1, 14.4, 52.9],
    [3.1, 3.9, 8.2, 17.8, 51.2],
  ],
  [
    [42.2, 96.7, 207.8, 389.5, 1640.7],
    [6.7, 28.8, 77.9, 183.7, 775.3],
    [15.2, 22.8, 41.7, 95.3, 324.6],
    [12.5, 14.8, 19.6, 45.0, 272.6],
    [16.4, 18.9, 34.7, 53.5, 267.3],
  ],
];
// SpaceVoicing's earlySameMs: when the room first answers, scaled by Size.
// Size scales every travel time in the room and not the decay (the engine
// holds RT60 on the Decay knob at any Size), so it is drawn as time: a later
// start and a slower, wider wave in a bigger room.
const SPACE_EARLY_MS = 17.7;

/** SpaceReverb::dampAt - bilinear on log2(Decay / 0.5 s) and Damp. */
function dampAt(table, decay, damping01) {
  const x = clamp(Math.log2(decay / 0.5), 0, 4);
  const y = clamp(damping01 * 4, 0, 4);
  const x0 = Math.min(3, Math.floor(x));
  const y0 = Math.min(3, Math.floor(y));
  const top = lerp(table[x0][y0], table[x0][y0 + 1], y - y0);
  const bottom = lerp(table[x0 + 1][y0], table[x0 + 1][y0 + 1], y - y0);
  return Math.max(0, lerp(top, bottom, x - x0));
}

function studioModel({ decay01, damping01, lowCut01, highCut01, size01, predelay01 }) {
  const decay = fromNormalised(decay01, ...DECAY_RANGE.studio);
  const lo = fromNormalised(lowCut01, ...CUT_RANGE.lo);
  const hi = fromNormalised(highCut01, ...CUT_RANGE.hi);
  const size = fromNormalised(size01, ...STUDIO_SIZE);
  const predelay = fromNormalised(predelay01, ...STUDIO_PREDELAY_MS) / 1000;
  const damping = clamp(damping01, 0, 1);

  const xs = clamp(Math.log2(decay / 0.5), 0, 4);
  const x0 = Math.min(3, Math.floor(xs));
  const rt60 = decay * lerp(SPACE_DECAY_SCALE[x0], SPACE_DECAY_SCALE[x0 + 1], xs - x0);

  // Excess absorption at the three pins, kept rising the way the engine's
  // shelves are, interpolated on log frequency and tapered to nothing an
  // octave under the lowest.
  const pins = [];
  for (let i = 0; i < 3; i++) pins.push(Math.max(pins[i - 1] ?? 0, dampAt(SPACE_DAMP[i], rt60, damping)));
  const excess = (f) => {
    const [a, b, c] = SPACE_DAMP_HZ;
    if (f <= a) return pins[0] * clamp(Math.log2(f / (a / 2)), 0, 1);
    if (f <= b) return lerp(pins[0], pins[1], Math.log(f / a) / Math.log(b / a));
    return lerp(pins[1], pins[2], clamp(Math.log(f / b) / Math.log(c / b), 0, 1.5));
  };

  return {
    lo,
    hi,
    start: predelay + (SPACE_EARLY_MS * size) / 1000,
    period: size,
    t60: (f) => 60 / (60 / rt60 + excess(f)),
  };
}

const MODELS = { spring: springModel, shimmer: shimmerModel, studio: studioModel };

// ------------------------------------------------------------------ drawing

const VIEW_W = 140;
const VIEW_H = 63;
const PAD_X = 8;
const PAD_Y = 7;
const SAMPLES = 150;

// The trace's own wiggle, in cycles per drawn sample - a picture of the tail's
// motion, not its real pitch (which would be a solid block at this width).
const BASE_CPS = 0.07;

/** The wave, one sample per drawn point: its envelope (0..1) and its carrier
    (-1..1), kept apart so the cuts can limit the one without bending the other. */
function tailWave(model) {
  const logSpan = Math.log(1 + T_MAX / TIME_KNEE);
  const tAt = (i) => TIME_KNEE * (Math.exp((i / SAMPLES) * logSpan) - 1);

  const midT60 = model.t60(1000);

  let phase = 0;
  const env = [];
  const carrier = [];
  for (let i = 0; i <= SAMPLES; i++) {
    const age = tAt(i) - model.start;
    if (age < 0) {
      env.push(0);
      carrier.push(0);
      continue;
    }

    // The body falls away 60 dB over its RT60, drawn on a dB scale so the
    // whole decay is visible rather than just its first 20 dB.
    let level = clamp(1 - age / midT60, 0, 1);
    let cps = BASE_CPS / (model.period ?? 1);

    // Spring: the level swells once per trip, the swells fading into the wash.
    if (model.bounce) {
      const within = (age / model.bounce.period) % 1;
      const k = Math.floor(age / model.bounce.period);
      const contrast = 0.45 * Math.exp(-k / 6);
      level *= 1 - contrast + contrast * (0.5 + 0.5 * Math.cos(2 * Math.PI * within));
    }

    // Shimmer: each feedback trip moves the tail an octave, so the wave
    // quickens (+1) or slows (-1) as it goes.
    if (model.octave && model.octave.shift !== 0) {
      const steps = Math.min(3, age / model.octave.trip);
      cps *= Math.pow(2, model.octave.shift * steps * 0.35);
    }

    phase += cps;
    env.push(level);
    carrier.push(Math.sin(2 * Math.PI * phase));
  }
  return { env, carrier };
}

/** How far in the Damping line has come at each drawn sample, 0..1 of its
    reach. Its strength is how much faster the knob makes the engine lose the
    top (8 kHz) than the body (1 kHz), counted in the tail's own lengths: the
    extra dB/s times the body's RT60, over 60 - so "the top goes k tail-lengths'
    worth faster". It is measured against Damping 0, because the engines lose
    the top a little faster than the mids even there (Shimmer's
    kDampingMaxRatio, Studio's Damp 0 column, Spring's fixed kHighDecayRatio)
    and that is voicing, not the knob - so the line is gone at 0, and Spring,
    which has no Damping knob, never shows one.

    It then runs on the tail's own clock rather than the wall clock, so it bites
    inside the wave at a short Decay as well as a long one; drawn in seconds it
    arrived after a short tail had already died, over empty well. It grows as
    the square root of that clock, so it bites early - most of the top has gone
    in the first part of a damped tail - and the square root on the strength
    keeps light Damping visible and heavy Damping from slamming shut. */
function dampLine(model, restModel) {
  const logSpan = Math.log(1 + T_MAX / TIME_KNEE);
  const midT60 = model.t60(1000);
  const excess = (m) => 60 / Math.max(1e-3, m.t60(8000)) - 60 / m.t60(1000);
  const strength = Math.sqrt(Math.max(0, ((excess(model) - excess(restModel)) * midT60) / 60));
  return Array.from({ length: SAMPLES + 1 }, (_, i) => {
    const t = TIME_KNEE * (Math.exp((i / SAMPLES) * logSpan) - 1);
    return clamp(DAMP_SPEED * strength * Math.sqrt(Math.max(0, t - model.start) / midT60), 0, 1);
  });
}

// How fast the Damping line closes, in reaches per tail length per unit of
// strength (see dampLine).
const DAMP_SPEED = 2.5;

// How far each cut can reach in from its edge, as a fraction of the
// well's height - short of halfway, so the two never cross.
const CUT_REACH = 0.45;

export default function ReverbScope({ engine = "studio", ariaLabel = "Reverb decay", ...params }) {
  const build = MODELS[engine] ?? studioModel;
  const model = build(params);
  const { env, carrier } = tailWave(model);

  const mid = VIEW_H / 2;
  const amp = mid - PAD_Y;
  const xAt = (i) => PAD_X + (i / SAMPLES) * (VIEW_W - 2 * PAD_X);

  // The cuts as the room they leave: Hi Cut a lid coming down from the top,
  // Low Cut a floor coming up from the bottom, each as far along its own (log)
  // range as its knob. Damping closes the lid further as the tail goes on - a
  // Hi Cut that tightens over time, faster on a short Decay (see dampLine).
  // At rest the lid and floor sit on the well's edges and take nothing.
  const reach = CUT_REACH * VIEW_H;
  const hiIn = clamp(Math.log(20000 / model.hi) / Math.log(20000 / 1000), 0, 1) * reach;
  const loIn = clamp(Math.log(model.lo / 20) / Math.log(800 / 20), 0, 1) * reach;
  const damp = dampLine(model, build({ ...params, damping01: 0 }));

  // Neither is drawn. They limit the wave's *envelope* on their own side - the
  // upper lobes may swing only as far as the lid, the lower only as far as the
  // floor - so the trace stays one unbroken sine whose peaks land where the
  // boundary is, rather than being sliced off by it.
  const room = (edgeIn) => clamp((mid - Math.max(PAD_Y, edgeIn)) / amp, 0, 1);
  const floorRoom = room(loIn);
  const values = carrier.map((c, i) => {
    const lidRoom = room(Math.min(reach, hiIn + damp[i] * reach));
    return c * Math.min(env[i], c >= 0 ? lidRoom : floorRoom);
  });

  const points = values.map((v, i) => `${xAt(i).toFixed(2)} ${(mid - v * amp).toFixed(2)}`);
  const trace = `M ${points.join(" L ")}`;
  const fill = `${trace} L ${xAt(SAMPLES).toFixed(2)} ${mid} L ${xAt(0).toFixed(2)} ${mid} Z`;

  return (
    <div className="pui-reset pui-modscope" role="img" aria-label={ariaLabel}>
      <svg className="pui-modscope__svg" viewBox={`0 0 ${VIEW_W} ${VIEW_H}`} preserveAspectRatio="none">
        <line className="pui-modscope__grid" x1="0" x2={VIEW_W} y1={mid} y2={mid} />
        <path className="pui-modscope__band" d={fill} />
        <path className="pui-modscope__trace pui-modscope__trace--thin" d={trace} />
      </svg>
    </div>
  );
}
