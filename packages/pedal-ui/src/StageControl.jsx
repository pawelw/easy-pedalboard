import Knob from "./Knob.jsx";
import "./StageControl.css";

/**
 * One knob in a footer stage (COMPONENTS.md #5): a 42px `variant="soft"` knob
 * with its name printed underneath.
 *
 * The name sits below rather than beside, and the value is not shown at all
 * until the knob is being turned, when it takes the name's place for the length
 * of the drag (Knob's own caption/valueLabel swap). Two reasons, and the first
 * is the one that matters: a value permanently parked next to every stage knob
 * is a number nobody is reading, and it was costing the footer the width of a
 * third section. The second is that a label under its knob is simply where a
 * pedal prints one.
 *
 * Its colours come from the `--pui-stage-*` and knob tokens the enclosing
 * `StageGroup`'s tone sets, so this renders green on the tape half and onyx on
 * the others without knowing which it is.
 *
 * `scaleFrom` passes straight through to Knob - "max" for a cut that rests
 * open at the top of its range.
 */
export default function StageControl({
  name,
  valueLabel,
  value,
  onChange,
  onDragStart,
  onDragEnd,
  scaleFrom,
}) {
  return (
    <div className="pui-reset pui-stage">
      <Knob
        variant="soft"
        size={38}
        caption={name}
        valueLabel={valueLabel}
        scaleFrom={scaleFrom}
        value={value}
        onChange={onChange}
        onDragStart={onDragStart}
        onDragEnd={onDragEnd}
      />
    </div>
  );
}
