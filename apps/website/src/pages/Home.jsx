import { Link } from 'react-router-dom';
import AbPlayer from '../components/AbPlayer.jsx';
import ChainReorder from '../components/ChainReorder.jsx';
import ModuleExplorer from '../components/ModuleExplorer.jsx';
import { ALPINE_PRICE, GRAINS_PRICE, MODULE_PRICE, SPEC_CHIPS } from '../data.js';

function ProductCard({ to, name, img, blurb, bullets, price, grow }) {
  return (
    <div className="bb-card" style={{ flex: grow, minWidth: 300, padding: 40 }}>
      <h3 style={{ fontSize: 26, marginBottom: 8 }}>{name}</h3>
      <img src={img} alt={name} style={{ width: '100%', borderRadius: 12, margin: '16px 0 24px' }} />
      <p className="bb-body" style={{ marginBottom: 20 }}>
        {blurb}
      </p>
      <ul className="bb-list" style={{ marginBottom: 28 }}>
        {bullets.map((b) => (
          <li key={b}>— {b}</li>
        ))}
      </ul>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
        <span className="bb-price">${price}</span>
        <Link to={to} className="bb-textlink">
          Learn more
        </Link>
      </div>
    </div>
  );
}

export default function Home() {
  return (
    <>
      <section className="bb-wrap bb-hero">
        <div className="bb-hero__blob" />
        <div className="bb-hero__col" style={{ flex: 1, minWidth: 320 }}>
          <div className="bb-eyebrow" style={{ marginBottom: 18 }}>
            Two effects. Eleven engines.
          </div>
          <h1 className="bb-h1" style={{ marginBottom: 24 }}>
            Colour,
            <br />
            not correction.
          </h1>
          <p className="bb-lede" style={{ maxWidth: 460, marginBottom: 36 }}>
            Two studio-grade effects for people who want their tracks to sound like something
            happened to them.
          </p>
          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
            <a href="#ab-player" className="bb-btn bb-btn--filled">
              Hear it
            </a>
            <Link to="/alpine" className="bb-btn bb-btn--ghost">
              Explore the plugins
            </Link>
          </div>
        </div>
        <div className="bb-hero__col" style={{ flex: 1.2, minWidth: 340 }}>
          <img
            src="/assets/alpine-full.png"
            alt="BitBit Alpine — four-module signal chain"
            className="bb-shot"
          />
        </div>
      </section>

      <AbPlayer />

      <section
        className="bb-wrap"
        style={{ paddingBottom: 140, display: 'flex', gap: 28, flexWrap: 'wrap' }}
      >
        <ProductCard
          to="/alpine"
          grow={1.3}
          name="BitBit Alpine"
          img="/assets/alpine-full.png"
          price={ALPINE_PRICE}
          blurb="Four drag-to-reorder modules, eleven engines, one window."
          bullets={[
            'Artifact, Modulation, Delay and Reverb, reordered live',
            'Every module keeps its own power, trim and Mix',
            'Engines stay warm while deselected — switching never clicks',
          ]}
        />
        <ProductCard
          to="/grains"
          grow={1}
          name="BitBit Grains"
          img="/assets/grain-mod-tab.png"
          price={GRAINS_PRICE}
          blurb="A granular delay into a plate. Freeze it, scrub it, keep it in key."
          bullets={[
            'Up to 32 overlapping grains, each its own pitch and pan',
            'Scale-aware pitch keeps the cloud in key',
            'A hand-drawn breakpoint LFO editor',
          ]}
        />
      </section>

      <ChainReorder />

      <ModuleExplorer />

      <section id="pricing" className="bb-band" style={{ padding: '100px 0' }}>
        <div className="bb-wrap">
          <div className="bb-eyebrow" style={{ marginBottom: 8 }}>
            Pricing
          </div>
          <h2 className="bb-h2" style={{ marginBottom: 32 }}>
            Buy one, or buy the host.
          </h2>
          <div style={{ display: 'flex', gap: 20, flexWrap: 'wrap', marginBottom: 24 }}>
            <div className="bb-well" style={{ flex: 1, minWidth: 220, padding: 28 }}>
              <div style={{ fontSize: 13, color: 'var(--bb-ink-muted)', marginBottom: 10 }}>
                Single module
              </div>
              <div className="bb-price" style={{ fontSize: 30 }}>
                ${MODULE_PRICE}
              </div>
            </div>
            <div
              className="bb-well"
              style={{ flex: 1, minWidth: 220, padding: 28, border: '1px solid var(--bb-grains)' }}
            >
              <div style={{ fontSize: 13, color: 'var(--bb-ink-muted)', marginBottom: 10 }}>
                BitBit Grains
              </div>
              <div className="bb-price" style={{ fontSize: 30 }}>
                ${GRAINS_PRICE}
              </div>
            </div>
            <div
              className="bb-well"
              style={{ flex: 1, minWidth: 220, padding: 28, border: '1px solid var(--bb-ink)' }}
            >
              <div style={{ fontSize: 13, color: 'var(--bb-ink-muted)', marginBottom: 10 }}>
                BitBit Alpine · Best value
              </div>
              <div className="bb-price" style={{ fontSize: 30 }}>
                ${ALPINE_PRICE}
              </div>
            </div>
          </div>
          <p className="bb-body" style={{ marginBottom: 32 }}>
            ${MODULE_PRICE} × 4 modules = $76. Alpine is ${ALPINE_PRICE} and adds the host,
            drag-to-reorder chain, per-module trims and global presets.
          </p>
          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
            <Link to="/alpine#buy" className="bb-btn bb-btn--filled bb-btn--sm">
              Buy Alpine
            </Link>
            <Link to="/grains#buy" className="bb-btn bb-btn--ghost bb-btn--sm">
              Buy Grains
            </Link>
          </div>
          <div
            className="bb-chips"
            style={{
              gap: 24,
              marginTop: 36,
              paddingTop: 28,
              borderTop: '1px solid var(--bb-border)',
              fontSize: 12,
              letterSpacing: '0.06em',
              color: 'var(--bb-ink-faint)',
            }}
          >
            {SPEC_CHIPS.map((chip) => (
              <span key={chip}>{chip}</span>
            ))}
          </div>
        </div>
      </section>
    </>
  );
}
