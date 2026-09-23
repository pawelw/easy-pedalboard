/**
 * A four-point sparkle with a smaller one above it: the Reverb module's Shimmer
 * engine, whose tail climbs an octave on every pass. The sparkle rather than
 * SpaceIcon's rings because the pitch climbing is what sets this engine apart
 * from the Modern one beside it, which keeps the rings.
 *
 * Same 44x36 family as the other engine glyphs, so all three Reverb engines sit
 * at one size in an `EngineStepper`.
 */
export default function ShimmerIcon({ size = 26 }) {
  return (
    <svg
      className="pui-shimmer-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M18 6 Q19.5 18.5 32 20 Q19.5 21.5 18 34 Q16.5 21.5 4 20 Q16.5 18.5 18 6 Z" />
      <path d="M34 3 Q34.6 7.4 39 8 Q34.6 8.6 34 13 Q33.4 8.6 29 8 Q33.4 7.4 34 3 Z" opacity=".55" />
    </svg>
  );
}
