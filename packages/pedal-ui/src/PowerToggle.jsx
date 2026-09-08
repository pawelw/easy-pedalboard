import PowerIcon from "./PowerIcon.jsx";
import "./PowerToggle.css";

/**
 * The control that turns one thing on or off, at two scopes.
 *
 * `variant="round"` (default) is a module's own power toggle: a 22px ring in
 * that module's accent with the power glyph inside it and no label at all -
 * it sits immediately before the module's name, which is the label. Lives in
 * a `ModulePanel` header, and reads `--pui-accent` off it, so it never learns
 * which module it is in.
 *
 * `variant="pill"` is the host header's global bypass: the same glyph beside a
 * word that swaps between `ACTIVE` and `BYPASSED`. A capsule rather than a
 * ring because it governs the whole plugin rather than one panel, and because
 * at that scope which state you are in has to be readable without decoding a
 * colour.
 *
 * `on` is the engaged state in both cases, so a caller passing a bypass
 * parameter passes its inverse - the control is named for power, not for
 * bypass, and every use of it in the design lights when the thing is running.
 *
 * Not built on `Button`: that one is a rack-panel pill with its own padding,
 * border weight and pressed-fill, and every one of those is different here.
 * What they would have shared is a `<button>` and a click handler.
 */
export default function PowerToggle({
  on = true,
  onToggle,
  variant = "round",
  label,
  ariaLabel = "Power",
}) {
  const text = variant === "pill" ? (label ?? (on ? "Active" : "Bypassed")) : null;

  return (
    <button
      type="button"
      className={`pui-reset pui-power pui-power--${variant}`}
      data-on={on || undefined}
      aria-label={ariaLabel}
      aria-pressed={on}
      onClick={() => onToggle?.(!on)}
    >
      <PowerIcon size={12} />
      {text && <span className="pui-power__label">{text}</span>}
    </button>
  );
}
