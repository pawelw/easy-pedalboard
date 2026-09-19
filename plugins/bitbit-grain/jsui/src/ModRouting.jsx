import { createContext, useCallback, useContext, useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";

// Resolved once at module scope - see LfoEditor.jsx's identical note on
// lfoGetBreakpoints/lfoSetBreakpoints, which this mirrors exactly.
const lfoRoutingGet = Juce.getNativeFunction("lfoRoutingGet");
const lfoRoutingSet = Juce.getNativeFunction("lfoRoutingSet");

// Matches ee::plugin::ModRouter::kMaxAssignments (shared/include/ee/plugin/ModRouter.h).
export const MAX_ASSIGNMENTS = 8;
const DEFAULT_DEPTH = 0.3;

function toRoutingJson(assignments) {
  return JSON.stringify(assignments.map(({ paramId, depth }) => ({ paramId, depth })));
}

/** Mirrors ee::plugin::modRoutingFromJson exactly - a plain array of
    {paramId, depth}, no version wrapper (see ModRoutingJson.h's own note on
    why breakpoints have one and this doesn't). */
function parseRoutingJson(json) {
  try {
    const parsed = typeof json === "string" ? JSON.parse(json) : json;
    if (!Array.isArray(parsed)) return null;
    return parsed
      .filter((a) => a && typeof a.paramId === "string")
      .map((a) => ({ paramId: a.paramId, depth: Number(a.depth) || 0 }));
  } catch {
    return null;
  }
}

const ModRoutingContext = createContext(null);

/** The Mod tab's drag-and-drop routing table, shared by the draggable LFO
    chip (ModSourceChip) and every droppable knob (ModdableKnob) - both sit
    far apart in GrainFace's tree, so this is a context rather than prop
    drilling. Wraps the whole face (App.jsx), the same level the DndContext
    it works alongside wraps at.

    Fetches the current routing on mount and resyncs whenever the processor
    says it changed (BitBitGrainWebEditor's "lfoRouting" event, after a preset
    load) - same shape as LfoEditor.jsx's own breakpoint sync, and for the
    same reason neither path re-commits back to the processor. */
export function ModRoutingProvider({ children }) {
  const [assignments, setAssignments] = useState([]);
  const assignmentsRef = useRef(assignments);
  // Which knob a drop just landed on, so that knob's own ModdableKnob can
  // open its depth popover immediately rather than making the user find and
  // click the new badge - cleared the instant that knob has read it (see
  // clearJustAssigned), so it never reopens on some later, unrelated render.
  const [justAssignedParamId, setJustAssignedParamId] = useState(null);

  const commit = useCallback((next) => {
    assignmentsRef.current = next;
    setAssignments(next);
    lfoRoutingSet(toRoutingJson(next));
  }, []);

  useEffect(() => {
    let live = true;

    lfoRoutingGet().then((json) => {
      if (!live) return;
      const parsed = parseRoutingJson(json);
      if (parsed) {
        assignmentsRef.current = parsed;
        setAssignments(parsed);
      }
    });

    const handle = window.__JUCE__?.backend?.addEventListener("lfoRouting", (json) => {
      const parsed = parseRoutingJson(json);
      if (parsed) {
        assignmentsRef.current = parsed;
        setAssignments(parsed);
      }
    });

    return () => {
      live = false;
      if (handle) window.__JUCE__.backend.removeEventListener(handle);
    };
  }, []);

  // Called on a knob drop - adds a new assignment at a sensible starting
  // depth, or does nothing if that knob already has one (drop again to
  // change it isn't a thing; use the badge's depth control instead) or the
  // 8-slot cap is already full.
  const assign = useCallback(
    (paramId) => {
      const current = assignmentsRef.current;
      if (current.some((a) => a.paramId === paramId)) return;
      if (current.length >= MAX_ASSIGNMENTS) return;
      commit([...current, { paramId, depth: DEFAULT_DEPTH }]);
      setJustAssignedParamId(paramId);
    },
    [commit],
  );

  const clearJustAssigned = useCallback(() => setJustAssignedParamId(null), []);

  const setDepth = useCallback(
    (paramId, depth) => {
      // Unipolar: 0 (no effect) to 1 (the LFO's full swing) - no inversion.
      const clamped = Math.max(0, Math.min(1, depth));
      commit(assignmentsRef.current.map((a) => (a.paramId === paramId ? { ...a, depth: clamped } : a)));
    },
    [commit],
  );

  const remove = useCallback(
    (paramId) => {
      commit(assignmentsRef.current.filter((a) => a.paramId !== paramId));
    },
    [commit],
  );

  const value = { assignments, assign, setDepth, remove, justAssignedParamId, clearJustAssigned };

  return <ModRoutingContext.Provider value={value}>{children}</ModRoutingContext.Provider>;
}

/** `undefined` when parameterId has no assignment, otherwise `{paramId, depth}`. */
export function useModAssignment(parameterId) {
  const ctx = useContext(ModRoutingContext);
  return ctx?.assignments.find((a) => a.paramId === parameterId);
}

export function useModRouting() {
  return useContext(ModRoutingContext);
}
