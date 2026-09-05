import * as Juce from "juce-framework-frontend";

// The plugin's window has been sized by hand twice now from a Chromium
// measurement of the panel, and both times Ableton's real WKWebView rendered
// it taller than that number - font/line-height metrics differ enough
// between the two engines that there's no way to get this right by guessing
// from outside a real host. This has the page measure its own actual
// rendered size and tell PeakWahWebEditor to match it exactly, so it's never
// a guess again regardless of engine, font-loading timing, or future
// content changes.
const PAGE_PADDING = 4; // .page's own padding, on every side - see index.css

export function installAutoResize() {
  if (typeof window.__JUCE__?.initialisationData?.__juce__functions?.includes !== "function") return;
  if (!window.__JUCE__.initialisationData.__juce__functions.includes("reportContentSize")) return;

  const reportContentSize = Juce.getNativeFunction("reportContentSize");

  const report = () => {
    const card = document.querySelector(".pui-card");
    if (!card) return;
    const rect = card.getBoundingClientRect();
    reportContentSize(Math.ceil(rect.width) + PAGE_PADDING * 2, Math.ceil(rect.height) + PAGE_PADDING * 2);
  };

  const card = document.querySelector(".pui-card");
  if (!card) return;

  // Fires once immediately on observe() as well as on every subsequent
  // layout change - covers late web-font swaps, not just the first paint.
  const observer = new ResizeObserver(report);
  observer.observe(card);
}
