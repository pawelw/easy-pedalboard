import { Cascader } from "@mantine/core";
import "./PresetPicker.css";

// Cascader values have to be unique across the whole tree, so a preset's own
// name cannot be its value - a user preset called "Init" would collide with
// the factory one. The bank is the prefix, which also means a path decodes
// straight back into the (kind, name) pair the native bridge speaks.
const USER_GROUP = "user";
const idFor = (kind, name) => `${kind}/${name}`;

// The one child a User Presets node with nothing in it still shows. Without
// it the node has no children at all, which reads as a selectable leaf - a
// preset called "User Presets" that does nothing when you pick it.
const EMPTY_USER = { value: "user/", label: "No user presets yet", disabled: true };

/**
 * The preset list, as a Mantine Cascader: **User Presets** is a column that
 * opens sideways into whatever you have saved, and the factory presets sit
 * under it as an ordinary flat list.
 *
 * That asymmetry is the point rather than an accident of the component. The
 * factory bank is fixed, shipped with the plugin and usually short, so it
 * wants to be one keystroke away; the user bank is the one that grows without
 * limit, so it is the one that earns a column of its own. A single flat list
 * of both would put a hundred of your own presets in front of the five that
 * came with the pedal.
 *
 * `value` is `{ kind, name }`, not a Cascader path - callers deal in the same
 * pair the native side does and this translates at the edges.
 */
export default function PresetPicker({ factory = [], user = [], value, onChange, placeholder = "Presets" }) {
  const data = [
    {
      value: USER_GROUP,
      label: "User Presets",
      children: user.length > 0 ? user.map((name) => ({ value: idFor("user", name), label: name })) : [EMPTY_USER],
    },
    ...factory.map((name) => ({ value: idFor("factory", name), label: name })),
  ];

  // A user preset is two levels down, a factory one is a top-level leaf.
  const path = value?.name
    ? value.kind === "user"
      ? [USER_GROUP, idFor("user", value.name)]
      : [idFor("factory", value.name)]
    : null;

  const handleChange = (next) => {
    const leaf = next?.[next.length - 1];

    if (!leaf) return;

    const separator = leaf.indexOf("/");
    onChange?.({ kind: leaf.slice(0, separator), name: leaf.slice(separator + 1) });
  };

  return (
    <div className="pui-reset pui-dropdown pui-presetpicker">
      <Cascader
        unstyled
        data={data}
        value={path}
        onChange={handleChange}
        placeholder={placeholder}
        allowDeselect={false}
        expandTrigger="hover"
        // The input shows the preset, not the route to it: "Tape Wash", never
        // "User Presets / Tape Wash". The column you walked through to get
        // there is not part of the preset's name and the closed control is
        // only 118px wide.
        formatValue={({ options }) => options[options.length - 1]?.label ?? ""}
        // Rendered in place rather than portalled to <body>. The pedal's
        // palette is a `data-pui-theme` attribute on a wrapper div that
        // PedalUIProvider stamps (see tokens.css), and every token here is
        // inherited from it - a panel portalled outside that wrapper resolves
        // its colours off the bare :root instead and comes out in the light
        // theme on a dark face.
        comboboxProps={{ position: "bottom-start", withinPortal: false }}
        classNames={{
          wrapper: "pui-dropdown__wrapper",
          input: "pui-dropdown__input",
          section: "pui-dropdown__section",
          dropdown: "pui-dropdown__panel pui-presetpicker__panel",
          columnsList: "pui-presetpicker__columns",
          column: "pui-presetpicker__column",
          columnOption: "pui-dropdown__option pui-presetpicker__option",
          columnOptionLabel: "pui-presetpicker__label",
          columnEmpty: "pui-presetpicker__empty",
        }}
      />
    </div>
  );
}
