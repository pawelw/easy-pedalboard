/**
 * A sine rendered as a staircase: what sample-rate and bit-depth reduction do
 * to a smooth wave, drawn as the shape of the result the way `FilterIcon` and
 * `PhaserIcon` are. Marks the Bit Crush engine in the Artifact module's stepper.
 *
 * Same 44x36 family as `TapeIcon` / `ModIcon` / `FilterIcon`; takes the module
 * accent through `currentColor`.
 */
export default function CrushIcon({ size = 26 }) {
  return (
    <svg
      className="pui-crush-icon"
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
      <path d="M2 22 H9 V13 H16 V7 H23 V13 H30 V22 H37 V29 H44" />
    </svg>
  );
}
