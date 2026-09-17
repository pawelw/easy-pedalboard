import { createContext, useContext, useRef, useSyncExternalStore } from "react";

const LfoPlaybackContext = createContext(null);

/** The LFO's live output (-1..1), broadcast from LfoEditor's own playhead
    rAF loop (it already computes this every frame for the graph's playhead
    dot - see its own note) so any ModdableKnob across the face can read the
    current value without LfoEditor needing to know knobs exist, and without
    a second phase-tracking loop anywhere else.

    A subscriber ref + useSyncExternalStore rather than useState/context
    value: this changes every animation frame, and updating it through
    ordinary state would re-render this Provider (and everything under it)
    sixty times a second. Kept in a ref instead, so only components that
    actually call useLfoValue() re-render on a tick - and only assigned
    knobs do that (see ModdableKnob.jsx). */
export function LfoPlaybackProvider({ children }) {
  const store = useRef(null);
  if (!store.current) {
    let value = 0;
    const listeners = new Set();
    store.current = {
      subscribe: (listener) => {
        listeners.add(listener);
        return () => listeners.delete(listener);
      },
      getSnapshot: () => value,
      set: (next) => {
        value = next;
        listeners.forEach((listener) => listener());
      },
    };
  }

  return <LfoPlaybackContext.Provider value={store.current}>{children}</LfoPlaybackContext.Provider>;
}

/** The LFO's current output, -1..1, live-updating at animation-frame rate.
    Outside a LfoPlaybackProvider this just reads 0 and never updates. */
export function useLfoValue() {
  const store = useContext(LfoPlaybackContext);
  return useSyncExternalStore(
    store?.subscribe ?? (() => () => {}),
    store?.getSnapshot ?? (() => 0),
  );
}

export function useSetLfoValue() {
  return useContext(LfoPlaybackContext)?.set;
}
