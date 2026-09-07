import "./StageHeader.css";

/**
 * A footer section's header (COMPONENTS.md #8): its icon and its name, plus
 * whatever control that section carries - a `StageRouter` on Tape, nothing on
 * Mod. This replaces the fixed `PRE-STAGE` / `POST-STAGE` captions: a section
 * names what it is, and shows where it is only if that is something the player
 * can change.
 *
 * Colours come from the `--pui-stage-*` tokens the enclosing `StageGroup`'s
 * tone sets, so the same header renders the green tape half and the onyx mod
 * half without knowing which it is.
 */
export default function StageHeader({ icon, name, children }) {
  return (
    <div className="pui-reset pui-stage-header">
      {icon && <div className="pui-stage-header__icon">{icon}</div>}
      <span className="pui-stage-header__name">{name}</span>
      {children}
    </div>
  );
}
