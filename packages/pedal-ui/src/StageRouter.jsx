import "./StageRouter.css";

/** The chevron glyphs from COMPONENTS.md §8. `currentColor` so the arrow
    takes the tone's ink through CSS rather than an SVG attribute - WebKit
    (the plugin's WKWebView) doesn't resolve `var(...)` written into an SVG
    presentation attribute, only a plain keyword like this one. */
function Chevron({ direction }) {
  return (
    <svg
      width="7"
      height="10"
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

/**
 * The recessed stepper that says where a section sits in the chain - `PRE` or
 * `POST`. Goes inside a `StageHeader`, next to the section's name.
 *
 * `value` is the text in the well; `onStep(delta)` is called with -1 or +1.
 * Both arrows are kept even though the parameter behind them has only two
 * states and either arrow reaches the other one: the pair is what signals
 * "this is configurable". Neither is ever disabled - stepping wraps.
 *
 * `label` names the section for screen readers, since the visible name lives
 * in the header beside this rather than inside it.
 */
export default function StageRouter({ value, onStep, label = "Stage" }) {
  return (
    <div className="pui-reset pui-router">
      <button
        type="button"
        className="pui-router__arrow"
        aria-label={`${label}: previous placement`}
        onClick={() => onStep?.(-1)}
      >
        <Chevron direction="left" />
      </button>

      <span className="pui-router__value">{value}</span>

      <button
        type="button"
        className="pui-router__arrow"
        aria-label={`${label}: next placement`}
        onClick={() => onStep?.(1)}
      >
        <Chevron direction="right" />
      </button>
    </div>
  );
}
