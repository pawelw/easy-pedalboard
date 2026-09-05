import { useState } from "react";
import { Dropdown, Button } from "@synthpeak/pedal-ui";

// Placeholder list - there is no preset storage wired up yet (no file format,
// no native save/load bridge). This proves out the header layout; picking a
// name, stepping with the arrows, or pressing Save doesn't change any
// parameter or write anything yet.
const PRESET_NAMES = ["Init", "Deep Sweep", "Fast Chop", "Subtle Wobble"];

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
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2Z" />
      <path d="M17 21v-8H7v8" />
      <path d="M7 3v5h8" />
    </svg>
  );
}

export default function PresetBar() {
  const [index, setIndex] = useState(0);
  const preset = PRESET_NAMES[index];

  const step = (delta) => setIndex((i) => (i + delta + PRESET_NAMES.length) % PRESET_NAMES.length);
  const selectByName = (name) => setIndex(Math.max(0, PRESET_NAMES.indexOf(name)));

  return (
    <div className="pw-presets">
      <div className="pw-nav-group">
        <Button onClick={() => step(-1)} aria-label="Previous preset">
          <ChevronLeftIcon />
        </Button>
        <Button onClick={() => step(1)} aria-label="Next preset">
          <ChevronRightIcon />
        </Button>
        <Dropdown options={PRESET_NAMES} value={preset} onChange={selectByName} menuAlign="end" />
      </div>
      <Button onClick={() => {}} aria-label="Save preset">
        <SaveIcon />
      </Button>
    </div>
  );
}
