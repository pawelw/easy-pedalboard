import "./StageHeader.css";

/**
 * A footer section's header (COMPONENTS.md #8): its icon and its name, plus
 * whatever control that section carries - a `StageRouter` on Tape, nothing on
 * Mod. This replaces the fixed `PRE-STAGE` / `POST-STAGE` captions: a section
 * names what it is, and shows where it is only if that is something the player
 * can change.
 *
 * Colours come from the `--pui-stage-*` tokens the enclosing `StageGroup`
 * sets, except for one: `accent` re-points `--pui-stage-icon` on this header
 * alone, so each section's glyph carries its own hue while the name, the
 * knobs and the ground stay identical across all three.
 *
 * That single coloured mark is the whole of what distinguishes one section
 * from another now - it replaced a green band behind the Tape section, which
 * told you Tape was different and told you nothing about the other two. Pass
 * one of tokens.css's `--pui-stage-*` hues, not a literal.
 */
export default function StageHeader({ icon, name, accent, children }) {
  return (
    <div className="pui-reset pui-stage-header">
      {icon && (
        <div className="pui-stage-header__icon" style={accent ? { "--pui-stage-icon": accent } : undefined}>
          {icon}
        </div>
      )}
      <span className="pui-stage-header__name">{name}</span>
      {children}
    </div>
  );
}
