import { useMemo } from "react";
import "./RustScope.css";

/**
 * The Rust engine's display: a reference sine drawn as the corrosion leaves it -
 * amplitude broken into quantise steps by Grind, then squared off (Contact) or
 * wavering and darkening (Oxide). It builds across the trace left to right, the
 * way a note rusts as it rings. Chrome and shape only, the sibling of
 * `FilterScope` / `CrushScope` / `RingScope`; it takes no live feed, it is a
 * picture of where the knobs sit.
 *
 *   - `grind01`  raw 0..1 Grind knob - how coarse the crumble is.
 *   - `mode`     0 = Oxide (warble + darken), 1 = Contact (squared-off peaks + coarser crumble).
 */

const VIEW_WIDTH = 320;
const PAD = 6;
const CYCLES = 3;
const SAMPLES = 320;
const TWO_PI = Math.PI * 2;

const LEVELS_CLEAN = 48;
const LEVELS_CRUSHED = 3;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

export default function RustScope({
  grind01 = 0,
  mode = 0,
  height = 64,
  baseColor = "var(--pui-scope-base)",
  fillColor = "var(--pui-scope-fill)",
}) {
  const plot = { x: PAD, top: PAD, bottom: height - PAD, w: VIEW_WIDTH - PAD * 2 };
  const midY = (plot.top + plot.bottom) / 2;
  const ampY = (plot.bottom - plot.top) / 2;

  const grind = clamp(grind01, 0, 1);
  const contact = mode === 1;

  const levels = Math.round(
    Math.exp(lerp(Math.log(LEVELS_CLEAN), Math.log(LEVELS_CRUSHED), grind)),
  );
  const half = Math.max(1, levels / 2);

  const line = useMemo(() => {
    const yFor = (v) => midY - clamp(v, -1, 1) * ampY;

    let d = "";
    for (let i = 0; i <= SAMPLES; i++) {
      const u = i / SAMPLES;
      // Corrosion builds across the trace - clean at the left edge, full by
      // about a third of the way in (the note rusting as it rings).
      const front = clamp(u / 0.33, 0, 1);
      const bite = front * (0.35 + 0.65 * grind);

      let v = Math.sin(u * TWO_PI * CYCLES);

      if (bite > 0.001) {
        // amplitude quantise
        const q = 1 + Math.round((half - 1) * bite);
        v = Math.round(v * q) / q;

        if (contact) {
          // Contact: peaks squared off by a hard clip that tightens as the
          // corrosion builds, over a coarser step ladder. No noise.
          const clip = 1 - 0.65 * bite;
          v = Math.max(-clip, Math.min(clip, v * (1 + 0.4 * bite)));
        } else {
          // Oxide: a slow waver on the baseline and a shrinking, darkening
          // amplitude. No noise.
          v += Math.sin(u * TWO_PI * 1.5 + 0.6) * 0.12 * bite;
          v *= 1 - 0.35 * bite;
        }
      }

      d += `${i === 0 ? "M" : "L"} ${(plot.x + u * plot.w).toFixed(1)} ${yFor(v).toFixed(1)} `;
    }
    return d.trim();
  }, [grind, contact, half, midY, ampY, plot.x, plot.w]);

  return (
    <div className="pui-scope pui-rust-scope" style={{ height }}>
      <svg className="pui-scope__svg" viewBox={`0 0 ${VIEW_WIDTH} ${height}`} preserveAspectRatio="none">
        <line
          x1={plot.x}
          y1={midY}
          x2={plot.x + plot.w}
          y2={midY}
          stroke="var(--pui-scope-grid)"
          strokeWidth="1"
        />
        <path
          d={`${line} L ${(plot.x + plot.w).toFixed(1)} ${midY.toFixed(1)} L ${plot.x.toFixed(1)} ${midY.toFixed(1)} Z`}
          fill={fillColor}
          stroke="none"
        />
        <path d={line} fill="none" stroke={baseColor} strokeWidth="2" strokeLinejoin="round" />
      </svg>
    </div>
  );
}
