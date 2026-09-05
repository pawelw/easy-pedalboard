import "./TapScope.css";

const MAX_TAP_HEIGHT = 30;
const MIN_TAP_HEIGHT = 5;
const MAX_TAPS = 8;

// Each tap is the previous one times feedback01, dropped once it's too
// faint to read as a tap - geometric decay rather than the prototype's
// static reference heights, so this stays correct as feedback actually
// moves once a real host feed is wired (see COMPONENTS.md's TapScope
// section: even horizontal spacing is fine for v1, but the decay itself
// is meant to track feedback01).
function tapHeights(feedback01) {
  const heights = [];
  for (let n = 1; n <= MAX_TAPS; n++) {
    const h = MAX_TAP_HEIGHT * Math.pow(feedback01, n);
    if (h < MIN_TAP_HEIGHT) break;
    heights.push(h);
  }
  return heights.length ? heights : [MAX_TAP_HEIGHT];
}

function Lane({ heights, bottom }) {
  return (
    <div className={`pui-tapscope__lane${bottom ? " pui-tapscope__lane--bottom" : ""}`}>
      {heights.map((h, i) => (
        <span key={i} className="pui-tapscope__tap" style={{ height: h, animationDelay: `${i * 0.18}s` }} />
      ))}
    </div>
  );
}

/**
 * The stereo repeat visualiser above Delay's control row: two lanes hanging
 * off a shared centre line, left channel above it, right below, each tap
 * fading on a loop the way a real repeat decays into the noise floor.
 *
 * `leftTime01`/`rightTime01` aren't used for horizontal spacing yet - both
 * lanes space their taps evenly, which COMPONENTS.md calls acceptable for
 * v1. They're still part of the API so a later pass can derive tap position
 * from them without changing how callers use this component.
 */
export default function TapScope({ height = 78, leftTime01 = 0.5, rightTime01 = 0.5, feedback01 = 0.6 }) {
  const heights = tapHeights(feedback01);

  return (
    <div className="pui-reset pui-tapscope" style={{ height }}>
      <span className="pui-tapscope__label pui-tapscope__label--l">L</span>
      <span className="pui-tapscope__label pui-tapscope__label--r">R</span>
      <div className="pui-tapscope__centre" />
      <Lane heights={heights} />
      <Lane heights={heights} bottom />
    </div>
  );
}
