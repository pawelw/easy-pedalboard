import AbPlayer from '../components/AbPlayer.jsx';
import FeatureDetail from '../components/FeatureDetail.jsx';
import BuyCta from '../components/BuyCta.jsx';
import {
  GRAINS_EFFECTS,
  GRAINS_HIGHLIGHTS,
  GRAINS_LFO,
  GRAINS_PRESETS,
  GRAINS_PRICE,
  GRAINS_SECTIONS,
  SPECS,
} from '../data.js';

export default function Grains() {
  return (
    <>
      <section className="bb-wrap bb-producthero">
        <div className="bb-hero__blob" style={{ background: 'radial-gradient(circle,#b39bd822,transparent 70%)' }} />
        <div style={{ position: 'relative', zIndex: 1 }}>
          <div className="bb-eyebrow">A granular delay into a plate</div>
          <h1 className="bb-h1" style={{ marginBottom: 24 }}>
            BitBit Grains
          </h1>
          <p className="bb-lede" style={{ margin: '0 auto' }}>
            Up to thirty-two overlapping grains, each with its own pitch, direction and place in
            the image. Freeze the buffer and scrub it, or leave it live and let it echo. A scale
            block keeps the cloud in key with what you are playing.
          </p>
          <div className="bb-producthero__meta">
            <a href="#buy" className="bb-btn bb-btn--filled" style={{ background: 'var(--bb-grains)' }}>
              Buy Grains · ${GRAINS_PRICE}
            </a>
            <a href="#ab-player" className="bb-btn bb-btn--ghost">
              Hear it
            </a>
          </div>
          <div
            style={{
              height: 4,
              borderRadius: 4,
              background: 'linear-gradient(90deg,#b39bd8,#7fd2d8,#a3ce7a)',
              margin: '56px auto 24px',
              maxWidth: 1100,
            }}
          />
          <img
            src="/assets/grain-mod-tab.png"
            alt="BitBit Grains, the full window"
            className="bb-shot"
            style={{ maxWidth: 1100, margin: '0 auto' }}
          />
        </div>
      </section>

      <AbPlayer
        eyebrow="Hear it"
        title="The same take, dry and granulated."
        presets={GRAINS_PRESETS}
      />

      <section className="bb-wrap" style={{ paddingBottom: 120 }}>
        <div className="bb-modgrid">
          {GRAINS_HIGHLIGHTS.map(([title, blurb]) => (
            <div key={title} className="bb-well" style={{ padding: 24 }}>
              <div style={{ fontSize: 16, color: 'var(--bb-ink-bright)', marginBottom: 8 }}>
                {title}
              </div>
              <p className="bb-body" style={{ fontSize: 14 }}>
                {blurb}
              </p>
            </div>
          ))}
        </div>
      </section>

      <section className="bb-wrap" style={{ paddingBottom: 40 }}>
        <div className="bb-eyebrow">Section by section</div>
        <h2 className="bb-h2" style={{ maxWidth: 760 }}>
          Three sections decide what the cloud is made of.
        </h2>
      </section>

      <div className="bb-wrap">
        {GRAINS_SECTIONS.map((section, i) => (
          <FeatureDetail key={section.key} section={section} flip={i % 2 === 1} />
        ))}
      </div>

      <section className="bb-wrap" style={{ paddingTop: 100, paddingBottom: 40 }}>
        <div className="bb-eyebrow">Behind the cloud</div>
        <h2 className="bb-h2" style={{ maxWidth: 760 }}>
          The effects, and the LFO that moves them.
        </h2>
      </section>

      <div className="bb-wrap">
        <FeatureDetail section={GRAINS_EFFECTS} />
        <FeatureDetail section={GRAINS_LFO} flip />
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
        title="Freeze it, scrub it, keep it in key."
        blurb="One granular engine, a plate behind it and a drawn LFO over the top. Mono or stereo in, stereo out, on every format."
        price={GRAINS_PRICE}
        cta="Buy BitBit Grains"
        accent="var(--bb-grains)"
      />
    </>
  );
}
