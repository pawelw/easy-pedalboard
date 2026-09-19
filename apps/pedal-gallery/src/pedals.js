import PeakDelayFace from "peak-delay-jsui/src/App.jsx";
import PeakAlpineFace from "peak-alpine-jsui/src/App.jsx";
import PeakArtifactFace from "peak-artifact-jsui/src/App.jsx";
import PeakModulationFace from "peak-modulation-jsui/src/App.jsx";
import PeakReverbFace from "peak-reverb-jsui/src/App.jsx";
import PeakWahFace from "peak-wah-jsui/src/App.jsx";
import PeakGrainFace from "peak-grain-jsui/src/App.jsx";

// The shipping plan, not a mirror of plugins/: Peak Alpine, each of its four
// modules as its own product, and two pedals that ship on their own. A pedal
// that is not on that list is not on this page, whether or not it has a face.
//
// `face` is the pedal's own real App.jsx, imported straight from its jsui
// project - nothing here re-implements a pedal's UI, so there's nothing to
// keep in sync by hand. Leave `face: null` for one whose jsui doesn't exist
// yet.
//
// `theme` picks the palette this page renders that face in - App.jsx is
// imported directly here, bypassing the pedal's own main.jsx entirely, so
// this is the one piece of per-pedal metadata the gallery has to carry
// rather than reuse. Omit it for the default light theme. It mirrors what the
// pedal's main.jsx passes to PedalUIProvider, so what the gallery shows is
// what the plugin ships.
export const groups = [
  { id: "alpine", title: "BitBit Alpine and its modules" },
  { id: "standalone", title: "Standalone pedals" },
];

export const pedals = [
  // The multi-effect host - listed first because it is the one that contains
  // the four after it rather than sitting beside them.
  { slug: "peak-alpine", name: "BitBit Alpine", group: "alpine", face: PeakAlpineFace, theme: "onyx" },
  { slug: "peak-reverb", name: "BitBit Reverb", group: "alpine", face: PeakReverbFace, theme: "onyx" },
  { slug: "peak-delay", name: "BitBit Delay", group: "alpine", face: PeakDelayFace, theme: "onyx" },
  { slug: "peak-modulation", name: "BitBit Modulation", group: "alpine", face: PeakModulationFace, theme: "onyx" },
  { slug: "peak-artifact", name: "BitBit Artifact", group: "alpine", face: PeakArtifactFace, theme: "onyx" },

  { slug: "peak-wah", name: "BitBit Wah", group: "standalone", face: PeakWahFace },
  { slug: "peak-grain", name: "BitBit Grains", group: "standalone", face: PeakGrainFace, theme: "onyx" },
  { slug: "peak-eq", name: "BitBit EQ", group: "standalone", face: null },
];
