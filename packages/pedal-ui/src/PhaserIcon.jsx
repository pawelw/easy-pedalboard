/**
 * A notch: a flat line dropping away and coming back. What a phaser does to a
 * spectrum, drawn the way `FilterIcon` draws what a filter pair does - the
 * shape of the result, not a picture of the mechanism.
 *
 * The inverse of FilterIcon on purpose. That one is a band standing up out of
 * a flat line; this is a band taken out of one, and at 26px the pair reads as
 * two halves of the same idea.
 */
export default function PhaserIcon({ size = 26 }) {
  return (
    <svg
      className="pui-phaser-icon"
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
      <path d="M2 11 C9 11, 11 29, 17 29 C23 29, 25 11, 31 11 L42 11" />
    </svg>
  );
}
