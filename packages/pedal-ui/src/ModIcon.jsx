/**
 * The phase-smear sine that marks Peak Delay's post-stage (COMPONENTS.md #8b):
 * one cycle drawn three times at rising x offsets and falling opacity, so it
 * reads as a waveform being dragged rather than as a static curve.
 *
 * The back copy runs off the right of the viewBox on purpose - the smear is
 * meant to look like it carries on past the icon.
 */
export default function ModIcon({ size = 38 }) {
  return (
    <svg
      className="pui-mod-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3.4"
      strokeLinecap="round"
      aria-hidden="true"
    >
      <path d="M11 24 C16 6, 24 6, 29 18 C34 30, 42 30, 47 12" opacity=".3" />
      <path d="M6 24 C11 6, 19 6, 24 18 C29 30, 37 30, 42 12" opacity=".55" />
      <path d="M1 24 C6 6, 14 6, 19 18 C24 30, 32 30, 37 12" />
    </svg>
  );
}
