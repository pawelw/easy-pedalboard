import "./ModuleTabs.css";

/**
 * The Easy / Adv switch at the foot of a narrow module's body: a full-width
 * two-button strip that bleeds to the panel edges. The selected tab drops its
 * top border and carries the panel's own fill, so it reads as one surface with
 * the content above it; the other tab stays a recessed slab.
 *
 * Peak Alpine's three narrow modules (Artifact, Modulation, Reverb) each carry
 * one - "Easy" shows a single macro knob that makes the musical move for the
 * whole engine, "Adv" the full parameter set. The wide Delay module has no
 * such split and no strip.
 *
 * Presentational only: `value` is `"easy"` or `"adv"`, `onChange` gets the id
 * of the tab that was clicked. The caller owns which one is shown.
 */
const TABS = [
  ["easy", "Easy"],
  ["adv", "Adv"],
];

export default function ModuleTabs({ value, onChange }) {
  return (
    <div className="pui-reset pui-module-tabs">
      {TABS.map(([id, label]) => (
        <button
          key={id}
          type="button"
          className="pui-module-tabs__tab"
          data-active={value === id ? true : undefined}
          onClick={() => onChange?.(id)}
        >
          {label}
        </button>
      ))}
    </div>
  );
}
