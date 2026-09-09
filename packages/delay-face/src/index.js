export { default as DelayFace } from "./DelayFace.jsx";

// The face's own JUCE hooks, exported for a host that needs the same numbers
// outside the face - Peak Alpine's editor drives one meter feed for three
// modules, and its chrome reads the host tempo off it.
export { useDelayMeter, useDelayTimesMs, useHostBpm, useTimeReadoutText } from "./juceBindings.jsx";
