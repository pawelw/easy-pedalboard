import Chevron from "./Chevron.jsx";
import "./EngineStepper.css";

/**
 * Which engine a module is running: a recessed well with an arrow at each end
 * and the engine's own glyph and name between them. Stepping wraps in both
 * directions, so neither arrow is ever disabled and neither is the "forward"
 * one - four engines is few enough that either way round is a short trip.
 *
 * The sibling of `StageRouter`, and deliberately not a variant of it. That one
 * says where a section *sits* in the chain and is a text-only switch inside a
 * `StageHeader`; this says what a module *is*, carries the engine's mark, and
 * is the widest thing in the module's body. They share the well treatment and
 * the chevron, and nothing else.
 *
 * `engines` is the ordered list of names; `value` is the current one. The
 * caller owns the mapping from name to glyph (`icon`) because that mapping is
 * the face's, not the component's - a module that wants no glyph passes none.
 *
 * The arrows and the glyph take the module's accent from `--pui-accent`, which
 * a `ModulePanel` sets; the name stays on the panel's own ink, so a row of
 * modules reads as one family with three highlights rather than three colours.
 */
export default function EngineStepper({ engines, value, icon, onChange, label = "Engine" }) {
  const step = (delta) => {
    const at = engines.indexOf(value);
    const from = at < 0 ? 0 : at;
    onChange?.(engines[(from + delta + engines.length) % engines.length]);
  };

  return (
    <div className="pui-reset pui-stepper">
      <button
        type="button"
        className="pui-stepper__arrow"
        aria-label={`${label}: previous`}
        onClick={() => step(-1)}
      >
        <Chevron direction="left" />
      </button>

      <div className="pui-stepper__value">
        {icon && <span className="pui-stepper__icon">{icon}</span>}
        <span className="pui-stepper__name">{value}</span>
      </div>

      <button
        type="button"
        className="pui-stepper__arrow"
        aria-label={`${label}: next`}
        onClick={() => step(1)}
      >
        <Chevron direction="right" />
      </button>
    </div>
  );
}
