/**
 * A signal eaten away: a clean swell on the left that breaks into jagged teeth
 * on the right. Marks the Rust engine in the Artifact module's stepper - the
 * corroded-contact voicing, where what comes out is the input with pieces
 * torn out of it.
 *
 * Same 44x36 family as `CrushIcon` / `ModIcon` / `FilterIcon`, and like them it
 * takes the module accent through `currentColor`. Rust used to borrow
 * `TapeIcon`, which pinned itself to the tape green and drew a reel-to-reel
 * deck - so the one engine that has nothing to do with tape was also the one
 * glyph in the stepper wearing a different colour from its module.
 */
export default function RustIcon({ size = 26 }) {
  return (
    <svg
      className="pui-rust-icon"
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
      <path d="M2 27 C7 27, 8 9, 14 9 C18 9, 19 20, 22 20" />
      <path d="M22 20 L26 8 L29 25 L32 11 L35 23 L38 13 L42 19" />
    </svg>
  );
}
