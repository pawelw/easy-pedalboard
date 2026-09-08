import { Cascader } from "@mantine/core";
import "./PresetPicker.css";

// Cascader values have to be unique across the whole tree, so a preset's own
// name cannot be its value - a user preset called "Init" would collide with
// the factory one. The bank is the prefix, which also means a path decodes
// straight back into the (kind, name) pair the native bridge speaks.
const USER_GROUP = "user";
const idFor = (kind, name) => `${kind}/${name}`;

// A factory preset whose *name* reads "Modulated - Deep Wow" is filed under a
// "Modulated" column and shown as "Deep Wow". The category lives in the name
// because that is all a factory preset has: the bank is a flat list of
// filenames compiled out of the pedal's presets/ folder (PresetStore.h), and
// juce_add_binary_data keeps only each file's basename, so a subfolder would
// not survive into the binary. Nothing below the UI knows about any of this -
// the store, the bridge and presetLoad all keep speaking in whole names, and
// a preset with no separator in it is a plain top-level entry exactly as
// before. Split on the FIRST separator only, so a category may not contain
// one and a preset name may.
const CATEGORY_SEPARATOR = " - ";

function split(name) {
  const at = name.indexOf(CATEGORY_SEPARATOR);
  return at < 0
    ? { category: null, label: name }
    : { category: name.slice(0, at), label: name.slice(at + CATEGORY_SEPARATOR.length) };
}

// Cascader values have to be unique across the whole tree and a category is
// not a preset, so its node gets a prefix no bank uses - which is also what
// lets handleChange below tell a branch apart from a leaf.
const GROUP_PREFIX = "group/";

/** The factory bank as the picker shows it: categories first, alphabetically,
    then whatever was left uncategorised in the order the store handed it over
    (which is the presets/ folder's own A-Z, so "Init" leads by name).

    Alphabetical rather than a table of preferred category names here: this
    component ships with every pedal and cannot know what categories any of
    them will invent, and a category missing from such a table would be the
    one silently sorted last. */
function groupFactory(factory) {
  const categories = new Map();
  const loose = [];

  for (const name of factory) {
    const { category, label } = split(name);

    if (category === null) {
      loose.push({ value: idFor("factory", name), label });
      continue;
    }

    if (!categories.has(category)) categories.set(category, []);
    categories.get(category).push({ value: idFor("factory", name), label });
  }

  const grouped = [...categories.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([category, children]) => ({ value: GROUP_PREFIX + category, label: category, children }));

  return [...grouped, ...loose];
}

// The one child a User Presets node with nothing in it still shows. Without
// it the node has no children at all, which reads as a selectable leaf - a
// preset called "User Presets" that does nothing when you pick it.
const EMPTY_USER = { value: "user/", label: "No user presets yet", disabled: true };

// The panel hangs off the whole prev/next/name group, not off the name box
// alone - so it has to reach back across the two arrow buttons to its left.
// How far back depends on how that group is drawn: 50px joined (26px buttons
// less the 1px border each shares with its neighbour), 68px separated (28px
// buttons and two 6px gaps). PresetBar knows which it asked for and passes the
// distance; see `reachBack`.
//
// mainAxis is Mantine's own default, restated because passing the object form
// of `offset` drops it.
const DEFAULT_REACH_BACK = 50;

/**
 * The preset list, as a Mantine Cascader. The first column is a list of
 * *categories* - **User Presets**, then whatever the factory bank names (see
 * CATEGORY_SEPARATOR above) - each opening sideways into its presets, with any
 * uncategorised factory preset sitting under them as a plain leaf.
 *
 * The uncategorised ones are the point of the arrangement rather than a
 * leftover. A pedal's headline presets - the init state, one example of each
 * thing the pedal does - are the ones worth reaching in a single move, and
 * they are few; everything else is a variation on one of them and belongs
 * behind the column named for what it varies. A single flat list of all of it,
 * plus a user bank that grows without limit, would bury the six that matter.
 *
 * `value` is `{ kind, name }`, not a Cascader path - callers deal in the same
 * pair the native side does and this translates at the edges.
 *
 * `chevron` replaces the mark in the closed box's right-hand slot. Omit it for
 * Mantine's own single down-arrow, which is what a select box carries and what
 * every pedal face uses; Peak Alpine's host header passes a double one,
 * because there the field sits beside two chevron *buttons* and a third
 * single arrow would read as a third button.
 *
 * `reachBack` is how far left of this box the open panel's edge should land -
 * see DEFAULT_REACH_BACK.
 */
export default function PresetPicker({
  factory = [],
  user = [],
  value,
  onChange,
  placeholder = "Presets",
  chevron,
  reachBack = DEFAULT_REACH_BACK,
}) {
  const data = [
    {
      value: USER_GROUP,
      label: "User Presets",
      children: user.length > 0 ? user.map((name) => ({ value: idFor("user", name), label: name })) : [EMPTY_USER],
    },
    ...groupFactory(factory),
  ];

  // A user preset, and a factory one in a category, are two levels down; an
  // uncategorised factory preset is a top-level leaf. User names are never
  // split - the columns there are the bank, not a taxonomy someone typed into
  // a filename.
  const factoryCategory = value?.name ? split(value.name).category : null;
  const path = value?.name
    ? value.kind === "user"
      ? [USER_GROUP, idFor("user", value.name)]
      : factoryCategory !== null
        ? [GROUP_PREFIX + factoryCategory, idFor("factory", value.name)]
        : [idFor("factory", value.name)]
    : null;

  const handleChange = (next) => {
    const leaf = next?.[next.length - 1];

    // Clicking a column rather than a preset in it: Cascader still reports the
    // path, and "User Presets" or "Modulated" is not something to try to load.
    if (!leaf || leaf === USER_GROUP || leaf.startsWith(GROUP_PREFIX)) return;

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
        // Left undefined for Mantine's own chevron - passing `undefined` here
        // is not the same as passing null, which would blank the slot.
        rightSection={chevron}
        rightSectionPointerEvents="none"
        // Rendered in place rather than portalled to <body>. The pedal's
        // palette is a `data-pui-theme` attribute on a wrapper div that
        // PedalUIProvider stamps (see tokens.css), and every token here is
        // inherited from it - a panel portalled outside that wrapper resolves
        // its colours off the bare :root instead and comes out in the light
        // theme on a dark face.
        comboboxProps={{
          position: "bottom-start",
          withinPortal: false,
          offset: { mainAxis: 8, crossAxis: -reachBack },
        }}
        classNames={{
          wrapper: "pui-dropdown__wrapper",
          input: "pui-dropdown__input",
          section: "pui-dropdown__section",
          dropdown: "pui-dropdown__panel pui-presetpicker__panel",
          columnsList: "pui-presetpicker__columns",
          column: "pui-presetpicker__column",
          columnOption: "pui-dropdown__option pui-presetpicker__option",
          columnOptionLabel: "pui-presetpicker__label",
          columnOptionIcon: "pui-presetpicker__chevron",
          columnEmpty: "pui-presetpicker__empty",
        }}
      />
    </div>
  );
}
