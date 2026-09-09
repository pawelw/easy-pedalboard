export { default as ArtifactFace } from "./ArtifactFace.jsx";

// The engine and wave tables, exported for a host that needs the same lists
// outside the face (a gallery entry, a snapshot renderer). The face itself
// reads them straight from ./engines.jsx.
export { ENGINES as ARTIFACT_ENGINES, WAVES as ARTIFACT_WAVES } from "./engines.jsx";
