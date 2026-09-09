/**
 * A source and the rings coming back off it: the Reverb module's Space engine.
 * Concentric circles fading outwards, which is the reflection pattern rather
 * than a room - the engine is an FDN, and it has no walls to draw.
 *
 * Same 44x36 family as the other engine glyphs, so the two Reverb engines and
 * the four Modulation ones all sit at one size in an `EngineStepper`.
 */
export default function SpaceIcon({ size = 26 }) {
  return (
    <svg
      className="pui-space-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3"
      strokeLinecap="round"
      aria-hidden="true"
    >
      <circle cx="22" cy="18" r="2.6" fill="currentColor" stroke="none" />
      <circle cx="22" cy="18" r="7" />
      <circle cx="22" cy="18" r="12" opacity=".55" />
      <circle cx="22" cy="18" r="16.5" opacity=".3" />
    </svg>
  );
}
