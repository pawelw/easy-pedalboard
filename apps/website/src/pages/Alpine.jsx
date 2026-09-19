import AbPlayer from '../components/AbPlayer.jsx';
import ModuleExplorer from '../components/ModuleExplorer.jsx';
import ModuleDetail from '../components/ModuleDetail.jsx';
import BuyCta from '../components/BuyCta.jsx';
import { ALPINE_PRICE, MODULE_PRICE, MODULES, SPECS } from '../data.js';

export default function Alpine() {
  return (
    <>
      <section className="bb-wrap bb-producthero">
        <div className="bb-hero__blob" />
        <div style={{ position: 'relative', zIndex: 1 }}>
          <div className="bb-eyebrow">Four modules · eleven engines · one window</div>
          <h1 className="bb-h1" style={{ marginBottom: 24 }}>
            BitBit Alpine
          </h1>
          <p className="bb-lede" style={{ margin: '0 auto' }}>
            A chain host that stops pretending the order was decided for you. Artifact,
            Modulation, Delay and Reverb, each with its own power, trim and Mix — dragged into
            whatever sequence the track needs, live, without a click.
          </p>
          <div className="bb-producthero__meta">
            <a href="#buy" className="bb-btn bb-btn--filled">
              Buy Alpine · ${ALPINE_PRICE}
            </a>
            <a href="#ab-player" className="bb-btn bb-btn--ghost">
              Hear it
            </a>
          </div>
          <div
            style={{
              height: 4,
              borderRadius: 4,
              background: 'linear-gradient(90deg,#c00001,#e0b23c,#a3ce7a,#7fd2d8)',
              margin: '56px auto 24px',
              maxWidth: 1100,
            }}
          />
          <img
            src="/assets/alpine-full.png"
            alt="BitBit Alpine, the full four-module window"
            className="bb-shot"
            style={{ maxWidth: 1100, margin: '0 auto' }}
          />
        </div>
      </section>

      <AbPlayer
        eyebrow="Hear it"
        title="The same take, dry and through Alpine."
      />

      <ModuleExplorer title="Eleven engines. One window." />

      <section className="bb-wrap" style={{ paddingBottom: 40 }}>
        <div className="bb-eyebrow">Module by module</div>
        <h2 className="bb-h2" style={{ maxWidth: 760 }}>
          Every module is a plugin in its own right — ${MODULE_PRICE} on its own, or all four
          inside the host.
        </h2>
      </section>

      <div className="bb-wrap">
        {MODULES.map((mod, i) => (
          <ModuleDetail key={mod.key} module={mod} flip={i % 2 === 1} />
        ))}
      </div>

      <section className="bb-wrap" style={{ paddingTop: 80, paddingBottom: 120 }}>
        <div className="bb-eyebrow" style={{ marginBottom: 24 }}>
          Specification
        </div>
        <div className="bb-spec">
          {SPECS.map(([label, value]) => (
            <div key={label} className="bb-spec__row">
              <span>{label}</span>
              <span>{value}</span>
            </div>
          ))}
        </div>
      </section>

      <BuyCta
        title="All eleven engines, in the order you want them."
        blurb={`Four modules at $${MODULE_PRICE} each is $76. Alpine is $${ALPINE_PRICE} and adds the host, the drag-to-reorder chain, per-module trims and global presets across the whole chain.`}
        price={ALPINE_PRICE}
        cta="Buy BitBit Alpine"
      />
    </>
  );
}
