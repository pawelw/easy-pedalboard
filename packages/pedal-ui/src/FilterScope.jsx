import { useMemo } from "react";
import "./FilterScope.css";

const STEPS = 80;
const VIEW_WIDTH = 320;
const GRID_COLUMNS = 8;
const GRID_ROWS = 4;

function clamp01(v) {
  return Math.max(0, Math.min(1, v));
}

/** Graph-paper backing so the curve reads against fixed divisions rather
    than floating in a blank panel. */
function Grid({ height }) {
  const lines = [];
  for (let i = 1; i < GRID_COLUMNS; i++) {
    const x = (i / GRID_COLUMNS) * VIEW_WIDTH;
    lines.push(<line key={"v" + i} x1={x} y1={0} x2={x} y2={height} />);
  }
  for (let i = 1; i < GRID_ROWS; i++) {
    const y = (i / GRID_ROWS) * height;
    lines.push(<line key={"h" + i} x1={0} y1={y} x2={VIEW_WIDTH} y2={y} />);
  }
  return (
    <g stroke="var(--pui-scope-grid)" strokeWidth="1">
      {lines}
    </g>
  );
}

/** Same three-tap blend Peak Wah's old face drew on the Filter Type knob's
    cap: low shelf at 0, a resonant bump at 1, high shelf at 2. `resonance01`
    narrows the bump the way turning Q actually narrows the filter. */
function tapHeight(t, tap, resonance01) {
  if (tap === 1) {
    const sigma = 0.22 - resonance01 * 0.14;
    return Math.exp(-0.5 * Math.pow((t - 0.5) / sigma, 2));
  }
  const u = tap === 0 ? t : 1 - t;
  return 0.8 / (1 + Math.pow(u / 0.5, 4)) + 0.45 * Math.exp(-0.5 * Math.pow((u - 0.5) / 0.09, 2));
}

function blendedHeight(t, type01, resonance01) {
  const m = clamp01(type01);
  const lower = m <= 0.5 ? 0 : 1;
  const blend = m <= 0.5 ? m * 2 : (m - 0.5) * 2;
  const a = tapHeight(t, lower, resonance01);
  const b = tapHeight(t, lower + 1, resonance01);
  return a + blend * (a === b ? 0 : b - a);
}

function curvePath(centerShift, type01, resonance01, height) {
  const top = height * 0.12;
  const bottom = height * 0.92;
  let d = "";
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const local = clamp01(t - centerShift + 0.5);
    const v = blendedHeight(local, type01, resonance01);
    const x = t * VIEW_WIDTH;
    const y = bottom - clamp01(v) * (bottom - top);
    d += (i === 0 ? "M" : "L") + x.toFixed(1) + " " + y.toFixed(1) + " ";
  }
  return d;
}

/**
 * The pedal's response scope: the resting filter curve, plus a pale wash on
 * either side showing how far Range lets it sweep. Reacts live to
 * freq/resonance/type/sweep - not a live audio-modulation animation (that
 * would need its own bridge from the C++ envelope, not wired up yet), but a
 * true reading of where the filter sits and how far it can move.
 */
export default function FilterScope({
  freq01 = 0.5,
  resonance01 = 0.5,
  type01 = 0,
  sweepDepth01 = 0,
  height = 72,
  baseColor = "var(--pui-scope-base)",
  sweepColor = "var(--pui-scope-sweep)",
}) {
  const center = 0.15 + clamp01(freq01) * 0.7;
  const sweep = clamp01(sweepDepth01) * 0.22;

  const basePath = useMemo(() => curvePath(center, type01, resonance01, height), [center, type01, resonance01, height]);
  const lowSweep = useMemo(
    () => curvePath(center - sweep, type01, resonance01, height),
    [center, sweep, type01, resonance01, height],
  );
  const highSweep = useMemo(
    () => curvePath(center + sweep, type01, resonance01, height),
    [center, sweep, type01, resonance01, height],
  );

  return (
    <div className="pui-scope" style={{ height }}>
      <svg className="pui-scope__svg" viewBox={`0 0 ${VIEW_WIDTH} ${height}`} preserveAspectRatio="none">
        <Grid height={height} />
        {sweep > 0.001 && (
          <>
            <path d={lowSweep} fill="none" stroke={sweepColor} strokeWidth="1.4" opacity="0.7" />
            <path d={highSweep} fill="none" stroke={sweepColor} strokeWidth="1.4" opacity="0.7" />
          </>
        )}
        <path d={basePath} fill="none" stroke={baseColor} strokeWidth="2" strokeLinecap="round" />
      </svg>
    </div>
  );
}
