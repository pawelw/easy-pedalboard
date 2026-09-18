import "./VerticalTabs.css";

/**
 * A vertical tab rail: the same recessed-slab language as `ModuleTabs` (the
 * selected tab drops its border and takes the panel's own fill, so it reads
 * as one surface with the content beside it), turned 90 degrees to run down
 * the left edge of a wide face instead of across the foot of a narrow one.
 *
 * `tabs`: `[{id, label}]`. `value`/`onChange` as `ModuleTabs` - the caller
 * owns which tab is shown.
 */
export default function VerticalTabs({ tabs, value, onChange }) {
  return (
    <div className="pui-reset pui-vtabs">
      {tabs.map(({ id, label }) => (
        <button
          key={id}
          type="button"
          className="pui-vtabs__tab"
          data-active={value === id ? true : undefined}
          onClick={() => onChange?.(id)}
        >
          <span className="pui-vtabs__label">{label}</span>
        </button>
      ))}
    </div>
  );
}
