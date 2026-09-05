import { useMemo } from "react";

const STEPS = 24;

// The bump's peak slides from off-screen left to off-screen right as type01
// sweeps 0..1: at the low end only its falling right shoulder is in frame
// (a low-pass magnitude response), centred it's a full symmetric hump
// (band-pass), and at the high end only its rising left shoulder shows
// (high-pass) - one continuous shape for the same three anchors the knob's
// own readout names.
const PEAK_MIN = -0.3;
const PEAK_MAX = 1.3;
const BUMP_WIDTH = 0.4;

/** A tiny filter-response curve for the Filter Type knob, echoing the shape
    of what it actually does rather than an arbitrary glyph. */
export default function FilterCurveIcon({ type01, size = 28, color = "currentColor" }) {
  const d = useMemo(() => {
    const center = PEAK_MIN + (PEAK_MAX - PEAK_MIN) * type01;
    const amp = size * 0.5;
    const baseY = size * 0.75;
    let path = "";
    for (let i = 0; i <= STEPS; i++) {
      const t = i / STEPS;
      const x = t * size;
      const bump = Math.exp(-((t - center) * (t - center)) / (2 * BUMP_WIDTH * BUMP_WIDTH));
      const y = baseY - bump * amp;
      path += (i === 0 ? "M" : "L") + x.toFixed(2) + " " + y.toFixed(2) + " ";
    }
    return path;
  }, [type01, size]);

  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} className="pui-filter-curve-icon">
      <path d={d} fill="none" stroke={color} strokeWidth={Math.max(1.4, size * 0.06)} strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
