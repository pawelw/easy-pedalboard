/**
 * The pre-EQ's response, ported from ee/dsp/Equaliser.h and EqualiserConfig.h -
 * the same RBJ biquads, the same 8th-order Butterworth split for the 48 dB
 * cuts - so the curve on the face is the filter the processor runs, not a
 * drawing of one. Evaluated at a fixed 48 kHz: the face does not know the
 * session's rate, and below ~15 kHz the difference is invisible.
 */

// The shapes, in ee::dsp::eq::FilterType's order - which is also the
// `eq.bN.type` choice list, so an index from the relay is an index here.
export const EQ_TYPES = [
  { id: "lowCut48", label: "Low Cut 48" },
  { id: "lowCut12", label: "Low Cut 12" },
  { id: "lowShelf", label: "Low Shelf" },
  { id: "bell", label: "Bell" },
  { id: "notch", label: "Notch" },
  { id: "highShelf", label: "High Shelf" },
  { id: "highCut12", label: "High Cut 12" },
  { id: "highCut48", label: "High Cut 48" },
];

export const EQ_MIN_HZ = 20;
export const EQ_MAX_HZ = 20000;
export const EQ_MAX_GAIN_DB = 15;
export const EQ_MIN_Q = 0.1;
export const EQ_MAX_Q = 18;
// A cut's resonance stops at this Q whatever the knob says (kMaxCutQ).
export const EQ_MAX_CUT_Q = 6;
export const EQ_BANDS = 8;

// The Simple face's three fixed bands (EqualiserConfig.h's kSimple*).
export const EQ_SIMPLE = [
  { type: 2, hz: 250, q: 0.71, label: "Low", caption: "250 Hz" },
  { type: 3, hz: 790, q: 0.5, label: "Mid", caption: "790 Hz" },
  { type: 5, hz: 2500, q: 0.71, label: "High", caption: "2.5 kHz" },
];

// Where the eight Advanced bands start (kAdvancedDefaults) - the picture drawn
// when there is no backend to ask.
export const EQ_DEFAULTS = [
  { type: 0, hz: 40, on: false },
  { type: 2, hz: 100, on: true },
  { type: 3, hz: 250, on: true },
  { type: 3, hz: 600, on: true },
  { type: 3, hz: 1500, on: true },
  { type: 3, hz: 3500, on: true },
  { type: 5, hz: 8000, on: true },
  { type: 7, hz: 16000, on: false },
];

export function typeHasGain(type) {
  return type === 2 || type === 3 || type === 5;
}

/** The four cuts. Their level at the corner is exactly Q (in dB) - true of
    one RBJ section, and of the 48s too, whose Butterworth section Qs multiply
    to 1/sqrt2 before the band's Q scales the last one - so the graph puts a
    cut's dot there, and dragging it up and down is Q. */
export function typeIsCut(type) {
  return type === 0 || type === 1 || type === 6 || type === 7;
}

const FS = 48000;
const BUTTERWORTH8_Q = [0.50979558, 0.60134489, 0.89997622, 2.56291545];
const BUTTERWORTH_Q = 0.70710678;

function norm(b0, b1, b2, a0, a1, a2) {
  return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
}

function lowPass(hz, q) {
  const w = (2 * Math.PI * hz) / FS, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  return norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

function highPass(hz, q) {
  const w = (2 * Math.PI * hz) / FS, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  return norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

function notch(hz, q) {
  const w = (2 * Math.PI * hz) / FS, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  return norm(1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
}

function peak(hz, q, db) {
  const A = Math.pow(10, db / 40);
  const w = (2 * Math.PI * hz) / FS, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  return norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}

function shelf(hz, q, db, high) {
  const A = Math.pow(10, db / 40);
  const w = (2 * Math.PI * hz) / FS, c = Math.cos(w), al = Math.sin(w) / (2 * q);
  const k = 2 * Math.sqrt(A) * al;
  if (high)
    return norm(
      A * (A + 1 + (A - 1) * c + k),
      -2 * A * (A - 1 + (A + 1) * c),
      A * (A + 1 + (A - 1) * c - k),
      A + 1 - (A - 1) * c + k,
      2 * (A - 1 - (A + 1) * c),
      A + 1 - (A - 1) * c - k,
    );
  return norm(
    A * (A + 1 - (A - 1) * c + k),
    2 * A * (A - 1 - (A + 1) * c),
    A * (A + 1 - (A - 1) * c - k),
    A + 1 + (A - 1) * c + k,
    -2 * (A - 1 + (A + 1) * c),
    A + 1 + (A - 1) * c - k,
  );
}

function clampHz(hz) {
  return Math.min(Math.max(hz, EQ_MIN_HZ * 0.5), FS * 0.45);
}

function clampQ(q) {
  return Math.min(Math.max(q, EQ_MIN_Q), EQ_MAX_Q);
}

/** The biquad sections one band runs, as [b0, b1, b2, a1, a2] each. */
export function bandSections({ type, hz, gain = 0, q = 0.71 }) {
  hz = clampHz(hz);
  q = clampQ(q);
  if (typeIsCut(type)) q = Math.min(q, EQ_MAX_CUT_Q);
  switch (type) {
    case 0:
    case 7:
      return BUTTERWORTH8_Q.map((sq, i) => {
        const qq = i === 3 ? sq * (q / BUTTERWORTH_Q) : sq;
        return type === 0 ? highPass(hz, qq) : lowPass(hz, qq);
      });
    case 1:
      return [highPass(hz, q)];
    case 6:
      return [lowPass(hz, q)];
    case 2:
      return [shelf(hz, q, gain, false)];
    case 5:
      return [shelf(hz, q, gain, true)];
    case 4:
      return [notch(hz, q)];
    default:
      return [peak(hz, q, gain)];
  }
}

/** |H| in dB of a set of sections at `hz`. */
export function sectionsDb(sections, hz) {
  const w = (2 * Math.PI * hz) / FS;
  const c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  let mag2 = 1;
  for (const [b0, b1, b2, a1, a2] of sections) {
    const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    mag2 *= (nr * nr + ni * ni) / Math.max(1e-30, dr * dr + di * di);
  }
  return 10 * Math.log10(Math.max(1e-30, mag2));
}

/** Frequencies the curve is drawn through: log-spaced across the face. */
export function curveFrequencies(points = 180) {
  const out = [];
  const lo = Math.log(EQ_MIN_HZ), hi = Math.log(EQ_MAX_HZ);
  for (let i = 0; i < points; ++i) out.push(Math.exp(lo + ((hi - lo) * i) / (points - 1)));
  return out;
}

export function hzToText(hz) {
  if (hz >= 1000) return `${(hz / 1000).toFixed(hz >= 10000 ? 1 : 2).replace(/\.?0+$/, "")} kHz`;
  return `${Math.round(hz)} Hz`;
}

export function dbToText(db) {
  return `${db > 0 ? "+" : ""}${db.toFixed(1)} dB`;
}
