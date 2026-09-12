import "@mantine/core/styles.css";
import "./tokens.css";

export { default as PedalUIProvider } from "./Provider.jsx";
export { default as Card } from "./Card.jsx";
export { default as Logo } from "./Logo.jsx";
export { default as Knob } from "./Knob.jsx";
export { default as Slider } from "./Slider.jsx";
export { default as Toggle } from "./Toggle.jsx";
export { default as Button } from "./Button.jsx";
export { default as Dropdown } from "./Dropdown.jsx";
export { default as PresetBar } from "./PresetBar.jsx";
// The bar wired to a processor's ee::plugin::PresetStore - what a pedal
// actually drops into its header. PresetBar above stays prop-driven for the
// gallery and for a face with no store behind it.
export { default as JucePresetBar } from "./JucePresetBar.jsx";
export { default as PresetPicker } from "./PresetPicker.jsx";
export { default as PresetSaveDialog } from "./PresetSaveDialog.jsx";
export { default as WaveIcon } from "./WaveIcon.jsx";
export { default as FilterCurveIcon } from "./FilterCurveIcon.jsx";
export { default as FilterScope } from "./FilterScope.jsx";
export { default as CrushScope } from "./CrushScope.jsx";
export { default as RingScope } from "./RingScope.jsx";
export { default as RustScope } from "./RustScope.jsx";
export { freqHzFor01 } from "./autowah.js";
// The shaped LFO the tremolo engine runs on, ported from ee/dsp/Lfo.h - so a
// face drawing that engine's envelope draws the wave it actually produces.
export { lfoValue } from "./lfo.js";
export { default as Readout } from "./Readout.jsx";
export { default as TapScope } from "./TapScope.jsx";
export { default as SliderRow } from "./SliderRow.jsx";
export { default as Pill } from "./Pill.jsx";
export { default as LinkIcon } from "./LinkIcon.jsx";
export { default as SectionLabel } from "./SectionLabel.jsx";
export { default as StageControl } from "./StageControl.jsx";
export { default as StageGroup } from "./StageGroup.jsx";
export { default as StageHeader } from "./StageHeader.jsx";
export { default as StageRouter } from "./StageRouter.jsx";
export { default as TapeIcon } from "./TapeIcon.jsx";
export { default as ModIcon } from "./ModIcon.jsx";
export { default as FilterIcon } from "./FilterIcon.jsx";

// The multi-effect host shell (design_handoff_peak_alpine/). ModulePanel is
// the compartment a whole pedal's worth of controls goes into; the four below
// it are what a module's own chrome is made of.
export { default as ModulePanel } from "./ModulePanel.jsx";
export { default as ModuleTabs } from "./ModuleTabs.jsx";
export { default as PowerToggle } from "./PowerToggle.jsx";
export { default as EngineStepper } from "./EngineStepper.jsx";
export { default as BarDisplay } from "./BarDisplay.jsx";
export { default as Chevron } from "./Chevron.jsx";

// Engine glyphs, in the same 44x36 family as TapeIcon/ModIcon/FilterIcon.
// There is deliberately no ChorusIcon: the mark the design draws for the
// Chorus engine is ModIcon, path for path - it is the same smeared sine, and
// a second copy under a second name is how the two would drift apart.
export { default as TremoloIcon } from "./TremoloIcon.jsx";
export { default as PhaserIcon } from "./PhaserIcon.jsx";
export { default as CrushIcon } from "./CrushIcon.jsx";
export { default as RustIcon } from "./RustIcon.jsx";
export { default as BitIcon } from "./BitIcon.jsx";
export { default as SpaceIcon } from "./SpaceIcon.jsx";
export { default as SpringIcon } from "./SpringIcon.jsx";
export { default as PowerIcon } from "./PowerIcon.jsx";
export { default as SaveIcon } from "./SaveIcon.jsx";
