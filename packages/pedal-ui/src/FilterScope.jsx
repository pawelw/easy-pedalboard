import { useMemo } from "react";
import { SWEEP_RATIO_MAX } from "./autowah.js";
import "./FilterScope.css";

// Ported from the digital paint path of shared/src/ui/FilterScope.cpp
// (Peak Wah is a "digital"-themed pedal - see CLAUDE.md's control-styles
// section - so that path, not the analog one, is the real precedent). Same
// Gaussian-in-log-frequency bump, same axis range, same constants; modL/modR
// arrive live from the processor instead of being approximated.
const STEPS = 100;
const VIEW_WIDTH = 320;
const PAD = 6;
const F_MIN = 30;
const F_MAX = 18000;
const DB_FLOOR = -6;
const DB_CEIL = 26;
const FREQ_GRID_HZ = [100, 500, 2000, 10000];
const DB_GRID = [20, 10, 0];

const LOG_MIN = Math.log(F_MIN);
const LOG_SPAN = Math.log(F_MAX) - LOG_MIN;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function mapRange(v, inLo, inHi, outLo, outHi) {
  return outLo + ((v - inLo) / (inHi - inLo)) * (outHi - outLo);
}

function xForHz(hz, plot) {
  return plot.x + ((Math.log(clamp(hz, F_MIN, F_MAX)) - LOG_MIN) / LOG_SPAN) * plot.w;
}

function yForDb(db, plot) {
  return mapRange(clamp(db, DB_FLOOR, DB_CEIL), DB_FLOOR, DB_CEIL, plot.bottom, plot.top);
}

function bumpPath(fcHz, peakDb, bw, plot) {
  const logFc = Math.log(clamp(fcHz, F_MIN, F_MAX));
  let d = "";
  for (let i = 0; i <= STEPS; i++) {
    const t = i / STEPS;
    const lf = LOG_MIN + t * LOG_SPAN;
    const diff = (lf - logFc) / bw;
    const db = peakDb * Math.exp(-0.5 * diff * diff);
    const x = plot.x + t * plot.w;
    d += (i === 0 ? "M" : "L") + x.toFixed(1) + " " + yForDb(db, plot).toFixed(1) + " ";
  }
  return d;
}

function Grid({ plot }) {
  return (
    <g stroke="var(--pui-scope-grid)" strokeWidth="1">
      {FREQ_GRID_HZ.map((hz) => {
        const x = xForHz(hz, plot);
        return <line key={hz} x1={x} y1={plot.top} x2={x} y2={plot.bottom} />;
      })}
      {DB_GRID.map((db) => {
        const y = yForDb(db, plot);
        return <line key={db} x1={plot.x} y1={y} x2={plot.x + plot.w} y2={y} />;
      })}
    </g>
  );
}

/**
 * The pedal's response scope: the resting filter curve (peak height and
 * width from resonance, position from freq), a shaded band showing how far
 * Range lets it sweep, and - when a live modL/modR feed is wired up (see
 * PeakWahWebEditor's Timer) - the two channels' actual live position riding
 * inside that band. Without a live feed modL/modR default to 0 (centre).
 */
export default function FilterScope({
  baseFreqHz = 500,
  resonance01 = 0.5,
  sweepDepth01 = 0,
  modL = 0,
  modR = 0,
  height = 72,
  baseColor = "var(--pui-scope-base)",
  sweepColor = "var(--pui-scope-sweep)",
  fillColor = "var(--pui-scope-fill)",
}) {
  const plot = { x: PAD, top: PAD, bottom: height - PAD, w: VIEW_WIDTH - PAD * 2 };

  const res01 = clamp(resonance01, 0, 1);
  const peakDb = mapRange(res01, 0, 1, 3, 22);
  const bw = mapRange(res01, 0, 1, 0.85, 0.18);
  const baseHz = clamp(baseFreqHz, F_MIN, F_MAX);

  const fcL = baseHz * Math.pow(SWEEP_RATIO_MAX, modL);
  const fcR = baseHz * Math.pow(SWEEP_RATIO_MAX, modR);
  const depth = clamp(sweepDepth01, 0, 1);
  const ratio = Math.pow(SWEEP_RATIO_MAX, depth);

  const basePath = useMemo(() => bumpPath(baseHz, peakDb, bw, plot), [baseHz, peakDb, bw, plot]);
  const leftPath = useMemo(() => bumpPath(fcL, peakDb, bw, plot), [fcL, peakDb, bw, plot]);
  const rightPath = useMemo(() => bumpPath(fcR, peakDb, bw, plot), [fcR, peakDb, bw, plot]);

  const bandX1 = xForHz(baseHz / ratio, plot);
  const bandX2 = xForHz(baseHz * ratio, plot);

  return (
    <div className="pui-scope" style={{ height }}>
      <svg className="pui-scope__svg" viewBox={`0 0 ${VIEW_WIDTH} ${height}`} preserveAspectRatio="none">
        {depth > 0.001 && (
          <rect
            x={bandX1}
            y={plot.top}
            width={bandX2 - bandX1}
            height={plot.bottom - plot.top}
            fill={sweepColor}
            opacity="0.14"
          />
        )}

        <Grid plot={plot} />

        {[leftPath, rightPath].map((p, i) => (
          <path key={i} d={p + `L ${plot.x + plot.w} ${plot.bottom} L ${plot.x} ${plot.bottom} Z`} fill={fillColor} stroke="none" />
        ))}
        {[leftPath, rightPath].map((p, i) => (
          <path key={i} d={p} fill="none" stroke={sweepColor} strokeWidth="1.2" opacity="0.6" />
        ))}

        <path d={basePath} fill="none" stroke={baseColor} strokeWidth="2" strokeLinecap="round" />
      </svg>
    </div>
  );
}
