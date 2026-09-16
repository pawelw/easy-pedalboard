import { useEffect, useRef } from "react";
import { useJuceSliderValue, useParamId, useFormattedText } from "@synthpeak/pedal-ui/juce";

/**
 * The five recessed displays across the plate (COMPONENTS.md's "Displays"
 * table) plus the full-width scope strip. Random/Reverb are decorative and
 * static, matching the design_handoff mock exactly; Grain and Pitch Weights
 * both read live parameter values.
 */

// The Size range, mirroring GrainerConfig.h's kMinGrainMs/kMaxGrainMs. Only
// GrainEnvelope uses these, to turn the printed duration back into a width.
const SIZE_MIN_MS = 20;
const SIZE_MAX_MS = 1000;

function clampMs(ms) {
  return Math.min(SIZE_MAX_MS, Math.max(SIZE_MIN_MS, ms));
}

/** Milliseconds out of a readout this codebase printed - "240 ms" or "1.20 s",
    the two shapes PeakGrainProcessor::sizeReadout() emits. Returns null for
    anything else, including the empty string a page with no JUCE backend
    behind it gets back, so the caller can fall back rather than draw nonsense. */
function parseDurationMs(text) {
  const match = /^\s*([\d.]+)\s*(ms|s)\s*$/.exec(text || "");
  if (match == null) return null;

  const value = Number(match[1]);
  if (! Number.isFinite(value)) return null;

  return match[2] === "s" ? value * 1000 : value;
}

/** Mirrors ee::dsp::TempoDivision's own table (shared/include/ee/dsp/
    TempoDivision.h) - only the `beats` column, since only how many of a
    division fit in a bar is drawn here, never its label. */
const GRAIN_SYNC_BEATS = [0.125, 1 / 6, 0.25, 0.375, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 4 / 3, 2, 3, 4, 6];

/** Which of GrainSyncMap's tempo divisions Destiny would land on if Sync were
    on. Density is a *rate* map there (knob up = faster), so the fastest,
    shortest division sits at the top of the knob's travel - the reverse of a
    duration knob - see GrainSyncMap::divisionIndex's own note. */
function grainDivisionBeats(density01) {
  const last = GRAIN_SYNC_BEATS.length - 1;
  const pick = 1 - density01;
  const index = Math.min(last, Math.max(0, Math.round(pick * last)));
  return GRAIN_SYNC_BEATS[index];
}

/** Grain's envelope curve: attack/decay lean driven by Size, Shape and
    Destiny (density) - the same three fields Grainer::updateDerived morphs
    off Shape (attack width, decay steepness), stretched by Size and overlaid
    with extra staggered copies for however many of Destiny's tempo division
    fit in a bar (a quarter note is "1/4", four of which fit in a bar of
    four beats, so it draws four - not a linear guess at Destiny's value).

    Window fades those trailing copies rather than adding to their count: it
    is how long (see GrainerConfig.h's WINDOW section) the cloud keeps
    drawing grains from the struck attack before falling back to whatever
    Time and Scatter are currently offering, so a short Window reads as one
    clean pass (the trailing copies scale to nothing) and a long one as the
    cloud still re-singing the same attack several shapes later - the same
    geometric falloff drawn here, one Window power per step from the lead
    grain. The lead is drawn first (left), the way a delay's own repeat
    diagram reads: the struck note first, its repeats decaying away to the
    right of it. */
export function GrainEnvelope({ accent }) {
  const [shape] = useJuceSliderValue("shape");
  const [size] = useJuceSliderValue("size");
  const [density] = useJuceSliderValue("density");
  const [window01] = useJuceSliderValue("window");

  // Width comes from the readout, not from the knob position. The readout is
  // clamped to the grain length the engine will actually apply (see
  // PeakGrainProcessor::sizeReadout) - synced, the knob can select a division
  // far longer than kMaxGrainMs, and every position past that cap produces the
  // same grain. Drawing off the raw knob made the envelope go on widening
  // while the value sat still, which is a picture of a sound nothing is
  // making. Reading it back off the printed text is what guarantees the two
  // can never disagree.
  const sizeId = useParamId("size");
  const sizeMs = parseDurationMs(useFormattedText(sizeId, size));
  // Logarithmic, because grain length reads to the ear as a ratio, not a
  // difference - 20 to 40 ms is the same step as 1 to 2 s.
  const sizeSpan =
    sizeMs == null
      ? size // no backend to ask (the gallery) - fall back to the knob
      : Math.log(clampMs(sizeMs) / SIZE_MIN_MS) / Math.log(SIZE_MAX_MS / SIZE_MIN_MS);

  const grainCount = Math.min(8, Math.max(1, Math.round(4 / grainDivisionBeats(density))));
  const width = 70 + sizeSpan * 110; // longer Size = wider grain window
  const spacing = grainCount > 1 ? (234 - width) / (grainCount - 1) : 0;

  return (
    <svg width="100%" height="48" viewBox="0 0 240 48" preserveAspectRatio="none" fill="none">
      {Array.from({ length: grainCount }, (_, i) => {
        const x0 = 6 + i * Math.max(spacing, 0);
        const stepsOut = i; // 0 at the lead (left), rising going right
        const isLead = stepsOut === 0;
        const decay = Math.pow(window01, stepsOut);
        return (
          <path
            key={i}
            d={grainEnvelopePath(x0, width, shape)}
            stroke={accent}
            strokeWidth="1.6"
            strokeLinecap="round"
            vectorEffect="non-scaling-stroke"
            fill={isLead ? accent : "none"}
            fillOpacity={isLead ? 0.1 : 0}
            opacity={isLead ? 1 : 0.35 * decay}
          />
        );
      })}
    </svg>
  );
}

// One grain window's outline, closed down to the baseline for the fill: a
// short attack up to the peak, then a decay leaning from gentle (Shape 0,
// Grainer's "soft" end) to a fast pluck that flattens early (Shape 1, its
// "hard" end) - sampled into a polyline since an SVG path takes no exponent.
function grainEnvelopePath(x0, width, shape01) {
  const top = 6;
  const base = 42;
  const attackFrac = 0.5 - shape01 * 0.42; // soft: peak near mid-window, hard: near the start
  const decayShape = 0.6 + shape01 * 3.4; // soft: gentle slope, hard: steep then flat
  const peakX = x0 + width * Math.max(0.04, attackFrac);

  const points = [`${x0.toFixed(1)} ${base}`, `${peakX.toFixed(1)} ${top}`];
  const steps = 16;
  for (let i = 1; i <= steps; i++) {
    const t = i / steps;
    const x = peakX + (x0 + width - peakX) * t;
    const y = top + (base - top) * (1 - Math.exp(-decayShape * t));
    points.push(`${x.toFixed(1)} ${y.toFixed(1)}`);
  }
  points.push(`${(x0 + width).toFixed(1)} ${base + 4}`, `${x0.toFixed(1)} ${base + 4}`);
  return `M ${points.join(" L ")} Z`;
}

/** Three bars, heights from the Low/Unison/High pitch-weight parameters -
    the one display in this row that COMPONENTS.md documents as reactive. */
export function PitchWeights({ accent }) {
  const [low] = useJuceSliderValue("plow");
  const [unison] = useJuceSliderValue("puni");
  const [high] = useJuceSliderValue("phigh");

  return (
    <div className="pg-weights">
      <div className="pg-weights__baseline" />
      {[low, unison, high].map((v, i) => (
        <div key={i} className="pg-weights__col">
          <div
            className="pg-weights__bar"
            // .pg-weights only has 31px of vertical room above its own
            // bottom padding (48px tall minus 8px top/9px bottom padding) -
            // 40 overshot that and poked a fully-dialled bar past the
            // panel's own top edge.
            style={{ height: `${Math.max(2, v * 30)}px`, background: accent }}
          />
        </div>
      ))}
    </div>
  );
}

// Fixed scatter - 11 grains, left percent/size/opacity straight off the mock
// (Peak Grain - 1c.dc.html). `top` is numeric (percent, 50 = the centre
// line) rather than a string here, since RandomField scales each grain's
// distance from centre by the Stereo knob at render time.
const RANDOM_DOTS = [
  { left: "8%", top: 62, size: 5, opacity: 0.35 },
  { left: "16%", top: 34, size: 3, opacity: 0.5 },
  { left: "24%", top: 70, size: 3, opacity: 0.65 },
  { left: "33%", top: 46, size: 5, opacity: 0.8 },
  { left: "41%", top: 26, size: 3, opacity: 0.35 },
  { left: "49%", top: 58, size: 3, opacity: 0.5 },
  { left: "57%", top: 38, size: 5, opacity: 0.65 },
  { left: "64%", top: 74, size: 3, opacity: 0.8 },
  { left: "72%", top: 50, size: 3, opacity: 0.35 },
  { left: "80%", top: 30, size: 5, opacity: 0.5 },
  { left: "88%", top: 64, size: 3, opacity: 0.65 },
];

/** L/R stereo field: a centre line and a scatter of grains either side, each
    now driven by this section's own three knobs. Stereo scales every grain's
    distance from the centre line (0 collapses the whole field onto it, 1 is
    the full spread above); Scatter sets how far and how fast the grains jitter
    - it is exactly the engine's own per-grain timing/position jitter, so 0
    holds them still; Reverse mirrors whatever fraction of the field (by
    left-to-right order) that knob calls for, so that many grains both drift
    and taper the other way, standing in for that fraction playing backwards.
    Each grain is drawn with the same steep-attack/long-decay lean as
    GrainEnvelope above - a fat rounded head tapering to a thin tail - rather
    than a plain dot, so the two displays read as the same shape.

    The jitter itself is a `requestAnimationFrame` loop rather than a CSS
    keyframe animation driven by custom properties: a CSS animation re-reads
    its `to` value (and duration) live, but re-evaluates it at the *same*
    elapsed-time fraction, so every tick of the Scatter knob snapped the
    drift to a new position - visible as the grains jumping rather than
    drifting. Accumulating each grain's own phase every frame and reading the
    latest knob values off a ref sidesteps that: a knob change can only ever
    bend the curve going forward, never relocate a point already drawn. */
export function RandomField({ accent }) {
  const [stereo] = useJuceSliderValue("stereo");
  const [reverse] = useJuceSliderValue("reverse");
  const [scatter] = useJuceSliderValue("scatter");

  const n = RANDOM_DOTS.length;
  const liveRef = useRef({ reverse, scatter });
  liveRef.current = { reverse, scatter };
  const dotRefs = useRef([]);

  useEffect(() => {
    const phase = RANDOM_DOTS.map((_, i) => (i / n) * Math.PI * 2);
    let last = performance.now();
    let raf = requestAnimationFrame(tick);

    function tick(now) {
      const dt = (now - last) / 1000;
      last = now;
      const { reverse, scatter } = liveRef.current;
      // Grainer::setScatter docs 0 as "metronomic, identical grains" - no
      // jitter at all, not just a small one, so this has no floor.
      const amplitude = scatter * 22; // px of drift travel
      const speed = 1.4 + scatter * 4; // radians/sec, faster jitter as Scatter rises

      for (let i = 0; i < n; i++) {
        phase[i] += speed * dt;
        const reversed = n > 1 && i / (n - 1) < reverse;
        const dx = Math.sin(phase[i]) * amplitude * (reversed ? -1 : 1);
        const el = dotRefs.current[i];
        if (el) el.style.transform = `translate(-50%, -50%) translateX(${dx.toFixed(2)}px)`;
      }

      raf = requestAnimationFrame(tick);
    }

    return () => cancelAnimationFrame(raf);
  }, [n]);

  return (
    <div className="pg-field">
      <div className="pg-field__hline" />
      <span className="pg-field__side pg-field__side--l">L</span>
      <span className="pg-field__side pg-field__side--r">R</span>
      {RANDOM_DOTS.map((d, i) => {
        const reversed = n > 1 && i / (n - 1) < reverse;
        const top = 50 + (d.top - 50) * stereo;
        const w = d.size * 2.6;
        const h = d.size * 1.5;
        return (
          <span
            key={i}
            ref={(el) => (dotRefs.current[i] = el)}
            className="pg-field__dot"
            style={{ left: d.left, top: `${top}%`, width: w, height: h, opacity: d.opacity }}
          >
            <svg viewBox="0 0 14 8" width="100%" height="100%" style={{ transform: reversed ? "scaleX(-1)" : undefined }}>
              <path
                d="M1 4 C1 1.9 3.3 1 5.6 1 C9.6 1 13 2.5 13 4 C13 5.5 9.6 7 5.6 7 C3.3 7 1 6.1 1 4 Z"
                fill={accent}
              />
            </svg>
          </span>
        );
      })}
    </div>
  );
}

// The cloud lowpass's travel, mirroring GrainerConfig.h's CLOUD FILTER block.
// Only the lowpass is drawn: the highpass underneath is fixed and hidden by
// design, so putting it on the scope would advertise a control that is not
// there.
const CLOUD_LP_HZ = 13000;
const CLOUD_LP_MIN_HZ = 320;

const CURVE_F_MIN = 20;
const CURVE_F_MAX = 20000;
const CURVE_DB_FLOOR = -30;
const CURVE_GRID_HZ = [100, 1000, 10000];
const CURVE_W = 120;
const CURVE_H = 44;
const CURVE_PAD = 4;
const CURVE_LOG_MIN = Math.log(CURVE_F_MIN);
const CURVE_LOG_SPAN = Math.log(CURVE_F_MAX) - CURVE_LOG_MIN;

/** Where the cutoff sits for a given knob position - the same geometric sweep
    Grainer::updateCloudFilter() does with its own std::pow, so the drawn
    corner tracks the audible one across the whole travel rather than only at
    the ends. `openness` is the knob in 0..1, 1 being wide open. */
function cloudCutoff(openness) {
  return CLOUD_LP_HZ * Math.pow(CLOUD_LP_MIN_HZ / CLOUD_LP_HZ, 1 - openness);
}

/** The lowpass's magnitude in dB: one pole at 6 dB/oct and nothing else. No Q
    term, because the engine has none - a drawn-on bump would be inventing a
    control that does not exist. */
function cloudDb(fHz, lpHz) {
  return 20 * Math.log10(Math.max(1e-6, lpHz / Math.sqrt(fHz * fHz + lpHz * lpHz)));
}

function curveX(hz) {
  const clamped = Math.min(CURVE_F_MAX, Math.max(CURVE_F_MIN, hz));
  return CURVE_PAD + ((Math.log(clamped) - CURVE_LOG_MIN) / CURVE_LOG_SPAN) * (CURVE_W - CURVE_PAD * 2);
}

function curveY(db) {
  const clamped = Math.min(0, Math.max(CURVE_DB_FLOOR, db));
  return CURVE_PAD + (clamped / CURVE_DB_FLOOR) * (CURVE_H - CURVE_PAD * 2);
}

/** The Mixer's Filter response, in place of a printed value. Reads `filter`
    live and redraws, so the line moves with the knob: wide open it is flat to
    13 kHz, and winding down walks the corner to 320 Hz.

    Colours are literals rather than `var(--pui-scope-*)` for the reason at the
    top of GrainFace.jsx - a custom property in an SVG presentation attribute
    does not resolve reliably in the plugin's WKWebView - and because onyx only
    defines --pui-scope-grid, so the rest would fall back to the light theme's
    dark red. */
export function FilterCurve({ accent }) {
  const [filter] = useJuceSliderValue("filter");

  // The parameter is 0..100 % and the hook hands back 0..1, so it is already
  // the openness the sweep wants.
  const lpHz = cloudCutoff(filter);

  const steps = 72;
  let line = "";
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const hz = Math.exp(CURVE_LOG_MIN + t * CURVE_LOG_SPAN);
    const x = CURVE_PAD + t * (CURVE_W - CURVE_PAD * 2);
    line += `${i === 0 ? "M" : "L"}${x.toFixed(1)} ${curveY(cloudDb(hz, lpHz)).toFixed(1)} `;
  }

  const floorY = CURVE_H - CURVE_PAD;
  const fill = `${line}L ${CURVE_W - CURVE_PAD} ${floorY} L ${CURVE_PAD} ${floorY} Z`;

  return (
    <div className="pg-filter-curve">
      <svg width="100%" height="100%" viewBox={`0 0 ${CURVE_W} ${CURVE_H}`} preserveAspectRatio="none" fill="none">
        <g stroke="#233034" strokeWidth="1" vectorEffect="non-scaling-stroke">
          {CURVE_GRID_HZ.map((hz) => (
            <line key={hz} x1={curveX(hz)} y1={CURVE_PAD} x2={curveX(hz)} y2={floorY} />
          ))}
          <line x1={CURVE_PAD} y1={curveY(-12)} x2={CURVE_W - CURVE_PAD} y2={curveY(-12)} />
        </g>
        <path d={fill} fill={accent} fillOpacity="0.12" stroke="none" />
        <path
          d={line}
          fill="none"
          stroke={accent}
          strokeWidth="1.6"
          strokeLinecap="round"
          strokeLinejoin="round"
          vectorEffect="non-scaling-stroke"
        />
      </svg>
    </div>
  );
}

// Falling bar heights, 28 -> 3px - Reverb's tail (COMPONENTS.md: "eight 5px
// bars falling 28 -> 3px").
const TAIL_HEIGHTS = [28, 22, 17, 13, 9, 6, 4, 3];

export function ReverbTail({ accent }) {
  return (
    <div className="pg-tail">
      {TAIL_HEIGHTS.map((h, i) => (
        <div key={i} className="pg-tail__bar" style={{ height: `${h}px`, background: accent }} />
      ))}
    </div>
  );
}
