/**
 * A tuning fork - the preset bar's tuner button. Same 16-unit grid and 1.5
 * stroke as DiceIcon and SaveIcon's chrome variant, so the three read as one
 * family along the header.
 */
export default function TunerIcon({ size = 14 }) {
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
      <path d="M5 1.75v5.5a3 3 0 0 0 6 0v-5.5" />
      <path d="M8 10.25v4" />
    </svg>
  );
}
