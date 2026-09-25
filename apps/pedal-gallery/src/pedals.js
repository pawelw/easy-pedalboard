import BitBitDelayFace from "bitbit-delay-jsui/src/App.jsx";
import BitBitAlpineFace from "bitbit-alpine-jsui/src/App.jsx";
import BitBitArtifactFace from "bitbit-artifact-jsui/src/App.jsx";
import BitBitModulationFace from "bitbit-modulation-jsui/src/App.jsx";
import BitBitReverbFace from "bitbit-reverb-jsui/src/App.jsx";
import BitBitWahFace from "bitbit-wah-jsui/src/App.jsx";
import BitBitGrainFace from "bitbit-grain-jsui/src/App.jsx";

// The shipping plan, not a mirror of plugins/: BitBit Alpine, each of its four
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
  { slug: "bitbit-alpine", name: "BitBit Alpine", group: "alpine", face: BitBitAlpineFace, theme: "light" },
  { slug: "bitbit-reverb", name: "BitBit Reverb", group: "alpine", face: BitBitReverbFace, theme: "onyx" },
  { slug: "bitbit-delay", name: "BitBit Delay", group: "alpine", face: BitBitDelayFace, theme: "onyx" },
  { slug: "bitbit-modulation", name: "BitBit Modulation", group: "alpine", face: BitBitModulationFace, theme: "onyx" },
  { slug: "bitbit-artifact", name: "BitBit Artifact", group: "alpine", face: BitBitArtifactFace, theme: "onyx" },

  { slug: "bitbit-wah", name: "BitBit Wah", group: "standalone", face: BitBitWahFace },
  { slug: "bitbit-grain", name: "BitBit Grains", group: "standalone", face: BitBitGrainFace, theme: "onyx" },
  { slug: "bitbit-eq", name: "BitBit EQ", group: "standalone", face: null },
];
