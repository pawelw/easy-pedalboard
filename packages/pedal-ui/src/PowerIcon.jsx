/**
 * The standby mark: a broken ring with a stem through the gap. Used at 12px by
 * both power controls Peak Alpine has - the round accent toggle in a module
 * header and the host header's ACTIVE/BYPASSED pill - which is deliberate:
 * they do the same thing at two scopes, so they should be the same glyph.
 *
 * `currentColor`, so the accent or the chrome ink reaches it through CSS. See
 * Chevron's note on why that matters in a WKWebView.
 */
export default function PowerIcon({ size = 12 }) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.7"
      strokeLinecap="round"
      aria-hidden="true"
    >
      <path d="M8 1.6 V7" />
      <path d="M12.1 3.6 A6 6 0 1 1 3.9 3.6" />
    </svg>
  );
}
