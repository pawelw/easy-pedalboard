import { useState } from "react";
import Button from "./Button.jsx";
import PresetPicker from "./PresetPicker.jsx";
import PresetSaveDialog from "./PresetSaveDialog.jsx";
import "./PresetBar.css";

// What the bar shows with nothing behind it: a plain browser tab, the
// component gallery, a pedal whose processor has no store wired up yet. The
// picker still opens and still reads correctly - it just cannot load anything.
// Categorised entries among them on purpose: the picker files a factory
// preset by the prefix in its name (PresetPicker's CATEGORY_SEPARATOR), and a
// demo list of leaves alone would show a column arrangement the real faces
// have and the gallery doesn't.
const DEMO_FACTORY = [
  "Init",
  "Slapback",
  "Dotted Eighth",
  "Tape Wash",
  "Long Trails",
  "Modulated - Deep Wow",
  "Modulated - Phase Trails",
  "Digital - Clean Quarters",
  "Digital - Glass Halves",
];

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
 * Presentational and stateless about the presets themselves - it is handed two
 * lists and a selection and calls back. `JucePresetBar` is the same bar with
 * the native bridge wired to it, and is what a pedal actually drops into its
 * header; this stays prop-driven so the gallery, and any face without a store
 * behind it, can still render one.
 *
 * The only state it does own is whether the save dialog is open and what the
 * last save said, because neither is anyone else's business.
 *
 * Peak Wah has its own copy of this (plugins/peak-wah/jsui/src/PresetBar.jsx)
 * predating the shared one, styled with literal colours against that pedal's
 * cream panel. Fold it in here when Wah is next touched; doing it blind would
 * move that face's header for no reason of its own.
 */
export default function PresetBar({
  factory = DEMO_FACTORY,
  user = [],
  value,
  canAuthor = false,
  onLoad,
  onStep,
  onSave,
}) {
  const [dialogOpen, setDialogOpen] = useState(false);
  const [error, setError] = useState("");

  const save = async (kind, name) => {
    // onSave answers with the native side's verdict, so a name the store
    // refused keeps the dialog open with the name still in the box.
    const result = await onSave?.(kind, name);

    if (result && result.ok === false) {
      setError(result.error || "Could not save that preset.");
      return;
    }

    setError("");
    setDialogOpen(false);
  };

  return (
    <div className="pui-reset pui-presetbar">
      <div className="pui-presetbar__group">
        <Button onClick={() => onStep?.(-1)} aria-label="Previous preset">
          <ChevronLeftIcon />
        </Button>
        <Button onClick={() => onStep?.(1)} aria-label="Next preset">
          <ChevronRightIcon />
        </Button>
        <PresetPicker factory={factory} user={user} value={value} onChange={onLoad} />
      </div>

      <Button
        onClick={() => {
          setError("");
          setDialogOpen(true);
        }}
        aria-label="Save preset"
      >
        <SaveIcon />
      </Button>

      <PresetSaveDialog
        open={dialogOpen}
        // Offers the loaded preset's name, so re-saving your own is one click
        // and two keys. Saving over a *factory* name is allowed and makes a
        // user preset that shadows it - the shipped one is in the binary and
        // is not going anywhere.
        initialName={value?.name ?? ""}
        canAuthor={canAuthor}
        error={error}
        onSave={save}
        onCancel={() => setDialogOpen(false)}
      />
    </div>
  );
}
