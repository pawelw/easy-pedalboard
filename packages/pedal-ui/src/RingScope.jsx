import { useMemo } from "react";
import "./RingScope.css";

/**
 * The Ring Mod engine's display: a slow "program" sine multiplied by the
 * carrier, drawn as the classic DSB-SC lattice. Chrome and shape only, the
 * sibling of `FilterScope` / `CrushScope`; it takes no live feed, it is a
 * picture of where the knobs sit.
 *
 *   - `freq01`   raw 0..1 Freq knob - sets how dense the carrier lattice is.
 *   - `tweak01`  raw 0..1 Tweak knob - in Earworm it wobbles the carrier
 *                spacing; in Green Lantern it leans the trace toward the
 *                rectified octave-up.
 *   - `mode`     0 = Earworm, 1 = Green Lantern.
 */

const VIEW_WIDTH = 320;
const PAD = 6;
const PROGRAM_CYCLES = 1.5;

// Carrier cycles drawn across the view at the bottom and top of the Freq knob,
// interpolated in log so the first turn already reads as a pitch move.
const CARRIER_MIN = 5;
const CARRIER_MAX = 46;

const SAMPLES = 320;
const TWO_PI = Math.PI * 2;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

export default function RingScope({
  freq01 = 0,
  tweak01 = 0,
  mode = 0,
  height = 64,
  baseColor = "var(--pui-scope-base)",
  fillColor = "var(--pui-scope-fill)",
}) {
  const plot = { x: PAD, top: PAD, bottom: height - PAD, w: VIEW_WIDTH - PAD * 2 };
  const midY = (plot.top + plot.bottom) / 2;
  const ampY = (plot.bottom - plot.top) / 2;

  const f = clamp(freq01, 0, 1);
  const t = clamp(tweak01, 0, 1);
  const greenLantern = mode === 1;

  const carrierCycles = Math.exp(lerp(Math.log(CARRIER_MIN), Math.log(CARRIER_MAX), f));

  const { line, area } = useMemo(() => {
    const yFor = (v) => midY - clamp(v, -1, 1) * ampY;

    let d = "";
    let top = "";
    for (let i = 0; i <= SAMPLES; i++) {
      const u = i / SAMPLES;
      const x = plot.x + u * plot.w;

      // Earworm bends the carrier spacing along x with Tweak; Green Lantern
      // keeps it even and spends Tweak on rectifying the program instead.
      const wob = greenLantern ? 0 : t * 0.35 * Math.sin(u * TWO_PI * 2);
      const carrier = Math.sin(u * TWO_PI * carrierCycles * (1 + wob));

      let program = Math.sin(u * TWO_PI * PROGRAM_CYCLES);
      if (greenLantern) program = lerp(program, Math.abs(program) * 2 - 1, t);

      const v = program * carrier;
      d += `${i === 0 ? "M" : "L"} ${x.toFixed(1)} ${yFor(v).toFixed(1)} `;
      top += `${i === 0 ? "M" : "L"} ${x.toFixed(1)} ${yFor(Math.abs(program)).toFixed(1)} `;
    }

    const filled = `${top} L ${(plot.x + plot.w).toFixed(1)} ${midY.toFixed(1)} L ${plot.x.toFixed(1)} ${midY.toFixed(1)} Z`;
    return { line: d.trim(), area: filled.trim() };
  }, [carrierCycles, t, greenLantern, midY, ampY, plot.x, plot.w]);

  return (
    <div className="pui-scope pui-ring-scope" style={{ height }}>
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
