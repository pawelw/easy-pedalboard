# BitBit Audio — website design brief / AI design-tool prompt

Everything below is pulled from the real code in this repo (theme tokens, engine
lists, parameter names, presets). Paste **Part A** into Google Stitch / Claude
Design / v0 / Lovable. **Part B** is the content inventory the tool needs so it
stops inventing feature names. **Part C** is a short version for tools with a
small input box.

**Part D** describes the plugin faces themselves — their anatomy and their icon
vocabulary — so the site can borrow the product's own visual language instead of
inventing a generic one.

## Naming

The code says Peak; the brand is BitBit. The map:

| In the code | On the website |
|---|---|
| Peak Alpine | **BitBit Alpine** — $99 |
| Peak Grain | **BitBit Grains** — $49 |
| Peak Artifact | **BitBit Artifact** — $19 |
| Peak Modulation (face label: MOD) | **BitBit Modulation** — $19 |
| Peak Delay | **BitBit Delay** — $19 |
| Peak Reverb | **BitBit Reverb** — $19 |

The mountain-range mark in each plugin's top-left corner is already the logo —
reuse it as the BitBit Audio wordmark lockup rather than commissioning a new one.

## Attach these reference images

The screenshots are the most useful thing you can give the tool — the site has to
look like it was designed by whoever designed these plugins. Send, at minimum:

1. **Alpine, full window** — the four-module row (Artifact red / Mod amber /
   Delay green / Reverb cyan).
2. **Alpine again with Mod and Artifact swapped** — this one proves the
   drag-to-reorder story in a single glance; it is the evidence behind the
   interactive chain builder.
3. **Grains, Effects tab** and **Grains, Mod tab** — the second shows the green
   breakpoint LFO editor, which nothing else in the range has.
4. **The narrow standalone module windows** — one per engine if you have them
   (Artifact: Amp / Ring / Crasher / Rust; Mod: Tape / Trem / Chorus / Phaser /
   Filter; Reverb: Space / Spring). These are the engine explorer's source art.
5. **Delay standalone** — the only wide module, with its tap display.

---

# PART A — the prompt

> ## Role
>
> You are a senior product designer and front-end engineer designing the
> flagship marketing website for **BitBit Audio**, a boutique audio-software
> company that makes studio-grade VST3 / AU / AAX effect plugins. Produce a
> complete, production-quality multi-page website design plus responsive
> front-end code.
>
> ## The company
>
> BitBit Audio makes effects that *add life and colour* to a sound. The house
> position is "character, not correction" — these are not EQs and compressors,
> they are the plugins you put on a sterile DI guitar, a flat synth or a dry
> vocal to make it sound like a record. Every plugin is low-latency and stable
> enough to play live through, not just to render offline.
>
> Tone of voice: confident, technical, understated. Engineer-to-engineer. No
> exclamation marks, no "revolutionary", no "game-changing", no stock-photo
> producers in headphones. Short declarative sentences. The plugins' own UI is
> quiet and precise; the copy must match it.
>
> ## The product line
>
> **1. BitBit Alpine — $99.** The flagship multi-effect. Four drag-to-reorder
> modules in one window: **Artifact**, **Modulation**, **Delay**, **Reverb**.
> Eleven switchable engines across them, plus three delay routings. Drag a
> module by its header to change the signal order; every permutation of the
> four is a different sound. Each module has its own power toggle, level trim
> and Mix. Every engine keeps running while another is selected, so switching
> engines never clicks and never loses your settings.
>
> **2. BitBit Grains — $49.** Tagline, use verbatim: **"Shatter a sound into
> grains. Freeze it, scrub it, keep it in key."** — the three verbs are three
> real controls (Freeze, Stretch, Scale), so the product page can hang a section
> on each.
>
> A granular delay into a plate reverb. It records
> into a 10.5-second buffer and sprays windowed grains out of it — each one at a
> random pitch, direction, and pan position — up to 32 overlapping at once.
> Freeze the buffer and scrub through it with Stretch. A scale-aware pitch
> section means the cloud lands in key with what you are playing instead of
> fighting it. It also has a drag-and-drop modulation LFO with a breakpoint
> editor — drop the MOD chip on almost any knob and draw the shape by hand.
>
> **3. The four Alpine modules, sold individually — $19 each.** Exactly the same
> DSP and the same face as inside Alpine, each as a standalone plugin:
> **BitBit Artifact**, **BitBit Modulation**, **BitBit Delay**, **BitBit
> Reverb**. This is a first-class part of the offer, not a footnote: the
> pricing page must make "buy one for $19, or all four plus the host and the
> drag-to-reorder chain for $99" an obvious, legible decision. Show the maths
> ($19 × 4 = $76 of modules; Alpine is $99 and adds the host, reordering,
> per-module trims and global presets).
>
> ## Design references
>
> Study these three and synthesise, do not copy:
>
> - **excite-audio.com/lifeline/lifeline-expanse** — take: the dark cinematic
>   hero, the enormous product render as the page's centre of gravity, the
>   restraint.
> - **xlnaudio.com/products/addictive_fx/effect/rc-20_retro_color** — take: the
>   module/engine breakdown, the way each character module gets its own named
>   block with an icon and one-line description, and the audio A/B demos.
> - **babyaud.io/transit** — take: the playfulness in the interaction layer, the
>   crisp typographic rhythm, the immediacy of "press play and hear it".
>
> The result should sit closest to Lifeline Expanse in mood: dark, spacious,
> photographic. Baby Audio's energy shows up in the interactive bits, not the
> palette.
>
> ## Visual system — use these exact values
>
> The site inherits the plugins' own design tokens so the page and the product
> are visibly one object.
>
> **Palette (dark, and dark is the only mode):**
>
> | Token | Hex | Use |
> |---|---|---|
> | `--bb-page` | `#0f1315` | Page background, near-black blue-green |
> | `--bb-panel` | `#171d20` | Cards, panels |
> | `--bb-panel-edge` | `#222b2e` | Card borders, dividers |
> | `--bb-module` | `#20292d` | Raised module chrome |
> | `--bb-recess` | `#101416` | Insets, code blocks, players |
> | `--bb-ink` | `#b9d3d9` | Primary text — pale blue-grey, never pure white |
> | `--bb-ink-soft` | `#8ba3a9` | Secondary text |
> | `--bb-ink-dim` | `#6c8288` | Captions, metadata |
>
> **Module accent colours — these are the product's real identity colours and
> must be used consistently everywhere that module appears (nav, cards, engine
> chips, pricing, the chain diagram):**
>
> | Module | Hex | Character |
> |---|---|---|
> | Artifact | `#c00001` | Red — destruction, bit-crush, rust |
> | Modulation | `#e0b23c` | Amber — tape, movement, warmth |
> | Delay | `#a3ce7a` | Green — repeats, space in time |
> | Reverb | `#7fd2d8` | Cyan — room, tail, air |
>
> **BitBit Grains' section accents:** the Grain, Pitch and Random sections all
> share a warm amber-tan `#dfa878` for their knob arcs and displays; the Delay
> and Reverb sections are the same cyan `#7fd2d8` as Alpine's Reverb module; the
> modulation LFO editor is green `#3ddc84`; the Drive knob is red `#c00001`.
> Grains' overall brand accent on the website is violet `#b39bd8`.
>
> Accents are for 1–3 % of the pixels: a hairline, an icon, a knob arc, a
> hovered border, a chart stroke. Never a filled button the size of a postcard,
> never a gradient wash. The page is 97 % greys. This is exactly how the plugins
> themselves work — look at the screenshots: a whole module is grey except for
> its engine icon, its knob arcs and its power ring.
>
> **Type:** `Space Grotesk` for everything — it is the plugins' own typeface.
> Headings 300/400 weight, large and tight (`-0.02em`). Labels and eyebrows
> uppercase, 11–12px, `letter-spacing: 0.14em`, in `--bb-ink-dim`. Body 16–17px
> at 1.65 line height in `--bb-ink-soft`. Numeric readouts (prices, ms values,
> spec figures) tabular-nums.
>
> **Surfaces:** `border-radius: 20px` on cards and panels; `999px` on pills and
> buttons. No panel grain or noise texture. Shadows do the edge work, not
> borders: `0 20px 44px rgba(0,0,0,0.4)` for a lifted panel, `inset 0 2px 6px
> rgba(0,0,0,0.6)` for anything recessed (audio players, spec tables, code).
> Where a border is needed it is 1px of `--bb-panel-edge`, never lighter.
>
> **Layout:** 1280px max content width, 24px gutters, an 8px base grid, generous
> vertical rhythm (120–160px between major sections on desktop). Sections
> alternate between full-bleed dark and a slightly lifted `--bb-panel` band.
>
> **Motion:** 180–240ms, `cubic-bezier(0.2, 0, 0, 1)`. Fade-and-rise on scroll
> at 16px of travel, once, never repeating. Knob arcs and waveform strokes may
> draw in on first view. Respect `prefers-reduced-motion` — under it, nothing
> moves and everything is immediately in its final state.
>
> ## Sitemap
>
> 1. `/` — Home (brand + both products + the modules)
> 2. `/alpine` — BitBit Alpine product page
> 3. `/grains` — BitBit Grains product page
> 4. `/modules` — the four modules as standalone plugins, with `/modules/artifact`,
>    `/modules/modulation`, `/modules/delay`, `/modules/reverb`
> 5. `/pricing` — the full matrix, including the bundle maths
> 6. `/demos` — every audio example in one place, filterable by source and preset
> 7. `/support` — install, system requirements, formats, FAQ, manual links
> 8. `/about` — short. Who makes it, why it exists, how to get in touch
> 9. Legal: `/terms`, `/privacy`, `/refunds`
>
> ## Home page — section by section
>
> 1. **Nav (sticky, glass over `--bb-page` at 80 % with a 12px blur).** BitBit
>    BitBit Audio wordmark left; Alpine · Grains · Modules · Demos · Support
>    centre; "Buy" pill right. On scroll past 80px the bar tightens to 56px tall
>    and gains a 1px `--bb-panel-edge` bottom rule.
>
> 2. **Hero.** Full-viewport, dark. Headline on the left, large: *"Colour, not
>    correction."* with a sub-line: *"Two studio-grade effects for people who
>    want their tracks to sound like something happened to them."* Two buttons:
>    a solid "Hear it" (scrolls to the A/B player) and a ghost-outline "Explore
>    the plugins". On the right, or spanning behind at low opacity, a large
>    perspective render of the Alpine window — the four-module row, with its
>    red / amber / green / cyan accents legible. A very slow parallax and a
>    faint radial vignette behind the render. **Leave a labelled image slot**
>    (`hero-alpine-render.png`, 2400×1400) — do not generate fake plugin art;
>    reference the attached screenshots for what goes there.
>
> 3. **The A/B player — the most important module on the site.** A wide
>    recessed panel. Left: a source selector (Dry Guitar, Clean Rhythm, Synth
>    Pad, Drum Loop — build it data-driven so sources can be added). Right: a
>    row of preset chips, each tinted with its module's accent. Centre: one
>    large circular play/pause button and a waveform. The **A/B toggle is the
>    core interaction — a single control that crossfades between the dry take
>    and the processed take while playing, without restarting playback**, so the
>    listener hears the difference in place. Show the currently-playing preset
>    name and which engine it uses. Include a tiny "loop" toggle and a scrub
>    bar. Only one player on the page may play at a time. Placeholder audio:
>    `/audio/{source}-dry.mp3` and `/audio/{source}-{preset}-wet.mp3`.
>
> 4. **The two products, side by side.** Two large cards, Alpine (accent: use
>    its module colours as a four-stop hairline across the card's top edge) and
>    Grains (violet `#b39bd8`). Each: product shot, one-line positioning,
>    3 bullet capabilities, price, and a "Learn more" link. Alpine's card is
>    visually dominant — wider or taller.
>
> 5. **The Alpine chain — an interactive diagram.** Four module blocks in a row
>    in their accent colours, connected by a signal path. **Let the visitor drag
>    the blocks to reorder them**, exactly as the plugin does; as the order
>    changes, an annotation line updates ("Reverb before Artifact: crush the
>    tail, not the note"). This is the single most memorable thing on the page —
>    the site should do the thing the plugin does.
>
> 6. **Engine explorer.** A tabbed / accordion grid presenting **every engine of
>    every module** (the full list is in the content inventory below). Each
>    engine gets: its name, a one-line description, its actual control names as
>    small pills, **its own line-art icon redrawn from the plugin's engine
>    stepper** (Part D lists them), and its own A/B audio example. Grouped under
>    the four module headings, each heading in its accent colour. This section is
>    a large part of the page's value — it is what convinces someone that $99 is
>    a lot of plugin.
>
> 7. **Video.** A single 16:9 YouTube embed, lazy-loaded behind a custom
>    poster frame with a centred play button (do not load the YouTube iframe
>    until clicked — privacy and performance). Caption: "Alpine and Grains — the
>    presets worth hearing first · 1:00". Around it, three short text callouts
>    pointing at timestamps.
>
> 8. **Presets.** Show that the plugins ship with a real factory library,
>    organised into categories (Digital, Ping Pong, Modulated, Ambient,
>    Strings, Texture). Render a few named presets as chips; each chip plays its
>    audio example in the A/B player above.
>
> 9. **Pricing strip.** Three tiers: single module $19, BitBit Grains $49,
>    BitBit Alpine $99 (marked "Best value" with a subtle accent hairline, not a
>    loud badge). One line under the row: "$19 × 4 modules = $76. Alpine is $99
>    and adds the host, drag-to-reorder chain, per-module trims and global
>    presets." Underneath, a compact spec row: VST3 · AU · AAX · macOS 11+
>    (Universal, Apple Silicon native) · Windows 10+ 64-bit · 14-day refund ·
>    free updates.
>
> 10. **Footer.** Four columns (Products, Modules, Resources, Company), the
>     wordmark, a newsletter field, and social icons. Dark, quiet, no accent.
>
> ## Product page template (`/alpine`, `/grains`)
>
> Hero with the full-size face render → the A/B player scoped to that product →
> a feature deep-dive alternating image/text rows → the full engine or section
> breakdown → an annotated interface tour (numbered hotspots over the face
> screenshot, each expanding to explain a control) → full specification table
> (formats, latency, sample rates, CPU, presets, requirements) → preset library
> → the video → FAQ accordion → buy CTA.
>
> ## Module page template (`/modules/*`)
>
> Narrower, faster: the module's face render at actual scale, its accent colour
> owning the page, its engines listed with audio, a "this is the same module
> that ships inside Alpine" cross-sell block with the upgrade maths, $19 buy
> button.
>
> ## Components to design explicitly
>
> - `ABPlayer` — as specified above. Design its loading, playing, paused,
>   crossfading and error states.
> - `EngineCard` — accent-tinted, with control-name pills and an inline play.
> - `ChainBuilder` — the draggable four-module signal path, with keyboard
>   reordering as well as pointer drag.
> - `PriceCard` — three variants (module / Grain / Alpine).
> - `SpecTable` — recessed, tabular-nums, zebra-free, hairline rows.
> - `VideoEmbed` — click-to-load poster.
> - `PresetChip` — category-coloured, plays on click.
> - `FaceTour` — numbered hotspots over a plugin screenshot.
>
> Give every component its hover, focus-visible, active, disabled and loading
> state. Focus rings are a 2px accent outline at 2px offset — visible on the
> dark ground, never removed.
>
> ## Responsive
>
> Breakpoints 1440 / 1280 / 1024 / 768 / 480. On mobile: the chain builder
> becomes a vertical, tap-to-reorder list; the engine explorer becomes an
> accordion; the A/B player stacks with the source selector as a horizontal
> scroller; section spacing drops to 72px. Plugin face renders get a horizontal
> pan/pinch container rather than being shrunk illegibly.
>
> ## Accessibility and performance
>
> WCAG 2.2 AA. All text must clear 4.5:1 against its ground — check
> `--bb-ink-dim` `#6c8288` on `--bb-page` `#0f1315` and darken the ground or
> lift the ink if it fails. Audio players need real controls, labels and
> keyboard operation; never autoplay. The chain builder needs an ARIA
> drag-and-drop pattern with keyboard alternatives. Semantic landmarks, one h1
> per page, visible skip link. Target LCP < 2.0s: images as AVIF/WebP with
> explicit dimensions, audio and video lazy, no web-font FOIT (`font-display:
> swap`).
>
> ## Output
>
> Deliver: (1) high-fidelity designs for every page at desktop and mobile;
> (2) the design-token file; (3) responsive front-end code — semantic HTML with
> Tailwind, or React + Tailwind components — with the tokens as CSS custom
> properties; (4) every image as a clearly labelled placeholder with its
> intended dimensions and filename, since real plugin renders, audio and video
> will be dropped in later. Do not invent feature names, engine names or
> parameter names: use exactly the inventory below.

---

# PART B — content inventory (verbatim; paste with Part A)

> ### BitBit Alpine — $99
> Four drag-to-reorder modules, eleven engines, one window. Every module has its
> own power toggle, level trim and Mix. Engines keep running while deselected,
> so switching is silent and nothing is lost.
>
> **Module 1 — Artifact (red `#c00001`) — destruction and character. 4 engines.**
> The face labels them `RING`, `CRASHER`, `RUST`, `AMP` — use those short names
> as the engine-chip labels and the longer names in the description:
> - **Ring** (ring modulation) — a sine carrier with a post low-pass and a
>   bipolar rectifier. Two voicings, switched by a Wobble / Octave toggle.
>   Controls: Freq · Tweak · Filter · Rectify · Wobble/Octave · Mix
> - **Crasher** (bit crushing) — sample-and-hold downsampling plus bit-depth
>   quantisation, with jitter on the sample clock.
>   Controls: Bits · Rate · Filter · Jitter · Mix
> - **Rust** — degradation with a memory. A wear state tracks how hard you have
>   been playing and heals when you stop, so a note corrodes as it rings.
>   Oxide (soft, magnetic) or Contact (hard, electrical).
>   Controls: Grind · Tone · Oxide/Contact · Mix
> - **Amp** — tube drive with a peaking mid, a bipolar tone tilt, sample-rate
>   reduction and an optional Haas stereo spread.
>   Controls: Drive · Mids · Bit · Tone · Mono/Stereo · Mix
>
> **Module 2 — Modulation (amber `#e0b23c`), labelled `MOD` on the face —
> movement and warmth. 5 engines:**
> - **Tape** — a whole tape machine: saturation, flutter, wear, noise, a bipolar
>   tone tilt, mono or stereo. Runs fully wet by design, so it has no Mix — the
>   module's power toggle is its dry/wet.
>   Controls: Saturation · Flutter · Wear · Noise · Tone · Mono/Stereo
> - **Trem** — amplitude modulation with a shape control and a tube bias stage;
>   a Sync pill locks the rate to host tempo.
>   Controls: Amount · Rate · Shape · Tube · Sync · Mix
> - **Chorus** — Controls: Rate · Depth · Phase · Mix
> - **Phaser** — Controls: Rate · Depth · Mix
> - **Filter** — a resonant low-pass swept by an LFO, with a Triangle / Ramp /
>   Square wave picker and a stereo mode that runs the right channel half a
>   cycle out of phase.
>   Controls: Freq (200–1600 Hz) · Q · Range · Time · Sync · Wave · Mono/Stereo · Mix
>
> **Module 3 — Delay (green `#a3ce7a`) — a tape delay, and the only wide module:**
> - **Routings: Normal · Wide · Ping Pong**, on a pill beside Sync. Independent
>   left and right times, free-running in milliseconds or locked to host tempo,
>   with a chain-link that ties them together. The readouts show both at once —
>   `L 1/4  698 ms`.
> - **Tape placement: Pre or Post** — colour the input, or colour only the
>   repeats.
> - The face groups its character controls into three captioned strips along the
>   bottom, each with its own icon: **Tape** (Wear · Flutter), **Mod**
>   (Drift · Phaser) and **Filter** (Low Cut · High Cut).
> - Main controls: Mix · Feedback · L Time · R Time · Link · Sync · Routing,
>   plus In and Out trims.
>
> **Module 4 — Reverb (cyan `#7fd2d8`) — 2 engines:**
> - **Space** — a feedback-delay-network reverb with 0.3 to 40 second decays and
>   a shimmer octave.
>   Controls: Decay · Shimmer · Low Cut · Reso · Mix
> - **Spring** — two detuned spring tanks in stereo.
>   Controls: Decay · Tension · Low Cut · Mix
>
> **Alpine factory preset categories:** Digital, Ping Pong, Modulated, plus
> uncategorised headline presets. Real preset names to use as examples:
> *Clean Quarters · Glass Halves · Precision Slap · Crystal Triplets · Tape
> Bounce · Cross Rhythm · Triplet Bounce · Eighths · Deep Wow · Phase Trails ·
> Warble Slap · Detune Drift · Chorus Tape · Vibrato Echo · Flutter Machine ·
> Shimmer Phase · Dub Chamber · Tape Wash · Slapback · Dotted Wide.*
>
> ### BitBit Grains — $49
> **Shatter a sound into grains. Freeze it, scrub it, keep it in key.**
>
> A granular delay into a plate. Mono or stereo in, stereo out. A 10.5-second
> circular buffer, up to 32 overlapping grains, each at its own pitch,
> direction and pan. A **Live / Freeze** pill sits top-left; a **Wide** pill in
> the Grain header sets the stereo spread.
>
> The face is four sections across the top (Grain · Pitch · Random · Mixer) with
> a two-tab panel underneath — **Effects** and **Mod**.
>
> - **Grain** — **Size** (20 ms – 1 s, tempo-syncable) · **Destiny** (the grain
>   density, 1–40 grains/sec — the plugin calls it Destiny and so should the
>   site) · **Window** · **Shape** (soft fade to plucky attack), over a display
>   of the actual grain envelope with its overlapping copies. Freeze holds the
>   buffer and **Stretch** scrubs through it: +100 % forward at realtime, 0
>   frozen on one moment, −100 % backwards, with the pitch untouched.
> - **Pitch** — **Low · Unison · High** are weights against each other, not
>   positions on a scale, drawn as a three-bar histogram. Low drops an octave,
>   High jumps one up. A **Scale** block with its own power toggle picks Root
>   (C–B) and Scale (Major · Minor · Pentatonic Major · Pentatonic Minor ·
>   Chromatic), and a **Mix** decides how much of the scale the High group
>   takes. Two weights at once is a chord, not a transposition.
> - **Random** — **Stereo** (width of the random pan placement, equal-power) ·
>   **Reverse** (share of grains playing backwards) · **Scatter** (timing and
>   length randomness) · **Mod**, over a display that scatters each grain as a
>   dot in an L/R field.
> - **Mixer** (right-hand column) — **Dry** and **Grains** as two independent
>   vertical faders reading in dB rather than one crossfade, with a **Link**
>   toggle, so "all of both at once" is reachable. Under them, **Drive** (red)
>   and **Bit**, then a filter-response scope and the **Filter** knob over the
>   grain cloud only — the dry path is never filtered.
> - **Effects tab** — a **Delay** panel (Mix · Feedback · L/R times with Link,
>   Sync and a Normal/Wide/Ping Pong pill) and a **Reverb** panel (Mix · Decay ·
>   Low Cut over a decaying-tail display). Both cyan, both with their own power
>   toggle.
> - **Mod tab** — a green **breakpoint LFO editor** on a 0/50/100 grid: drag the
>   points to draw the shape by hand. Below it a **LFO** chip you drag onto any
>   moddable knob to assign it, a **Rate** knob, a **Sync** pill and a
>   destination dropdown. This is the thing no competitor screenshot has — give
>   it its own section on the product page.
>
> Header on both products: the mountain mark and product name top-left, a
> **Level** slider with a dB readout and a power ring top-right, and the preset
> bar centre — `<` `>` steppers, a searchable dropdown, a save button and a dice
> button that randomises the patch. **Put the dice in the website copy** —
> "press the dice until something surprises you" is a good line.
>
> ### The four modules, standalone — $19 each
> BitBit Artifact · BitBit Modulation · BitBit Delay · BitBit Reverb. Identical
> DSP and identical face to the module inside Alpine — a preset sounds the same
> in either. Each is a single narrow window with a `<>` stepper through its
> engines and a Mix in the footer.
>
> ### Cross-cutting facts worth surfacing
> - Formats: VST3, AU, AAX. macOS 11+ Universal (Apple Silicon native) and
>   Windows 10+ 64-bit.
> - Engines stay warm while deselected — switching never clicks and never loses
>   a setting.
> - No on/off switch of its own: bypass crossfades to dry, latency-compensated,
>   so a host's device on/off never clicks or shifts timing.
> - Tempo sync throughout, and the delay follows transport relocation.
> - Low, reported latency (~6 ms on the Modulation module) — designed to be
>   played live, not just rendered.
> - Factory preset library included, organised by category; user presets saveable.

---

# PART C — short version (for a small prompt box)

> Design a dark, cinematic marketing website for **BitBit Audio**, a boutique
> VST plugin company. Reference the mood of excite-audio.com/lifeline/lifeline-expanse,
> the module breakdown of XLN Audio RC-20 Retro Color, and the interaction
> energy of babyaud.io/transit.
>
> Two products: **BitBit Alpine, $99** — a multi-effect of four drag-to-reorder
> modules (Artifact / Mod / Delay / Reverb) with eleven engines; and
> **BitBit Grains, $49** — "Shatter a sound into grains. Freeze it, scrub it,
> keep it in key." A granular delay into a plate, with freeze, scrubbing,
> scale-aware pitch and a hand-drawn breakpoint LFO. The four Alpine modules are
> also sold standalone at **$19 each**, and the pricing page must make that
> upgrade decision obvious ($19 × 4 = $76 of modules; Alpine is $99 and adds the
> host and the reorderable chain).
>
> Palette, dark only: page `#0f1315`, panel `#171d20`, edge `#222b2e`, primary
> text `#b9d3d9`, secondary `#8ba3a9`, dim `#6c8288`. Module accents used
> consistently for identity: Artifact `#c00001` red, Mod `#e0b23c` amber,
> Delay `#a3ce7a` green, Reverb `#7fd2d8` cyan, Grains `#b39bd8` violet.
> Accents are hairlines, icons, knob arcs and chart strokes only — never big
> filled buttons. Typeface `Space Grotesk`; uppercase 11px labels at 0.14em
> tracking; 20px panel radius; shadows instead of borders.
>
> Pages: Home, /alpine, /grains, /modules (+ one per module), /pricing, /demos,
> /support, /about.
>
> Three signature components: **(1) an A/B audio player** that crossfades
> between a dry guitar take and the processed take *while playing*, with a
> source selector and accent-tinted preset chips; **(2) a draggable four-module
> signal-chain diagram** that reorders like the real plugin does and annotates
> what each order sounds like; **(3) an engine explorer** presenting all eleven
> engines with their real control names and an audio example each. Plus a
> click-to-load 1-minute YouTube embed behind a custom poster.
>
> Voice: technical, understated, engineer-to-engineer. "Character, not
> correction." No hype words.
>
> Deliver desktop and mobile designs, the token file, and responsive Tailwind
> code. All imagery, audio and video as clearly labelled placeholders with
> dimensions — real assets come later. WCAG 2.2 AA, no autoplay, respect
> `prefers-reduced-motion`.

---

# PART D — the faces themselves (paste with the screenshots)

The website should feel like an extension of these windows, so here is what is
actually in them.

> ## Anatomy of a module
>
> Every narrow module — Artifact, Mod, Reverb, and each of Alpine's side slots —
> is the same five-part stack, and the website's `EngineCard` should echo it:
>
> 1. **Header** — a power ring in the module's accent colour, the module name in
>    uppercase at wide tracking, and (inside Alpine) a small level trim knob at
>    the right. In Alpine this header is also the drag handle.
> 2. **Engine stepper** — a recessed pill: `‹  [icon]  NAME  ›`. The icon and the
>    chevrons are in the module's accent; the name is in pale ink.
> 3. **Display** — a recessed panel drawing what the engine is doing, in the
>    accent colour on a tinted ground (Artifact's ground is tinted dark red).
> 4. **Knob field** — a 2×2 or 2+1 grid of near-black knobs with a white pointer
>    and an accent-coloured value arc that appears only where the knob has been
>    moved off its rest. A bipolar knob draws its arc out from twelve o'clock in
>    whichever direction it was turned.
> 5. **Footer** — a divider, then a single **Mix** knob, centred.
>
> Delay is the exception: it is wide, and carries a full-width tap display
> (vertical bars decaying left to right across an L and an R lane) plus a bottom
> strip of three captioned, icon-led groups.
>
> ## Engine icon vocabulary
>
> **If the tool has repo access, do not redraw these — they already exist as
> React SVG components** in `packages/pedal-ui/src/`: `TapeIcon`, `TremoloIcon`,
> `ModIcon` (also used for Chorus), `PhaserIcon`, `FilterIcon`, `SpaceIcon`,
> `SpringIcon`, `RustIcon`, `CrushIcon`, `BitIcon`, plus `PowerIcon`,
> `LinkIcon`, `DiceIcon`, `SaveIcon` and `Chevron`. All are exported from
> `@synthpeak/pedal-ui` and take a `size` prop. Import them. Only redraw if the
> tool is image-only, in which case here is what each one is:
>
> | Engine | Icon | Display graphic |
> |---|---|---|
> | **Ring** | a twisted double-sine ribbon | a dense red waveform under a shaded carrier envelope |
> | **Crasher** | a stepped staircase wave | a red stepped square wave |
> | **Rust** | a jagged, broken-up wave | a red sine eaten away into steps and grit |
> | **Amp** | a soft-clipping transfer curve | a clean red sine, gently squared |
> | **Tape** | two tape reels on a deck | — |
> | **Trem** | a tight squiggle | an amber comb of amplitude bars |
> | **Chorus** | a twisted ribbon | — |
> | **Phaser** | a `V` notch curve | — |
> | **Filter** | a resonant bell curve | an amber bell over a shaded sweep range, with an infinity mark in the corner |
> | **Space** | concentric circles, a target | cyan bars decaying to nothing |
> | **Spring** | a coil / zigzag spring | cyan bars decaying to nothing |
>
> Inside the Delay module the three bottom groups carry their own marks: **Tape**
> (green reels), **Mod** (amber double-sine ribbon), **Filter** (pink shelf
> curve).
>
> These marks are the best asset the brand has. Use them as the engine explorer's
> icons, as the nav's product glyphs, and as the favicon set. Line art on a dark
> ground, one accent colour each, no fill.
>
> ## Control vocabulary to reuse on the website
>
> - **Pills** — uppercase, tracked; outlined pale-on-dark when off, filled pale
>   with dark ink when on (`SYNC`, `WIDE`, `LIVE`, `NORMAL`). Use these for the
>   demo player's source selector and the pricing toggle.
> - **Sliding toggles** — a dark track with a white knuckle and the two states
>   labelled either side: `MONO / STEREO`, `WOBBLE / OCTAVE`, `OXIDE / CONTACT`.
> - **Numeric readouts** — a recessed dark box, value in pale ink, often paired
>   (`L 1/4   698 ms`). Use this exact treatment for prices and spec figures.
> - **Chain-link glyph** — a small link button between two paired controls,
>   showing they move together. A good motif for the "buy all four" bundle card.
> - **Preset bar** — `‹` `›` steppers, a dropdown, a save icon, and a **dice**
>   that randomises the patch. Worth rebuilding in the website's demo player.
> - **Vertical tab rail** — Grains' `EFFECTS` / `MOD` tabs, rotated 90° on the
>   left edge of the panel. A distinctive move; consider it for the engine
>   explorer on desktop.
>
> ## Two things the screenshots prove, so say them in the copy
>
> - **Order matters.** Two Alpine screenshots, identical but for Artifact and Mod
>   swapped, are the whole argument for the flagship. Chorus into Ring is not
>   Ring into Chorus. Put the two images side by side with a play button under
>   each, and let the chain builder above them demonstrate it live.
> - **Nothing is hidden.** Every control is on one surface — no menus, no pages,
>   no hamburger, no second window. Say so; it is a real differentiator against
>   plugins that bury half the feature set.

