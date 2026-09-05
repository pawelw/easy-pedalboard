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
 */
export default function Dropdown({ options, value, onChange, caption, placeholder, menuAlign = "start" }) {
  return (
    <div className="pui-reset pui-dropdown">
      <Select
        unstyled
        data={options}
        value={value}
        onChange={onChange}
        placeholder={placeholder}
        allowDeselect={false}
        comboboxProps={{
          position: `bottom-${menuAlign}`,
          classNames: { dropdown: "pui-dropdown__panel", option: "pui-dropdown__option" },
        }}
        classNames={{ wrapper: "pui-dropdown__wrapper", input: "pui-dropdown__input", section: "pui-dropdown__section" }}
      />
      {caption && <div className="pui-caption pui-dropdown__caption">{caption}</div>}
    </div>
  );
}
