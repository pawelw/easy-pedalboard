/**
 * A loud bump pressed down to the level of the quiet ones either side, under
 * a flat ceiling: what a compressor does to a phrase. Marks the Comp engine in
 * the Artifact module's stepper.
 *
 * Same 44x36 family as `CrushIcon` / `RustIcon` / `BitIcon`, and like them
 * takes the module accent through `currentColor`.
 */
export default function CompIcon({ size = 26 }) {
  return (
    <svg
      className="pui-comp-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3.4"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M3 8 H41" strokeWidth="2.2" strokeDasharray="3 4" />
      <path d="M2 30 C6 30, 7 20, 11 20 C14 20, 15 14, 22 14 C29 14, 30 20, 33 20 C37 20, 38 30, 42 30" />
    </svg>
  );
}
