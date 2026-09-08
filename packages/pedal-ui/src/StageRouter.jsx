import Chevron from "./Chevron.jsx";
import "./StageRouter.css";

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
