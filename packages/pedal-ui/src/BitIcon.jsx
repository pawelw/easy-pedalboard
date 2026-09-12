/**
 * A driven peak flattening off, then falling away in steps: what Bit does to a
 * sine - Drive rounds the top flat before Crush and Rate turn the rest into a
 * staircase. Marks the Bit engine in the Artifact module's stepper, RustIcon's
 * smooth-to-jagged idea with a clipped (not rounded) bump on the left, so it
 * does not read as a second Rust glyph.
 *
 * Same 44x36 family as `CrushIcon` / `RustIcon` / `FilterIcon`, and like them
 * takes the module accent through `currentColor`.
 */
export default function BitIcon({ size = 26 }) {
  return (
    <svg
      className="pui-bit-icon"
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
      <path d="M2 27 C5 14, 9 8, 13 8 L21 8 C25 8, 27 20, 30 20" />
      <path d="M30 20 H34 V26 H38 V32 H42" />
    </svg>
  );
}
