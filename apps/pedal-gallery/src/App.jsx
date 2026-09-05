import { useEffect, useState } from "react";
import { pedals } from "./pedals.js";
import Components from "./Components.jsx";
import "./index.css";

function useHashSlug() {
  const [slug, setSlug] = useState(() => window.location.hash.slice(1));
  useEffect(() => {
    const onHashChange = () => setSlug(window.location.hash.slice(1));
    window.addEventListener("hashchange", onHashChange);
    return () => window.removeEventListener("hashchange", onHashChange);
  }, []);

  // Every route used to be shorter than one screen (html/body were
  // `overflow: hidden` - see plugins/*/jsui/src/index.css), so a leftover
  // scroll position from the previous route was never visible. Now that a
  // route (Components) can be taller than the viewport, switching away from
  // a scrolled-down one needs an explicit reset - the browser doesn't do
  // this on its own for a hash-only navigation within one page.
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [slug]);

  return slug;
}

function Home() {
  return (
    <div className="gallery">
      <header className="gallery__header">
        <h1>Synth Peak — Pedal Gallery</h1>
        <p>Dev-only browser preview of each pedal's WebView face. Not the real plugin - no audio, no host.</p>
      </header>

      <div className="gallery__grid">
        <a key="components" href="#components" className="gallery__tile">
          <span className="gallery__tile-name">pedal-ui components</span>
          <span className="gallery__tile-status">Light / onyx showcase</span>
        </a>

        {pedals.map((pedal) => (
          <a
            key={pedal.slug}
            href={pedal.face ? `#${pedal.slug}` : undefined}
            className={"gallery__tile" + (pedal.face ? "" : " gallery__tile--disabled")}
            aria-disabled={!pedal.face}
          >
            <span className="gallery__tile-name">{pedal.name}</span>
            <span className="gallery__tile-status">{pedal.face ? "View face" : "No WebView face yet"}</span>
          </a>
        ))}
      </div>
    </div>
  );
}

function PedalView({ pedal }) {
  const Face = pedal.face;
  return (
    <div>
      <a href="#" className="gallery__back">
        ← All pedals
      </a>
      <Face />
    </div>
  );
}

export default function App() {
  const slug = useHashSlug();
  if (slug === "components") return <Components />;

  const pedal = pedals.find((p) => p.slug === slug && p.face);
  return pedal ? <PedalView pedal={pedal} /> : <Home />;
}
