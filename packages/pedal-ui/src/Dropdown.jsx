import { Select } from "@mantine/core";
import "./Dropdown.css";

/**
 * A rack-panel select box, built on Mantine's Select with its styling
 * stripped so ours applies instead - keyboard navigation, typeahead and
 * ARIA combobox semantics come from Mantine for free.
 *
 * `options`: array of strings, or {value, label} objects (Mantine's own
 * `data` shape) - passed straight through.
 *
 * `menuAlign`: "start" (default) keeps the open panel's left edge under the
 * trigger, growing right if its content is wider. "end" keeps the right
 * edge instead, growing left - for a trigger sitting near the right side of
 * its container, where growing right would run off the edge.
 *
 * `openDirection`: "down" (default) drops the panel below the trigger,
 * "up" opens it above - for a trigger sitting near the bottom of its own
 * panel, where dropping down would run off the edge or overlap whatever is
 * below it.
 *
 * `tone`: "panel" (default) is this component's own look - the plain rack
 * input, full radius, `--pui-panel` ground. "chrome" is the closed-box
 * treatment `PresetBar`'s `variant="separated"` name field uses (BitBit
 * Alpine's host header, and every separated `JucePresetBar`) - the
 * `--pui-chrome` family, 28px tall, for a dropdown that wants to read as one
 * of that header's rounded chrome objects rather than a panel-toned input.
 * `PresetBar.css` still owns that look where the field is *joined* to
 * neighbouring buttons (partial corner rounding, shared edges); this is the
 * same closed-box treatment standing alone, all four corners rounded, for a
 * dropdown that carries no steppers beside it. Only the closed box changes -
 * the open panel stays the ordinary look either way.
 */
export default function Dropdown({
  options,
  value,
  onChange,
  caption,
  placeholder,
  menuAlign = "start",
  openDirection = "down",
  tone = "panel",
}) {
  const chrome = tone === "chrome";

  return (
    <div className={`pui-reset pui-dropdown${chrome ? " pui-dropdown--chrome" : ""}`}>
      <Select
        unstyled
        data={options}
        value={value}
        onChange={onChange}
        placeholder={placeholder}
        allowDeselect={false}
        comboboxProps={{
          position: `${openDirection === "up" ? "top" : "bottom"}-${menuAlign}`,
          classNames: { dropdown: "pui-dropdown__panel", option: "pui-dropdown__option" },
        }}
        classNames={{
          wrapper: "pui-dropdown__wrapper",
          input: chrome ? "pui-dropdown__input pui-dropdown__input--chrome" : "pui-dropdown__input",
          section: "pui-dropdown__section",
        }}
      />
      {caption && <div className="pui-caption pui-dropdown__caption">{caption}</div>}
    </div>
  );
}
