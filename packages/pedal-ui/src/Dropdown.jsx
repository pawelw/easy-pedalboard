import { Select } from "@mantine/core";
import "./Dropdown.css";

/**
 * A rack-panel select box, built on Mantine's Select with its styling
 * stripped so ours applies instead - keyboard navigation, typeahead and
 * ARIA combobox semantics come from Mantine for free.
 *
 * `options`: array of strings, or {value, label} objects (Mantine's own
 * `data` shape) - passed straight through.
 */
export default function Dropdown({ options, value, onChange, caption, placeholder }) {
  return (
    <div className="pui-reset pui-dropdown">
      <Select
        unstyled
        data={options}
        value={value}
        onChange={onChange}
        placeholder={placeholder}
        allowDeselect={false}
        comboboxProps={{ classNames: { dropdown: "pui-dropdown__panel", option: "pui-dropdown__option" } }}
        classNames={{ wrapper: "pui-dropdown__wrapper", input: "pui-dropdown__input", section: "pui-dropdown__section" }}
      />
      {caption && <div className="pui-caption pui-dropdown__caption">{caption}</div>}
    </div>
  );
}
