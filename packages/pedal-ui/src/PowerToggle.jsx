import PowerIcon from "./PowerIcon.jsx";
import "./PowerToggle.css";

/**
 * The control that turns one thing on or off: a 22px ring with the power glyph
 * inside it and no label at all.
 *
 * The same control at both scopes a face has one - a module's own toggle, where
 * the module's name sits immediately after it and is the label, and the host
 * header's global bypass, where what it governs is the panel it is on. An
 * earlier draft gave the global one a capsule with `ACTIVE`/`BYPASSED` written
 * in it, on the argument that at plugin scope the state has to be readable
 * without decoding a colour. It does - but the row of modules behind it already
 * dims as one object when it is off, which says the same thing far louder than
 * a 9px word, and two shapes for one idea made the header read as two kinds of
 * control.
 *
 * Inside a `ModulePanel` the ring takes that module's accent off `--pui-accent`,
 * so it never learns which module it is in; with no panel above it, it falls
 * back to the face's ink. Off, both drop to `--pui-ink-dim`.
 *
 * `on` is the engaged state, so a caller holding a *bypass* parameter passes its
 * inverse - the control is named for power, not for bypass, and every use of it
 * in the design lights when the thing is running.
 *
 * Not built on `Button`: that one is a rack-panel pill with its own padding,
 * border weight and pressed-fill, and every one of those is different here.
 * What they would have shared is a `<button>` and a click handler.
 */
export default function PowerToggle({ on = true, onToggle, ariaLabel = "Power" }) {
  return (
    <button
      type="button"
      className="pui-reset pui-power"
      data-on={on || undefined}
      aria-label={ariaLabel}
      aria-pressed={on}
      onClick={() => onToggle?.(!on)}
    >
      <PowerIcon size={12} />
    </button>
  );
}
