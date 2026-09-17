// The Mod tab's breakpoint LFO: one evaluation path for every shape, so a
// preset is never more than a starting set of points the user can then drag.
// Unrelated to packages/pedal-ui/src/lfo.js's lfoValue() - that is the
// existing 5-anchor continuous morph the Tremolo engine runs on; this is a
// genuinely different model (a sparse, editable point list with per-segment
// curve/hold) needed for a graph you can click on and reshape.
//
// A breakpoint is { x, y, curve, hold }: `x` in [0,1) (phase position, points
// kept sorted ascending), `y` in [-1,1], `curve` in [-1,1] shaping the
// OUTGOING segment from this point (0 = linear, negative = fast-start/
// ease-out, positive = slow-start/ease-in), and `hold` (the segment stays
// flat at this point's `y` until the next point's `x`, then jumps - what
// gives Square and Random their staircase look on the same evaluator smooth
// shapes use).

export const kMaxBreakpoints = 64;

function clamp01(v) {
  return Math.min(1, Math.max(0, v));
}

function shapeT(t, curve) {
  if (!curve) return t;
  const k = 1 + Math.abs(curve) * 4;
  return curve > 0 ? Math.pow(t, k) : 1 - Math.pow(1 - t, k);
}

function segmentValue(p0, p1, phase01) {
  if (p0.hold) return p0.y;
  const span = p1.x - p0.x;
  const t = span <= 0 ? 0 : clamp01((phase01 - p0.x) / span);
  const shaped = shapeT(t, p0.curve || 0);
  return p0.y + (p1.y - p0.y) * shaped;
}

/** The LFO's value at a cyclic phase (any real; only the fractional part is
    used) for a given breakpoint list. Points need not be pre-sorted. */
export function evalBreakpoints(points, phase01) {
  if (!points || points.length === 0) return 0;
  const sorted = [...points].sort((a, b) => a.x - b.x);
  if (sorted.length === 1) return sorted[0].y;

  const p = ((phase01 % 1) + 1) % 1;

  for (let i = 0; i < sorted.length - 1; i++) {
    const p0 = sorted[i];
    const p1 = sorted[i + 1];
    if (p >= p0.x && p <= p1.x) return segmentValue(p0, p1, p);
  }

  // Wrap segment: last point back to the first, one cycle later.
  const last = sorted[sorted.length - 1];
  const first = sorted[0];
  const wrappedP = p < first.x ? p + 1 : p;
  return segmentValue(last, { ...first, x: first.x + 1 }, wrappedP);
}

function seededRandom(seed) {
  let s = seed >>> 0 || 1;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

export function sineBreakpoints() {
  return [
    { x: 0.0, y: 0, curve: -0.5, hold: false },
    { x: 0.25, y: 1, curve: 0.5, hold: false },
    { x: 0.5, y: 0, curve: -0.5, hold: false },
    { x: 0.75, y: -1, curve: 0.5, hold: false },
  ];
}

export function triangleBreakpoints() {
  return [
    { x: 0.0, y: 1, curve: 0, hold: false },
    { x: 0.5, y: -1, curve: 0, hold: false },
  ];
}

export function sawBreakpoints() {
  return [
    { x: 0.0, y: 1, curve: 0, hold: false },
    { x: 0.94, y: -1, curve: 0, hold: false },
  ];
}

export function squareBreakpoints() {
  return [
    { x: 0.0, y: 1, curve: 0, hold: true },
    { x: 0.5, y: -1, curve: 0, hold: true },
  ];
}

/** Sample-and-hold - a fixed, seeded staircase (a new seed on every pick, but
    then a stable, fully draggable shape like any other preset, not something
    that reshuffles itself every cycle). */
export function randomBreakpoints(seed = Date.now() & 0xffff) {
  const rand = seededRandom(seed);
  const steps = 8;
  return Array.from({ length: steps }, (_, i) => ({
    x: i / steps,
    y: rand() * 2 - 1,
    curve: 0,
    hold: true,
  }));
}

/** The "plucked" exponential-decay shape, matching the vocabulary
    packages/pedal-ui/src/lfo.js already uses for the same idea. */
export function pluckBreakpoints() {
  return [
    { x: 0.0, y: 1, curve: -0.9, hold: false },
    { x: 0.9, y: -1, curve: 0, hold: false },
  ];
}

// The wire format between this editor and PeakGrainProcessor (see
// shared/include/ee/plugin/LfoBreakpointJson.h, which mirrors this exactly):
// `{ "version": 1, "points": [{x,y,curve,hold}, ...] }`. `id` (this file's
// own React-key concern) never crosses the bridge.
export function toBreakpointsJson(points) {
  return JSON.stringify({
    version: 1,
    points: points.map(({ x, y, curve, hold }) => ({ x, y, curve, hold })),
  });
}

/** Parses a breakpoints JSON string (or an already-parsed object, since the
    JUCE bridge sometimes hands events over already decoded) back into a
    plain points array, or null if it isn't one - a dev-server preview with
    no JUCE backend behind it answers every native call with nothing, and a
    fresh install's own default is written by the processor before anything
    here ever asks, so an empty/malformed answer should mean "leave whatever
    is on screen alone", not "clear it". */
export function parseBreakpointsJson(json) {
  try {
    const parsed = typeof json === "string" ? JSON.parse(json) : json;
    if (!parsed || !Array.isArray(parsed.points) || parsed.points.length === 0) return null;

    return parsed.points.map((p) => ({
      x: Number(p.x) || 0,
      y: Number(p.y) || 0,
      curve: Number(p.curve) || 0,
      hold: Boolean(p.hold),
    }));
  } catch {
    return null;
  }
}

export const LFO_PRESETS = [
  { id: "sine", label: "Sine", make: sineBreakpoints },
  { id: "triangle", label: "Triangle", make: triangleBreakpoints },
  { id: "saw", label: "Saw", make: sawBreakpoints },
  { id: "square", label: "Square", make: squareBreakpoints },
  { id: "random", label: "Random", make: () => randomBreakpoints() },
  { id: "pluck", label: "Pluck", make: pluckBreakpoints },
];
