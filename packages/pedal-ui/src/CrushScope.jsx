import { useMemo } from "react";
import "./CrushScope.css";

/**
 * The Bit Crush engine's display: a reference sine drawn as the crusher would
 * leave it - held in time by the Rate knob (fewer, wider steps as it turns up),
 * quantised in amplitude by the Bits knob (fewer levels), and with the step
 * widths knocked out of regularity by Jitter. Chrome and shape only, the
 * sibling of `FilterScope`; it takes no live feed, it is a picture of where the
 * three knobs sit.
 *
 * `bits01` / `rate01` / `jitter01` are the raw 0..1 knob positions.
 */

const VIEW_WIDTH = 320;
const PAD = 6;
const CYCLES = 2;

// Steps drawn across the whole view at the clean and fully-crushed ends of the
// Rate knob. Interpolated in log so the first turn off "clean" already bites.
const STEPS_CLEAN = 150;
const STEPS_CRUSHED = 5;

// Amplitude levels drawn at the clean and fully-crushed ends of the Bits knob.
// The clean end is capped well below true 24-bit - past a few dozen the banding
// is invisible anyway and the trace should just read as smooth.
const LEVELS_CLEAN = 56;
const LEVELS_CRUSHED = 2;

const JITTER_DEPTH = 0.3;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

// Deterministic [0, 1) per step index - so the jittered widths hold still
// between renders instead of shimmering.
function hashStep(i) {
  const x = Math.sin(i * 127.1 + 11.7) * 43758.5453;
  return x - Math.floor(x);
}

export default function CrushScope({
  bits01 = 0,
  rate01 = 0,
  jitter01 = 0,
  height = 64,
  baseColor = "var(--pui-scope-base)",
  fillColor = "var(--pui-scope-fill)",
}) {
  const plot = { x: PAD, top: PAD, bottom: height - PAD, w: VIEW_WIDTH - PAD * 2 };
  const midY = (plot.top + plot.bottom) / 2;
  const ampY = (plot.bottom - plot.top) / 2;

  const b = clamp(bits01, 0, 1);
  const r = clamp(rate01, 0, 1);
  const j = clamp(jitter01, 0, 1);

  const steps = Math.max(
    2,
    Math.round(Math.exp(lerp(Math.log(STEPS_CLEAN), Math.log(STEPS_CRUSHED), r))),
  );
  const levels = Math.round(Math.exp(lerp(Math.log(LEVELS_CLEAN), Math.log(LEVELS_CRUSHED), b)));
  const half = levels / 2;

  const { line, area } = useMemo(() => {
    // Step widths, jittered, normalised to the plot width.
    const widths = [];
    let total = 0;
    for (let i = 0; i < steps; i++) {
      const wob = 1 + j * JITTER_DEPTH * (hashStep(i) * 2 - 1);
      const w = Math.max(0.12, wob);
      widths.push(w);
      total += w;
    }

    const quant = (v) => Math.round(v * half) / half;
    const yFor = (v) => midY - clamp(quant(v), -1, 1) * ampY;

    // Value at each step boundary (0..steps): sine sampled there, then held.
    let acc = 0;
    const boundaryPhase = [0];
    for (let i = 0; i < steps; i++) {
      acc += widths[i];
      boundaryPhase.push((acc / total) * CYCLES * 2 * Math.PI);
    }
    const v = boundaryPhase.map((ph) => Math.sin(ph));

    let x = plot.x;
    let d = `M ${x.toFixed(1)} ${yFor(v[0]).toFixed(1)}`;
    for (let i = 0; i < steps; i++) {
      x += (widths[i] / total) * plot.w;
      d += ` L ${x.toFixed(1)} ${yFor(v[i]).toFixed(1)}`; // hold across the step
      d += ` L ${x.toFixed(1)} ${yFor(v[i + 1]).toFixed(1)}`; // jump to the next
    }

    const filled = `${d} L ${(plot.x + plot.w).toFixed(1)} ${plot.bottom} L ${plot.x.toFixed(1)} ${plot.bottom} Z`;
    return { line: d, area: filled };
  }, [steps, levels, half, j, midY, ampY, plot.x, plot.w, plot.bottom, plot.top]);

  return (
    <div className="pui-scope pui-crush-scope" style={{ height }}>
      <svg className="pui-scope__svg" viewBox={`0 0 ${VIEW_WIDTH} ${height}`} preserveAspectRatio="none">
        <line
          x1={plot.x}
          y1={midY}
          x2={plot.x + plot.w}
          y2={midY}
          stroke="var(--pui-scope-grid)"
          strokeWidth="1"
        />
        <path d={area} fill={fillColor} stroke="none" />
        <path d={line} fill="none" stroke={baseColor} strokeWidth="2" strokeLinejoin="round" />
      </svg>
    </div>
  );
}
