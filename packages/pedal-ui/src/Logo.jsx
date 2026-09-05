import peakLogo from "./peak-logo.png";
import "./Logo.css";

// Delicate yellow at a light touch, warming to the panel's own lit-orange
// accent (--pui-knob-sweep-lit) as the signal gets hotter - interpolated
// rather than switched, so it reads as one meter warming up, not two colours
// swapping over.
function glowRgb(level) {
  const t = Math.min(1, Math.max(0, level));
  const r = Math.round(245 + (255 - 245) * t);
  const g = Math.round(200 + (88 - 200) * t);
  const b = Math.round(90 + (27 - 90) * t);
  return `${r}, ${g}, ${b}`;
}

/**
 * The Peak brand mark - one asset, recoloured via a CSS filter (the source
 * art isn't pure black itself) rather than shipping a tinted copy per theme.
 * `--pui-logo-filter` is what does the recolouring (`brightness(0)` for a
 * dark mark on a light panel, a hue-rotated `invert()` for a light mark on
 * onyx) - both are just the filter value, so this component never needs to
 * know which theme is active. Used by Card's header by default, and
 * directly by any pedal that wants the mark placed somewhere Card doesn't
 * offer a slot for.
 *
 * `level` (0..1, optional): live input level, for a pedal that wants the
 * mark to double as a signal-activity light. A `drop-shadow` follows the
 * logo's own silhouette (its alpha survives the tint filter) rather than
 * glowing a box around it. Omit it for a plain, static mark.
 */
export default function Logo({ size = 26, className = "", level }) {
  const glow = level !== undefined && level > 0.02
    ? ` drop-shadow(0 0 ${(4 + level * 10).toFixed(1)}px rgba(${glowRgb(level)}, ${(level * 0.9).toFixed(2)}))`
    : "";
  return (
    <img
      className={`pui-logo ${className}`}
      src={peakLogo}
      alt=""
      style={{ height: size, filter: `var(--pui-logo-filter)${glow}` }}
    />
  );
}
