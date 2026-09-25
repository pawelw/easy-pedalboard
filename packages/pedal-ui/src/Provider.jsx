import { createContext, useContext, useMemo, useState } from "react";
import { MantineProvider } from "@mantine/core";

const PedalThemeContext = createContext({ theme: "light", setTheme: () => {} });

/** The palette name in force. For the few choices a theme can't express as a
    token - a component swapping for a different one rather than recolouring -
    not for anything a `[data-pui-theme]` block could do in CSS. */
export function usePedalTheme() {
  return useContext(PedalThemeContext).theme;
}

/** Switches the palette for the whole tree under the provider. */
export function useSetPedalTheme() {
  return useContext(PedalThemeContext).setTheme;
}

/**
 * Wrap a pedal's app in this once, at the root. The rack-module look never
 * follows the host DAW's own theme - hardware doesn't do dark mode by OS
 * setting - so the colour scheme is pinned by `theme`, a prop the plugin
 * hardcodes, rather than left to `prefers-color-scheme`.
 *
 * `theme` picks the pedal's palette (see tokens.css). "light" (default) is
 * the original rack-module cream/black look every pedal had before a second
 * theme existed, and stamps no attribute at all, so a pedal that never
 * passes `theme` renders off the bare `:root` values, byte-identical to
 * before this prop existed. "onyx" is the dark face, "grey" the cool
 * near-white soft-UI one. Anything but "light" stamps `data-pui-theme` on a
 * wrapper div, which every token-driven component inherits from - an unknown
 * name stamps too and simply finds no block, leaving the default palette.
 *
 * The prop is the *starting* palette: `useSetPedalTheme` can move it at
 * runtime, which is what the header's theme switch does. A pedal that never
 * calls it stays on whatever it was given.
 */
export default function PedalUIProvider({ theme = "light", children }) {
  const [current, setCurrent] = useState(theme);
  const value = useMemo(() => ({ theme: current, setTheme: setCurrent }), [current]);

  return (
    <MantineProvider forceColorScheme="light">
      <PedalThemeContext.Provider value={value}>
        <div data-pui-theme={current === "light" ? undefined : current} style={{ background: "var(--pui-page)" }}>
          {children}
        </div>
      </PedalThemeContext.Provider>
    </MantineProvider>
  );
}
