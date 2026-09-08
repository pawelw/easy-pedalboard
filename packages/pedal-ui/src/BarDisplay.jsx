import "./BarDisplay.css";

/**
 * A recessed well with a row of bars in it: the small non-interactive display
 * a module puts above its knobs to show what its engine is doing. Peak
 * Machine has two, and they are the same object at two settings - the
 * Modulation module's tremolo envelope and the Reverb module's decay.
 *
 * `heights` is a list of pixel heights, and it is the whole of the data. What
 * those heights mean is the face's business: a tremolo traces its shaped LFO
 * over one cycle, a reverb traces its tail. Neither is a live meter - both are
 * drawn from the knob positions, so nothing here animates on its own.
 *
 * `align`:
 *  - `"centre"` (default) hangs the bars off a centre line, spread the full
 *    width of the well. An envelope has a middle, so it is drawn about one.
 *  - `"bottom"` stands them on the floor, packed and centred. A decay only
 *    ever falls towards silence, so it reads off a baseline instead.
 *
 * No caption, in either case. The engine stepper directly above already names
 * what this is showing, and a second label under it was the one thing in the
 * handoff's earlier draft that made the module feel crowded.
 *
 * Bars take the module's accent from `--pui-accent`, so a `ModulePanel` colours
 * this the same way it colours its stepper and its knobs' arcs.
 */
export default function BarDisplay({
  heights,
  align = "centre",
  barWidth = align === "centre" ? 3 : 4,
  opacity = align === "centre" ? 0.85 : 0.8,
  height = 63,
  ariaLabel,
}) {
  return (
    <div
      className={`pui-reset pui-bars pui-bars--${align}`}
      style={{ height }}
      role={ariaLabel ? "img" : "presentation"}
      aria-label={ariaLabel}
    >
      {align === "centre" && <span className="pui-bars__centre-line" />}
      {heights.map((h, i) => (
        <i key={i} className="pui-bars__bar" style={{ height: h, width: barWidth, opacity }} />
      ))}
    </div>
  );
}
