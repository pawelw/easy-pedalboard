import { useMemo } from "react";
import { lfoValue } from "./lfo.js";

const STEPS = 32;

/** One cycle of the morphing LFO wave, drawn upright regardless of the
    knob's own rotation - the shape is the reading, not the angle. Mirrored
    about the centre line so a triangle reads as a peak and a ramp as a rise,
    matching the wave that is actually heard (see lfo.js). */
export default function WaveIcon({ shape01, size = 28, color = "currentColor" }) {
  const d = useMemo(() => {
    const amp = size * 0.34;
    const midY = size / 2;
    let path = "";
    for (let i = 0; i <= STEPS; i++) {
      const t = i / STEPS;
      const x = t * size;
      const y = midY - lfoValue(t, shape01) * amp;
      path += (i === 0 ? "M" : "L") + x.toFixed(2) + " " + y.toFixed(2) + " ";
    }
    return path;
  }, [shape01, size]);

  return (
    <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} className="pui-wave-icon">
      <path d={d} fill="none" stroke={color} strokeWidth={Math.max(1.4, size * 0.06)} strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}
