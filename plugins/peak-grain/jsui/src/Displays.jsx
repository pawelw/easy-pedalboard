import { useEffect, useRef } from "react";
import { useJuceSliderValue } from "@synthpeak/pedal-ui/juce";

/**
 * The five recessed displays across the plate (COMPONENTS.md's "Displays"
 * table) plus the full-width scope strip. Random/Reverb are decorative and
 * static, matching the design_handoff mock exactly; Grain and Pitch Weights
 * both read live parameter values.
 */

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

  const grainCount = Math.min(8, Math.max(1, Math.round(4 / grainDivisionBeats(density))));
  const width = 70 + size * 110; // longer Size = wider grain window
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
