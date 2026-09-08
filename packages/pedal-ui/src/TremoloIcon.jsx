/**
 * A ripple: one line dipping and rising at an even rate. The Modulation
 * module's Tremolo engine, in the same 44x36 family as `TapeIcon`, `ModIcon`
 * and `FilterIcon` so the four sit together in an engine stepper.
 *
 * Deliberately not a sine. A tremolo is an amplitude that keeps moving, not a
 * waveform being smeared (that is `ModIcon`) - so this is drawn as a steady
 * wobble along one axis, thick enough to read as a level rather than a signal.
 */
export default function TremoloIcon({ size = 26 }) {
  return (
    <svg
      className="pui-tremolo-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="3.2"
      strokeLinecap="round"
      aria-hidden="true"
    >
      <path d="M2 18 C5 12, 8 24, 11 18 C14.5 8, 18 28, 22 18 C26 6, 30 30, 34 18 C37 11, 40 25, 42 18" />
    </svg>
  );
}
