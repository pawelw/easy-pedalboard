/**
 * The reel-to-reel deck that marks Peak Delay's pre-stage (COMPONENTS.md #8b):
 * a deck body with feet and a head cover, two reels sitting proud of it, each
 * with three spokes and a hub ring.
 *
 * `ground` is what the reels are filled with, and it has to be whatever the
 * icon is sitting on - the reels overlap the deck body, and filling them with
 * the background is what makes them read as in front of it rather than as two
 * transparent rings with the deck's outline running through them.
 *
 * Its default is the one place in the set that has to ask where it is. The
 * icon used to sit on the Tape section's green band and filled from that
 * token; with the band gone it sits on whatever panel is behind it, which is
 * the card on Peak Delay's face and the module on Peak Machine's. So the
 * default reads `--pui-stage-ground` - which `ModulePanel` sets and a plain
 * card does not - and falls back to the panel.
 *
 * The fallback is written at the point of use rather than as a `:root` default
 * for `--pui-stage-ground`, which would resolve `var(--pui-panel)` against
 * :root's own light-theme value and inherit it down as a literal. See
 * tokens.css's note on that trap.
 *
 * That fill goes through `style`, not the `fill` attribute: WebKit (the real
 * plugin's WKWebView) doesn't reliably resolve `var(...)` written into an SVG
 * presentation attribute, only one reached through the ordinary CSS pipeline -
 * the same trap Knob.jsx's TickScale documents. Strokes use `currentColor`,
 * which is a plain keyword and safe as an attribute.
 */
export default function TapeIcon({ size = 38, ground = "var(--pui-stage-ground, var(--pui-panel))" }) {
  const reel = { fill: ground };

  return (
    <svg
      className="pui-tape-icon"
      width={size}
      height={size * (31 / 38)}
      viewBox="0 0 44 36"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <rect x="3.5" y="16.5" width="37" height="14" rx="2" />
      <rect x="6.5" y="30.5" width="4.5" height="2.4" rx="1" />
      <rect x="33" y="30.5" width="4.5" height="2.4" rx="1" />
      <rect x="15" y="21.5" width="14" height="7.2" rx="1.6" />
      <circle cx="18.2" cy="24" r="0.85" fill="currentColor" stroke="none" />
      <circle cx="25.8" cy="24" r="0.85" fill="currentColor" stroke="none" />

      {/* The two reels. The ground fill only occludes the deck body behind
          them; the rim itself is the inherited currentColor stroke, so these
          must NOT set stroke="none" - that fills ground-on-ground and the
          reels vanish, leaving just the deck and the spokes floating. */}
      <circle cx="11" cy="11" r="10" style={reel} />
      <circle cx="33" cy="11" r="10" style={reel} />

      <g fill="currentColor" stroke="none">
        <path d="M9.69 6.7 L8.37 2.4 L13.63 2.4 L12.31 6.7 Z" />
        <path d="M15.38 12.01 L19.77 13.03 L17.14 17.58 L14.07 14.29 Z" />
        <path d="M7.93 14.29 L4.86 17.58 L2.23 13.03 L6.62 12.01 Z" />
        <path d="M31.69 6.7 L30.37 2.4 L35.63 2.4 L34.31 6.7 Z" />
        <path d="M37.38 12.01 L41.77 13.03 L39.14 17.58 L36.07 14.29 Z" />
        <path d="M29.93 14.29 L26.86 17.58 L24.23 13.03 L28.62 12.01 Z" />
      </g>

      <circle cx="11" cy="11" r="3.2" />
      <circle cx="33" cy="11" r="3.2" />
    </svg>
  );
}
