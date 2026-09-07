import { useState } from "react";
import Button from "./Button.jsx";
import Dropdown from "./Dropdown.jsx";
import "./PresetBar.css";

// Placeholder list. There is no preset storage anywhere in the project yet -
// no file format, no native save/load bridge - so this proves out the layout
// and nothing else: picking a name, stepping with the arrows, or pressing Save
// changes no parameter and writes nothing. Wiring it up means giving the
// processor a program list and this component the callbacks below.
const DEFAULT_PRESETS = ["Init", "Slapback", "Dotted Eighth", "Tape Wash", "Long Trails"];

function ChevronLeftIcon() {
  return (
    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
      <path d="M15 18 9 12l6-6" />
    </svg>
  );
}

function ChevronRightIcon() {
  return (
    <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
      <path d="M9 18l6-6-6-6" />
    </svg>
  );
}

function SaveIcon() {
  return (
    <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2Z" />
      <path d="M17 21v-8H7v8" />
      <path d="M7 3v5h8" />
    </svg>
  );
}

/**
 * Browse and save: prev/next/name as one joined control, then Save.
 *
 * Holds the selected name itself, because with no storage behind it there is
 * nothing else that could - `onChange`/`onSave` are there for the day there
 * is. Token-driven throughout, so it reads correctly on a light face and on
 * onyx without either one restyling it.
 *
 * Peak Wah has its own copy of this (plugins/peak-wah/jsui/src/PresetBar.jsx)
 * predating the shared one, styled with literal colours against that pedal's
 * cream panel. Fold it in here when Wah is next touched; doing it blind would
 * move that face's header for no reason of its own.
 */
export default function PresetBar({ presets = DEFAULT_PRESETS, onChange, onSave }) {
  const [index, setIndex] = useState(0);
  const preset = presets[index];

  const select = (next) => {
    setIndex(next);
    onChange?.(presets[next]);
  };

  const step = (delta) => select((index + delta + presets.length) % presets.length);
  const selectByName = (name) => select(Math.max(0, presets.indexOf(name)));

  return (
    <div className="pui-reset pui-presetbar">
      <div className="pui-presetbar__group">
        <Button onClick={() => step(-1)} aria-label="Previous preset">
          <ChevronLeftIcon />
        </Button>
        <Button onClick={() => step(1)} aria-label="Next preset">
          <ChevronRightIcon />
        </Button>
        <Dropdown options={presets} value={preset} onChange={selectByName} />
      </div>

      <Button onClick={() => onSave?.(preset)} aria-label="Save preset">
        <SaveIcon />
      </Button>
    </div>
  );
}
