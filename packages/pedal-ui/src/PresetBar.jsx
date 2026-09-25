import { useState } from "react";
import Button from "./Button.jsx";
import Chevron from "./Chevron.jsx";
import PresetPicker from "./PresetPicker.jsx";
import PresetSaveDialog from "./PresetSaveDialog.jsx";
import DiceIcon from "./DiceIcon.jsx";
import SaveIcon from "./SaveIcon.jsx";
import TunerIcon from "./TunerIcon.jsx";
import ThemeIcon from "./ThemeIcon.jsx";
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

// The joined variant's own arrows: a heavier 24-unit glyph than the shared
// Chevron's, matching the 12px squares it sits in. The separated variant uses
// the shared one, at the size the host header draws it.
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

/**
 * Browse and save: prev/next/name as one joined control, then Save and the
 * dice. The dice sets every knob to a random position (`onRandomize`; the
 * native side decides what counts as a knob and how far it may go - see
 * PresetStore::randomize) and leaves the selected preset's name showing, the
 * same as turning a knob by hand does.
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
 * BitBit Wah has its own copy of this (plugins/bitbit-wah/jsui/src/PresetBar.jsx)
 * predating the shared one, styled with literal colours against that pedal's
 * cream panel. Fold it in here when Wah is next touched; doing it blind would
 * move that face's header for no reason of its own.
 *
 * `variant`:
 *  - `"joined"` (default) is the segmented control every pedal face carries:
 *    prev, next and the name box share their edges and read as one object,
 *    with Save beside it.
 *  - `"separated"` is BitBit Alpine's host header: discrete rounded
 *    controls at a wider size. Four rather than one because that header is a
 *    row of separate chrome objects - the level faders and the bypass pill are
 *    next to it - and a segmented group among them reads as the odd one out.
 *    The name field is also nearly twice as wide there, which is what buys the
 *    room for a preset name to be read rather than truncated.
 *
 * `showSteppers` (default on) hides the prev/next arrows for a face too narrow
 * to carry them - BitBit Artifact's single-module card - leaving just the name
 * picker and Save.
 *
 * `showDice` (default on) hides the randomise button. Off for every pedal that
 * is a single Alpine module split out on its own (BitBit Artifact, BitBit
 * Modulation, BitBit Reverb) and for BitBit Delay - dice stays only on BitBit
 * Alpine and BitBit Grain, where a whole instrument's worth of knobs makes a
 * random start worth having.
 *
 * `onTuner` adds one more button at the far end, apart from Save and the dice
 * - a tuning fork that opens the tuner. Omitted, there is no button: only a
 * pedal with a tuner behind it passes one (BitBit Alpine, through
 * JucePresetBar's `showTuner`).
 *
 * `onTheme` adds the palette switch beside it, at the very end of the row.
 * Same rule: omitted, there is no button.
 */
export default function PresetBar({
  factory = DEMO_FACTORY,
  user = [],
  value,
  canAuthor = false,
  variant = "joined",
  showSteppers = true,
  showDice = true,
  onLoad,
  onStep,
  onSave,
  onRandomize,
  onTuner,
  onTheme,
}) {
  const [dialogOpen, setDialogOpen] = useState(false);
  const [error, setError] = useState("");
  const separated = variant === "separated";

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
    <div
      className={`pui-reset pui-presetbar pui-presetbar--${variant}${showSteppers ? "" : " pui-presetbar--no-steppers"}${showDice ? "" : " pui-presetbar--no-dice"}`}
    >
      <div className="pui-presetbar__group">
        {showSteppers && (
          <>
            <Button onClick={() => onStep?.(-1)} aria-label="Previous preset">
              {separated ? <Chevron direction="left" width={8} height={12} /> : <ChevronLeftIcon />}
            </Button>
            <Button onClick={() => onStep?.(1)} aria-label="Next preset">
              {separated ? <Chevron direction="right" width={8} height={12} /> : <ChevronRightIcon />}
            </Button>
          </>
        )}
        <PresetPicker
          factory={factory}
          user={user}
          value={value}
          onChange={onLoad}
          chevron={separated ? <Chevron direction="updown" width={10} height={13} /> : undefined}
          // Two 28px buttons and the two 6px gaps between them and the field -
          // nothing to reach back across once they are gone.
          reachBack={separated && showSteppers ? 68 : undefined}
        />
      </div>

      <Button
        onClick={() => {
          setError("");
          setDialogOpen(true);
        }}
        aria-label="Save preset"
        className="pui-presetbar__save"
      >
        <SaveIcon size={separated ? 14 : 13} variant={separated ? "chrome" : "default"} />
      </Button>

      {showDice && (
        <Button
          onClick={() => onRandomize?.()}
          aria-label="Randomise knobs"
          title="Randomise knobs"
          className="pui-presetbar__dice"
        >
          <DiceIcon size={separated ? 14 : 13} />
        </Button>
      )}

      {onTuner && (
        <Button onClick={() => onTuner()} aria-label="Tuner" title="Tuner" className="pui-presetbar__tuner">
          <TunerIcon size={separated ? 14 : 13} />
        </Button>
      )}

      {onTheme && (
        <Button onClick={() => onTheme()} aria-label="Switch theme" title="Switch theme" className="pui-presetbar__theme">
          <ThemeIcon size={separated ? 14 : 13} />
        </Button>
      )}

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
