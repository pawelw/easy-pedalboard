/**
 * The Artifact module's Easy macros, keyed by engine name. Artifact is
 * `ArtifactFace` from `@synthpeak/artifact-face`, a face shared with the
 * standalone BitBit Artifact pedal - so its Easy config is handed in from here
 * as a prop rather than living on that package's own engine table, which the
 * standalone pedal (no Easy tab) has no use for.
 *
 * Leaf ids carry the engine's own sub-prefix (`crush.`, `ring.`, `rust.`,
 * `amp.`); the face resolves them through its `art.` ParamScope.
 */
export const ARTIFACT_EASY = {
  Ring: {
    name: "Metal",
    targets: [
      { id: "ring.freq", min: 0.2, max: 0.7 },
      { id: "ring.tweak", min: 0.1, max: 0.6 },
      { id: "ring.lp", min: 0.5, max: 0.95 },
    ],
  },
  Crasher: {
    name: "Crush",
    targets: [
      { id: "crush.bits", min: 0.2, max: 0.8 },
      { id: "crush.rate", min: 0.25, max: 0.8 },
      { id: "crush.jitter", min: 0.0, max: 0.4 },
    ],
  },
  Rust: {
    name: "Decay",
    targets: [
      { id: "rust.grind", min: 0.15, max: 0.9 },
      { id: "rust.tone", min: 0.35, max: 0.9 },
    ],
  },
  Amp: {
    name: "Drive",
    targets: [
      { id: "amp.drive", min: 0.2, max: 0.9 },
      { id: "amp.bit", min: 0.2, max: 0.8 },
      { id: "amp.mids", min: 0.0, max: 0.7 },
    ],
  },
};
