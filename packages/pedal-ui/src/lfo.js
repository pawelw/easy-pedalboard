// A JS port of shared/include/ee/dsp/Lfo.h's lfoValue() - kept numerically in
// step with it by hand, since a knob glyph or scope drawn from a different
// formula than the audio path would be a picture of the wrong wave. See that
// header for what each of the five anchors means.

const FLYBACK = 0.06;

function easeUp(t) {
  const c = Math.max(0, Math.min(1, t));
  return -1 + (1 - Math.cos(c * Math.PI));
}

function expDecay(p) {
  const k = 5.0;
  const body = 1 - FLYBACK;
  const endValue = -1 + 2 * Math.exp(-k);
  if (p < body) return -1 + 2 * Math.exp((-k * p) / body);
  const t = (p - body) / FLYBACK;
  return endValue + (1 - endValue) * 0.5 * (1 - Math.cos(t * Math.PI));
}

function ramp(p) {
  const body = 1 - FLYBACK;
  if (p < body) return 1 - 2 * (p / body);
  return easeUp((p - body) / FLYBACK);
}

function triangle(p) {
  return p < 0.5 ? 1 - 4 * p : 4 * p - 3;
}

function roundedSquare(p, sharpness) {
  const c = Math.cos(p * 2 * Math.PI);
  return Math.tanh(sharpness * c) / Math.tanh(sharpness);
}

function anchor(index, p) {
  switch (index) {
    case 0:
      return expDecay(p);
    case 1:
      return ramp(p);
    case 2:
      return triangle(p);
    case 3:
      return roundedSquare(p, 2.1);
    default:
      return roundedSquare(p, 5.5);
  }
}

/** Shaped LFO value at a phase (any real; the integer part is discarded) and
    a shape in [0, 1]. Returns roughly [-1, 1], peaking at +1 at phase 0. */
export function lfoValue(phase, shape) {
  const p = phase - Math.floor(phase);
  const s = Math.max(0, Math.min(1, shape));

  const seg = s * 4;
  const i = Math.max(0, Math.min(3, Math.floor(seg)));
  const t = seg - i;

  const lo = anchor(i, p);
  const hi = anchor(i + 1, p);
  return lo + (hi - lo) * t;
}
