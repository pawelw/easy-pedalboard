import { MantineProvider } from "@mantine/core";

/**
 * Wrap a pedal's app in this once, at the root. The rack-module look is
 * always light/cream regardless of the host DAW's theme - hardware doesn't
 * follow dark mode - so the colour scheme is pinned rather than left to
 * `prefers-color-scheme`.
 */
export default function PedalUIProvider({ children }) {
  return <MantineProvider forceColorScheme="light">{children}</MantineProvider>;
}
