import { BitIcon, CrushIcon, ModIcon, RustIcon } from "@synthpeak/pedal-ui";

/**
 * The four engines the module steps through, each with its own body. The order
 * is the order of the processor's `engine` choice parameter. Filter used to sit
 * third, before it moved to BitBit Alpine's Modulation module.
 */
export const ENGINES = [
  { name: "Ring", icon: <ModIcon size={22} />, body: "ring" },
  { name: "Crasher", icon: <CrushIcon size={22} />, body: "crush" },
  { name: "Rust", icon: <RustIcon size={22} />, body: "rust" },
  { name: "Amp", icon: <BitIcon size={22} />, body: "amp" },
];
