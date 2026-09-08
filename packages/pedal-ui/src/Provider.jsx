import { MantineProvider } from "@mantine/core";

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
 */
export default function PedalUIProvider({ theme = "light", children }) {
  return (
    <MantineProvider forceColorScheme="light">
      <div data-pui-theme={theme === "light" ? undefined : theme} style={{ background: "var(--pui-page)" }}>
        {children}
      </div>
    </MantineProvider>
  );
}
