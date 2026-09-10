import PowerToggle from "./PowerToggle.jsx";
import "./ModulePanel.css";

/**
 * One effect module inside a multi-effect host: a header strip with its own
 * power toggle and name, a body, and an optional footer. Peak Alpine puts
 * three of these side by side.
 *
 * A panel inside a panel, not a second `Card`. Card is the enclosure - it has
 * the brand mark, the outer radius, the screws and the page padding, and it is
 * the thing a plugin window is sized to. This is a compartment within one, and
 * every one of those properties is wrong for it.
 *
 * `accent` is the module's hue, and it reaches the three places a module wears
 * one - the power toggle, the engine stepper, and the value arcs of the knobs
 * inside it - through two custom properties set on this wrapper. So nothing
 * below here takes an `accent` prop: `PowerToggle`, `EngineStepper`,
 * `BarDisplay` and `Knob` all just inherit. Pass one of tokens.css's
 * `--pui-accent-*`, not a literal, so the palette stays in one file.
 *
 * `tone="wide"` is the module carrying the content (Peak Alpine's Delay). It
 * is a padding difference and nothing else - 18px sides rather than 12px,
 * which is what a 560px panel's contents need to sit in. Every module is the
 * same panel colour; see tokens.css's note on why the wide one stopped having
 * a ground of its own. `tone="side"` is the default.
 *
 * `headerRight` is the slot a module puts its Level knob in, flush to the
 * header's right edge with no text label of its own. A module with no Level of
 * its own leaves it empty and its header simply ends at the spacer.
 *
 * `on`/`onToggle` drive the header's power toggle. Omit `onToggle` for a
 * module with nothing to bypass and the toggle is left out entirely rather
 * than rendered dead.
 *
 * **A module that is off dims**, the same way the whole row dims under the
 * host's global bypass - one language for "this is not running", at whichever
 * scope it was switched off. What does *not* dim is the power toggle itself: it
 * is the way back, so fading it would make a module hardest to find exactly
 * when someone is looking for it. It says its own state in its ink instead.
 */
export default function ModulePanel({
  name,
  accent,
  tone = "side",
  width,
  on = true,
  onToggle,
  headerRight,
  footer,
  children,
  className = "",
}) {
  return (
    <section
      className={`pui-reset pui-module pui-module--${tone} ${className}`}
      data-off={onToggle && !on ? true : undefined}
      style={{
        ...(width ? { width } : undefined),
        ...(accent ? { "--pui-accent": accent, "--pui-soft-lit": accent } : undefined),
      }}
    >
      <header className="pui-module__header">
        {onToggle && (
          <PowerToggle on={on} onToggle={onToggle} ariaLabel={`${name}: power`} />
        )}
        <span className="pui-module__name">{name}</span>
        <span className="pui-module__spacer" />
        {headerRight && <div className="pui-module__header-right">{headerRight}</div>}
      </header>

      <div className="pui-module__body">{children}</div>

      {footer && <div className="pui-module__footer">{footer}</div>}
    </section>
  );
}
