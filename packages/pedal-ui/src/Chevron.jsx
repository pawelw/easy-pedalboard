/**
 * The stepper arrow, shared by everything that steps through a list:
 * `StageRouter`'s placement switch, `EngineStepper`'s engine list, and the
 * host header's preset buttons. One path, drawn at whatever size the caller
 * needs - the design uses 7x10 inside a recessed well and 8x12 on the
 * standalone chrome buttons, which is the same glyph at two scales rather
 * than two glyphs.
 *
 * `currentColor` so the arrow takes its ink from CSS rather than an SVG
 * attribute - WebKit (the plugin's WKWebView) doesn't resolve `var(...)`
 * written into an SVG presentation attribute, only a plain keyword like this.
 *
 * `direction="updown"` is the pair of arrows a select box carries instead: it
 * doesn't step anything, it says the field opens. Same file because it is the
 * same family of mark, and a caller picking between them is picking a
 * direction.
 */
export default function Chevron({ direction = "right", width = 7, height = 10 }) {
  if (direction === "updown") {
    return (
      <svg
        width={width}
        height={height}
        viewBox="0 0 9 12"
        fill="none"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinecap="round"
        aria-hidden="true"
      >
        <path d="M1.5 4.6 L4.5 1.6 L7.5 4.6" />
        <path d="M1.5 7.4 L4.5 10.4 L7.5 7.4" />
      </svg>
    );
  }

  return (
    <svg
      width={width}
      height={height}
      viewBox="0 0 7 10"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.7"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d={direction === "left" ? "M5.2 1.2 L1.6 5 L5.2 8.8" : "M1.8 1.2 L5.4 5 L1.8 8.8"} />
    </svg>
  );
}
