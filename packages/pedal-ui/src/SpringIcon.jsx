/**
 * The coil itself: a hard zigzag flattening off at both ends, the way a spring
 * tank's wire is anchored at its transducers. The Reverb module's Spring
 * engine.
 *
 * A zigzag rather than the drawn-out helix a spring is usually shown as -
 * at 26px a helix collapses into a grey smudge, and the sharp corners are what
 * distinguish it from `SpaceIcon`'s rings at a glance.
 */
export default function SpringIcon({ size = 26 }) {
  return (
    <svg
      className="pui-spring-icon"
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
      <path d="M2 18 L8 6 L14 30 L20 6 L26 30 L32 6 L38 30 L42 18" />
    </svg>
  );
}
