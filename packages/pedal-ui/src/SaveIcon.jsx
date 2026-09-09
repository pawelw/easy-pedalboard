/**
 * A floppy disk - shutter above, label below. Factored out of PresetBar so the
 * host header's standalone save button and the bar's own use one mark.
 *
 * Two geometries, because the two contexts want different weights: the default
 * is the rounded 24-unit drawing PresetBar has always used, and
 * `variant="chrome"` is the handoff's flatter 16-unit one, which holds up
 * better at 14px on the host header's small square button.
 */
export default function SaveIcon({ size = 13, variant = "default" }) {
  if (variant === "chrome") {
    return (
      <svg
        width={size}
        height={size}
        viewBox="0 0 16 16"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinejoin="round"
        aria-hidden="true"
      >
        <path d="M2.5 2.5h8.5l2.5 2.5v8.5h-11z" />
        <path d="M5.5 2.5h5v3.5h-5z" />
        <path d="M5.5 9.5h5v4h-5z" />
      </svg>
    );
  }

  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2Z" />
      <path d="M17 21v-8H7v8" />
      <path d="M7 3v5h8" />
    </svg>
  );
}
