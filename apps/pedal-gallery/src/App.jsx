import { useEffect, useState } from "react";
import { pedals } from "./pedals.js";
import "./index.css";

function useHashSlug() {
  const [slug, setSlug] = useState(() => window.location.hash.slice(1));
  useEffect(() => {
    const onHashChange = () => setSlug(window.location.hash.slice(1));
    window.addEventListener("hashchange", onHashChange);
    return () => window.removeEventListener("hashchange", onHashChange);
  }, []);
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
  const pedal = pedals.find((p) => p.slug === slug && p.face);

  return pedal ? <PedalView pedal={pedal} /> : <Home />;
}
