import Knob from "./Knob.jsx";
import "./StageControl.css";

/**
 * One knob in a footer stage (COMPONENTS.md #5): a 42px `variant="scale"` knob
 * - the same knob as the time controls - with its name and value stacked
 * beside it. This is what the face uses instead of a horizontal slider for the
 * stage controls; the vertical `Slider` is untouched and still Peak EQ/Wah's.
 *
 * Its colours come from the `--pui-stage-*` and knob tokens the enclosing
 * `StageGroup`'s tone sets, so this renders green on the tape half and onyx on
 * the mod half without knowing which it is.
 */
export default function StageControl({ name, valueLabel, value, onChange, onDragStart, onDragEnd }) {
  return (
    <div className="pui-reset pui-stage">
      <Knob
        bare
        variant="scale"
        size={42}
        sweepGap={6}
        caption={name}
        value={value}
        onChange={onChange}
        onDragStart={onDragStart}
        onDragEnd={onDragEnd}
      />

      <div className="pui-stage__text">
        <span className="pui-stage__name">{name}</span>
        <span className="pui-stage__value">{valueLabel}</span>
      </div>
    </div>
  );
}
