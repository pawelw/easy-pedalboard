/**
 * A half-filled disc - the preset bar's theme switch. Same 16-unit grid and
 * 1.5 stroke as TunerIcon, DiceIcon and SaveIcon's chrome variant, so the row
 * reads as one family. The filled half is `currentColor` too, so the glyph
 * says "two halves of one thing" on either palette without a second token.
 */
export default function ThemeIcon({ size = 14 }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <circle cx="8" cy="8" r="6" />
      <path d="M8 2a6 6 0 0 1 0 12z" fill="currentColor" stroke="none" />
    </svg>
  );
}
