import Knob from "./Knob.jsx";
import SectionLabel from "./SectionLabel.jsx";
import "./StageControl.css";

/**
 * One half of Peak Delay's footer (COMPONENTS.md #5): a stage icon, the stage
 * label, and a 42px `variant="scale"` knob with its name and value stacked
 * beside it. This is what the face uses instead of a horizontal slider for
 * Tape and Mod - the vertical `Slider` is untouched and still Peak EQ/Wah's.
 *
 * `tone="tape"` re-points the plain `--pui-*` knob tokens to the `--pui-tape-*`
 * group on this wrapper (see StageControl.css). Everything inside inherits
 * them, so the very same Knob renders green here and unchanged everywhere
 * else - Knob never learns what a tone is.
 *
 * The band background, and the negative margins that bleed it to the card's
 * edge, are deliberately NOT here: how far a footer half reaches is a property
 * of the face it sits in (its card padding and corner radius), not of the
 * control. The face styles `.pui-stage--tape` for that.
 */
export default function StageControl({
  label,
  name,
  valueLabel,
  icon,
  tone = "default",
  value,
  onChange,
  onDragStart,
  onDragEnd,
}) {
  return (
    <div className={`pui-reset pui-stage pui-stage--${tone}`}>
      {icon && <div className="pui-stage__icon">{icon}</div>}

      <div className="pui-stage__main">
        <SectionLabel>{label}</SectionLabel>

        <div className="pui-stage__control">
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
      </div>
    </div>
  );
}
