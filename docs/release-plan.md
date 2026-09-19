# Release plan

Everything between here and the first paid download. Written against the tree as
it stands, so the gaps below are real gaps, not hypotheticals.

The plan is ordered by **gates**, not dates. A gate is a state the project is in;
nothing downstream of a gate should be started while an upstream one is red,
because the rework is what kills a release schedule.

---

## Three decisions that get more expensive every day

Settle these before any other work. Each one is cheap now and very expensive
after the first sale.

### D1. The name. `Peak` or `BitBit`?

The plugins are **Peak Alpine**, **Peak Grain**, manufacturer code `Peak`, plugin
codes `Palp` / `Pgrn` / …, presets in `~/Library/Peak/<Product>/Presets`. The
website says **BitBit Audio**, **BitBit Alpine**, **BitBit Grains**.

A rename touches the AU type/subtype/manufacturer codes, the VST3 UID (derived
from them), the bundle identifier, the product name, the preset directory and the
binary-data preset headers. **After release, changing any of those makes every
saved session lose its plugin** — the host looks up a plugin by ID, not by name,
and a missing ID is a silent hole in the user's project.

Pick one, do the rename in a single commit, and include a preset-directory
migration if `~/Library/Peak` already has user presets on your own machine.

### D2. What is actually for sale?

Fifteen plugins exist in `plugins/`. The website sells two products plus four
`$19` modules:

| On the website | In the tree |
| --- | --- |
| Alpine `$99` | `peak-alpine` |
| Grains `$49` | `peak-grain` |
| Artifact / Modulation / Delay / Reverb `$19` each | `peak-artifact`, `peak-modulation`, `peak-delay`, `peak-reverb` |
| — | `peak-chorus`, `peak-eq`, `peak-overdrive`, `peak-phase`, `peak-spring`, `peak-sympathy`, `peak-tape`, `peak-trem-pan`, `peak-wah` |

Nine pedals are in the repo with no place in the story. Decide per pedal: ship at
launch, hold for a later release, or fold into a module. Every one you ship
multiplies the QA matrix, the preset work, the screenshots and the support load —
this is the single biggest lever on how long the release takes.

Recommendation: launch with the six on the website, hold the other nine. They lose
nothing by waiting and they are the obvious "1.1 is here" story.

### D3. The platform promise

The site currently claims **VST3 · AU · AAX**, **macOS 11+**, **Windows 10+ 64-bit**.
The tree builds **VST3, AU and Standalone, macOS only**:

- No Windows build exists. Not a switch — the six WebView faces need WebView2, the
  build needs MSVC, the preset paths change, and signing needs a separate
  certificate and installer toolchain.
- No AAX. AAX needs an Avid developer agreement and PACE signing; Pro Tools is not
  reachable without it.

Fix the claim, not the code, for 1.0: **macOS, VST3 + AU + Standalone**. Put a
Windows waitlist form on the site instead — it also tells you whether Windows is
worth building.

---

## G1 — Correctness. Nothing ships until this is green

This is item 2 on your list, and it is the gate that matters most. A crash in a
paid plugin costs a refund, a review and a support thread; a crash in a beta
costs nothing.

### 1.1 Freeze the contract first

Before hunting bugs, freeze what must never change:

- **Parameter IDs per plugin.** Add a golden-file test: dump every parameter ID,
  range, default and version-hint to a checked-in text file and fail the build
  when it changes without the file being updated. An accidental rename silently
  breaks every saved session and preset, and there is no test for it today.
- **Engine enum order.** Already a house rule (`CLAUDE.md`: append LAST). Put it
  in the golden file too.
- **The preset format.** Write a state-version tag into the APVTS tree now, even
  though nothing reads it — the release where you need to migrate state is much
  easier if 1.0 already stamped a version.

### 1.2 pluginval, in CI, at strictness 10

This is the highest-value single item in the whole plan and the tree has none of
it. `pluginval` is what every host developer's bug report will otherwise tell you.

- Run it over every plugin × every format at `--strictness-level 10`
  `--validate-in-process` and `--repeat 5`.
- `auval -strict -v aufx <CODE> <MFR>` for every AU, in **both architectures**
  (`arch -arm64` and `arch -x86_64`) — the universal-binary trap in `CLAUDE.md`
  means "it loads here" proves nothing.
- Wire both into GitHub Actions on a macOS runner alongside `ee_dsp_tests`,
  `ee_preset_tests` and the `*_regress` checksums. **There is no CI at all right
  now**, which means the two known failures are the only ones anyone notices.

### 1.3 Sanitizers, because you already know there is a race

Build the offline host tools (`ee_*_host`, `ee_dsp_tests`, `ee_*_stress`) under:

- **ASan + UBSan** — cheap, catches the overwhelming majority of real crashes.
- **TSan** — you have a confirmed data race waiting: `daisysp::myrand()` backs the
  shimmer's modulation slew and is one function-local `static uint32_t seed`
  advanced per sample, shared by every instance in the process and touched from
  multiple audio threads (`CLAUDE.md`, "Shimmer is not bit-reproducible"). It is
  documented as inaudible. It is still undefined behaviour in shipping code, and
  it blocks checksumming any shimmered render. Fork the DaisySP `PitchShifter` to
  take a per-instance seed and the race and the reproducibility gap both close.
- **MSan or valgrind** for the second known failure: *"chorus is silent on a
  silent input" fails on roughly half of runs with no rebuild in between.*
  Nondeterminism with a fixed binary and fixed input is uninitialised memory, a
  shared mutable static, or a denormal/FTZ difference. **Do not ship with this
  open** — it is the shape of bug that becomes "it sometimes drops out in Logic".

### 1.4 The two known failures

`CLAUDE.md` lists them as baseline and `scripts/dev-check.sh` filters them out.
That filter is right for daily work and wrong for a release. Both get fixed, or
explicitly re-baselined with a written reason, before G1 closes:

- `tape at 100 % moves the level too far` — either the test's expectation or the
  voicing is wrong. Decide which.
- `chorus is silent on a silent input` — see above.

### 1.5 A real soak harness

You already have the pieces: `ee_grain_host`, `ee_alpine_host`,
`ee_modulation_host`, `ee_reverb_host` drive real processors, and
`tests/RegressHarness.h` has the ragged block sizes and the `FakePlayHead`.
Generalise them into one `ee_soak <plugin>` that runs for hours and asserts on
every block:

- output is finite, and below a sane ceiling
- no allocation on the audio thread (hook `operator new` during `processBlock`)
- no lock taken on the audio thread

…while randomising, continuously:

- every parameter, including engine switches mid-note (engine switching is a
  known click risk — `ee_module_stress` covers a slice of this already)
- block size 1 … 8192, including 1-sample and prime sizes
- sample rate 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz, with `prepareToPlay`
  re-entered mid-run
- transport: rolling, stopped, looping, and **relocating** — the tempo-locked
  engines only reach their alignment code with a playhead
- preset loads *during* processing. This is the exact shape of the
  `PeakDelayProcessor::installState` bug ("a whole tree arriving at once is not a
  knob being turned"); assume there are more of them.
- editor open / close cycles, and instance create / destroy
- 32+ instances in one process — the only reliable way to surface shared statics
  like the DaisySP one

Run it overnight per plugin. A clean 8-hour soak per product is a reasonable bar
for G1.

### 1.6 Input fuzzing where user data enters

`PresetStore` reads XML the user can edit, move and corrupt. Fuzz it with
truncated, malformed, wrong-schema and enormous files — a plugin that throws on a
bad preset file takes the host down with it.

### 1.7 Other correctness checks worth running once each

- `ScopedNoDenormals` present in every `processBlock` — a missing one is a 10×
  CPU spike on a reverb tail and reads to the user as "it freezes".
- Offline bounce vs realtime render must match. Hosts render offline faster than
  realtime and anything that reads a wall clock will break.
- Mono→stereo, stereo→stereo and any mono→mono configuration the hosts will try.
- Leak check across 100 editor open/close cycles.
- CPU ceiling per plugin at 96 kHz with everything on, and Alpine with all four
  modules active. Publish the numbers; set expectations before a reviewer does.

### 1.8 Then, and only then, humans

A closed beta of 10–20 people, on real projects, with a bug-report template that
asks for host + host version + macOS version + architecture + plugin version +
steps. Most of what they find will be usability, not crashes, which is exactly
what you cannot test for yourself.

---

## G2 — It installs, and macOS trusts it

Today `scripts/package-macos.sh` does `codesign --force --sign -` — **ad-hoc**.
That is fine for your own machine and unusable for customers: Gatekeeper blocks
it, and the `INSTALL.txt` instruction to run `xattr -dr com.apple.quarantine` is
the moment a normal buyer asks for a refund.

What is needed:

1. **Apple Developer Program** membership.
2. **Developer ID Application** certificate — sign every `.vst3`, `.component`
   and `.app` with `--options runtime` (hardened runtime) and a secure timestamp.
   Check whether any host needs
   `com.apple.security.cs.disable-library-validation`.
3. **Notarisation** — `xcrun notarytool submit --wait`, then `xcrun stapler
   staple` every bundle **and** the installer.
4. **A real installer.** `pkgbuild` per component + `productbuild` with a
   distribution XML, signed with a **Developer ID Installer** certificate. Users
   do not drag plugins into `~/Library`. The installer should let them choose
   formats, show the EULA, and install to the system folders.
5. **An uninstaller** — a script or a documented list of paths. Ship it.
6. **Verify on a machine that has never seen the plugin**, freshly downloaded via
   a browser so the quarantine bit is actually set. A VM snapshot is the only
   honest test here.
7. Keep `package-macos.sh` as the one entry point and make CI produce the signed,
   notarised artefact so releases are never hand-built.

---

## G3 — Commerce and licensing (your item 1, the LemonSqueezy part)

There is **no licence check, no activation, no trial and no DRM** anywhere in the
codebase. That is a feature to build, not a setting to switch on.

### Store

LemonSqueezy is a good pick specifically because it is **Merchant of Record** — it
handles EU VAT, US sales tax, invoices and chargebacks, which is otherwise a
genuinely large amount of work for a solo product.

Set up: one product per SKU (Alpine, Grains, four modules), a bundle if you want
one, the customer portal for re-downloads, and webhooks for order events.

### Licensing — build the smallest thing that works

Recommended shape, in order of how much it costs you:

1. **LemonSqueezy licence keys** (built into the platform: generate on purchase,
   `activate` / `validate` / `deactivate` endpoints, activation limit per key).
2. **In-plugin activation screen** — email + key, one HTTP call, on a background
   thread, never the audio thread. Success writes a licence file to
   `~/Library/Application Support/<Brand>/license.json`.
3. **Offline after activation.** Do not phone home on every launch; a studio
   machine may be permanently offline. Re-validate on a long interval with a
   generous grace period, and never disable the plugin because a request failed.
4. **Sign the licence file** (Ed25519, public key compiled in) so the offline
   check needs no server and cannot be forged by editing JSON.
5. **Device limit** of 3–5 activations with a self-service deactivate in the
   customer portal.

Do not build more than this. Every additional layer of protection costs you more
in support tickets from paying customers than it recovers from people who were
never going to pay.

### Free codes for friends and YouTubers (your item 1)

Use **100 %-off discount codes** in LemonSqueezy rather than a separate code path
in the plugin. The recipient "buys" for `$0`, gets a real licence key through the
normal flow, and you get a row in your dashboard telling you which code was used.
One mechanism, no special-case code, full attribution.

Do **not** put a public "free codes" page on the site. Distribute codes privately;
if you want a redemption page, make it an unlisted `/redeem` route that only
validates server-side.

### Trial

Decide explicitly. A 14-day full-featured trial pairs naturally with the 14-day
refund promise already on the site and removes most pre-sale support questions.
The alternative — a periodic noise burst or a silence every 60 s — is cheaper to
build and much worse for reviews.

---

## G4 — Updates (your item 3)

Nothing exists today; the version is `0.10.0` in the top-level `CMakeLists.txt`.

**Recommended for 1.0 — the boring option that works:**

- Publish `https://<domain>/updates.json` with `{product: {version, notes_url,
  download_url, min_macos}}`.
- On editor open, at most once per day, on a background thread, fetch it. If it is
  newer, show a small dismissable pill in the face linking to the download page.
  Failures are silent. Make it switchable off, and say so in the privacy policy —
  a version check is a network request and you now have a privacy page to keep
  honest.
- Downloading and installing stays manual: the notarised `.pkg` from the customer
  portal. Auto-install of an audio plugin while the host has it loaded is a
  category of bug you do not want to own in 1.0.
- **Sparkle** is worth it later, and only for the Standalone app.

**The discipline that matters more than the mechanism:**

- Semantic versioning, and a git tag plus an archived signed artefact for every
  release. Users *will* need to roll back; if you cannot hand them the previous
  build, a regression becomes a refund.
- Never remove or rename a parameter ID. New parameters get defaults that
  reproduce the old behaviour exactly — the `*_regress` checksum batteries are
  exactly the tool that proves it, and `ee_spring_regress` already demonstrates
  the pattern for an additive change.
- A public changelog page. It is also your best "still actively developed" signal.

---

## G5 — Content: presets, the plate engine, latency, the preset browser

Do the DSP-changing items (**plate**, **latency**) *before* you freeze the release
regression checksums — both alter audio, and re-baselining after the fact removes
the only evidence that nothing else moved.

### 5.1 Presets (your item 4)

Where it stands:

| Plugin | Factory presets |
| --- | --- |
| `peak-delay` | 22 |
| `peak-sympathy` | 7 |
| `peak-artifact` | 1 (Init) |
| `peak-reverb` | 1 (Init) |
| `peak-modulation` | 1 (Init) |
| `peak-grain` | 0 — its own flat, user-only store |
| `peak-alpine` | — |

For products you are charging for, this is the largest content gap in the plan.
The website already promises "global presets across the whole chain".

- Set a target per product. 30–40 for Alpine, 25+ for Grains, 15–20 per module is
  a defensible floor.
- Author with `-DEE_PRESET_AUTHOR=ON` (writes straight into the pedal's `presets/`
  folder for committing).
- Use the `Category - Name.xml` filename convention — the prefix *is* the category
  in the picker, and a flat glob is all a factory preset gets.
- `ee_preset_tests` already enforces that **every parameter appears in every
  preset**; keep it in CI, because a partial preset silently inherits from
  whatever was loaded before it.
- Alpine's presets should exist to show off the thing the product is sold on:
  make several of them reorder the chain.
- **Migrate `peak-grain` to `ee::plugin::PresetStore`**, with a one-time copy of
  any existing `~/Library/…` user presets into the new location. Doing this after
  launch means migrating real customers' saved work.
- Get a second pair of ears. Presets are the demo most buyers judge you on, and
  the author is the worst judge of their own.

### 5.2 Plate reverb engine (your item 6)

Note first: **`ee::dsp::FdnReverb` is already plate-voiced** — its own header
describes it as a plate-voiced 16-line FDN, and it is what Peak Reverb's Space
engine runs. So "add a Plate engine" is one of two quite different jobs:

- **Cheap and credible:** a third engine in `ee::fx::ReverbModule` that runs
  `FdnReverb` from a new `PlateConfig.h` — shorter predelay, higher input
  diffusion, tighter modulation, no shimmer — voiced deliberately against Space so
  the two are obviously different in a preset A/B.
- **Expensive and distinctive:** a true Dattorro plate as a separate engine. A
  real project, with its own voicing loop and reference material.

Either way, the checklist is the one in `CLAUDE.md` for adding an Artifact engine,
transposed: **engine enum appended LAST**, `ReverbModule` prepare/render/reset,
`peak-reverb` params + `Init.xml`, `peak-alpine` `rev.` bindings, the
`module-face` engine list, `tests/UiSnapshot.cpp` mirrored (nothing catches drift
there), `ee_reverb_host` coverage, a `*_regress` section asserting the new
controls at their defaults are **bit-identical** to before, and the README.

### 5.3 Latency (your item 7)

Where it stands:

| Product | Reported latency | Comes from |
| --- | --- | --- |
| Peak Tape | ~264 samples (~6 ms @ 44.1 k) | `TapeTransport` 198 + `TapeCharacter` 66 |
| Peak Delay | tape input latency | `DelayModule::tapeIn`, pre-section only |
| Peak Modulation | tape engine only | other engines report 0 |
| Peak Reverb | 0 | both engines latency-free |
| Peak Alpine | sum of Artifact + Modulation + Delay | per-module |
| Everything else | 0 | no `setLatencySamples` |

So the site's "~6 ms, reported and compensated" is true of the tape path and
*wrong* for Grains and Reverb, which are zero. Make the claim per product.

Work worth doing, in order:

1. **Prove the compensation.** Null test in a real host: the same audio on two
   tracks, one through the plugin at Mix 0, polarity inverted. It should cancel to
   silence. If it does not, PDC is wrong, and that is a far worse bug than 6 ms.
2. **Re-report on every change.** Latency must be pushed to the host when the
   engine changes or a module is bypassed/reordered, not only in `prepareToPlay`.
   Verify Alpine does this — a stale latency number is a subtle timing bug the
   user will blame on their own playing.
3. **Then reduce it.** The 198-sample nominal in `TapeTransport` is the budget the
   wow/flutter modulation is allowed to swing within; shortening it narrows the
   movement before the read pointer crosses the write pointer. A **"Low latency"**
   switch on the tape stage that halves the nominal and clamps flutter depth gives
   the user the choice instead of you guessing.
4. Anything below ~5 ms stops mattering to most players. Do not trade the tape
   voicing — which is the product — for a number.

### 5.4 Full-screen preset browser (your item 8)

Today: `PresetPicker.jsx` is a Mantine Cascader dropped out of a 118 px (joined) /
190 px (separated) combobox in `PresetBar.jsx`, `withinPortal: false`, opening
inline under the bar.

A full-face browser is a different component, not a size change:

- New component in `packages/pedal-ui` so all six WebView pedals get it at once.
  The nine native `ee::ui` pedals keep their JUCE menu — do not try to unify.
- Overlay portalled to the face root, covering the whole `.pui-card`, with a
  backdrop, ESC and click-outside to close.
- Category column + preset list + a search field, arrow-key navigation, Enter to
  load, and the current preset highlighted on open.
- Keep `PresetBar`'s prev/next steppers and the Save box exactly as they are —
  they are the fast path and the browser is the browse path.
- Both variants (`joined`, `separated`) must keep working; Peak Artifact runs with
  `showSteppers={false}`.
- Watch the gallery CSS-collision rule in `CLAUDE.md`: scoping classes must be
  unique across *all* WebView pedals, not just within one.
- `apps/pedal-gallery` is the dev loop; remember `useJuceSliderValue` has no echo
  outside a host.

---

## G6 — Hosts and platforms (your item 5)

Per host, run the same list: scan/validate, PDC null test, automation write and
read back, save and reload the session, change the sample rate with the plugin
loaded, offline bounce, editor resize, several instances, CPU under load, and
close/reopen the editor twenty times.

| Host | Formats | Why it is on the list |
| --- | --- | --- |
| Ableton Live 11 + 12 | AU, VST3 | Your primary target. Test **native and under Rosetta** — Live ships universal and the architecture trap in `CLAUDE.md` bites here |
| Logic Pro | AU only | Strictest AU validation; sandboxed; will reject things `auval` lets through |
| Reaper | VST3, AU | Best bug-finder: exotic block sizes, channel counts, offline render modes |
| Cubase / Nuendo | VST3 | Strictest VST3 bus and parameter rules |
| Studio One | VST3, AU | Large user base, aggressive plugin scanning |
| Bitwig | VST3 | Sandboxed per plugin; surfaces crashes cleanly |
| FL Studio (macOS) | VST3, AU | Unusual parameter/automation model |
| GarageBand | AU | Free, sandboxed, and where your least technical users are |
| Pro Tools | AAX | Only if D3 says AAX. Otherwise out of scope |

**Windows is a project, not a test pass.** If D3 says yes, it needs: MSVC/CMake
build, WebView2 runtime for the six WebView faces, VST3 + Standalone only (no AU),
a different user-data path than `~/Library`, a code-signing certificate (OV/EV, or
Azure Trusted Signing), an installer (Inno Setup or WiX), its own CI runner and
its own pass through the host matrix. Treat it as a separate release.

---

## G7 — Website (your item 1)

### Screenshots

Make them generated, not hand-captured, so they never drift from the product:

- `ee_ui_snapshot` already renders every native face to PNG. Extend it, or add a
  Playwright script at `deviceScaleFactor: 2` for the WebView faces, and commit
  the output into `apps/website/public/assets/`.
- Fix one standard: same zoom, same window chrome, a chosen preset per shot rather
  than Init, transparent or consistent background.
- Note the trap in `CLAUDE.md`: **`tests/UiSnapshot.cpp` duplicates every pedal's
  parameter layout and spec, and nothing checks it against the real plugin.** If
  the marketing screenshots come from it, that duplication is now a marketing bug
  too. Prefer capturing the real editor.
- The Grains page currently crops one screenshot per section (`focus` / `zoom` /
  `aspect` in `apps/website/src/data.js`) because only two captures exist. Real
  per-section captures replace both the crops and those fields.

### Audio demos

- Record one take per source (Dry Guitar, Clean Rhythm, Synth Pad, Drum Loop) and
  render a wet pass per preset from the **same** take, at matched loudness — the
  A/B switch is worthless if the wet one is simply louder.
- Wire `AbPlayer.jsx`: two preloaded `<audio>` elements sharing a transport,
  switching on the Dry/Processed toggle, with the scrub bound to real time.
- Replace the fake waveform bars with real peaks — analyse the files at build time
  into a small JSON.
- Normalise, trim silence, export as AAC or Opus, and keep them short.

### Video

- Under 90 seconds per product. No talking head. Play the riff dry, then wet, and
  show the one control that explains the product (Alpine: the drag-reorder;
  Grains: Freeze and Stretch).
- Embed click-to-load: a poster image that swaps in the YouTube iframe on click.
  Faster, and it keeps YouTube's cookies off the page until the user asks for them.

### Analytics, errors, legal

- **Plausible or Fathom**, not GA4 — cookieless and consent-banner-free, which
  matters because the rest of this page is a sales page.
- **Sentry** for the site's JS. For the plugin, there is no crash reporter today;
  if you want one, make it opt-in and Standalone-only. Otherwise rely on the beta
  and on a good bug-report template.
- **EULA, privacy policy, terms, refund policy.** Linked in the footer, and the
  EULA shown in the installer. The privacy policy must mention the update check
  and the licence activation call.

### Launch

- Netlify from GitHub: build `npm run build --workspace bitbit-website`, publish
  `apps/website/dist`. The SPA fallback is already in place
  (`apps/website/public/_redirects`) — without it `/alpine` and `/grains` 404 on
  reload.
- Domain, HTTPS, www → apex redirect, and a `support@` mailbox that a human reads.
- `og:image` and Twitter card per page, favicon, sitemap, `robots.txt`.
- Lighthouse pass, real mobile devices, Safari **and** Firefox — `backdrop-filter`
  and `aspect-ratio` are both used in the styles.
- Then: replace every `href="#"` Buy button with its LemonSqueezy checkout, and
  make the spec table match what you actually ship (see D3).

---

## What else you need, that is not on your list

1. **The name decision (D1).** Top of this document for a reason.
2. **EULA, privacy policy, terms.** Required by Netlify-adjacent reality, by
   Apple's notarisation paperwork, and by the first customer who asks.
3. **A manual.** `README.md` is already an excellent one — publish it as a docs
   site or a PDF in the installer rather than writing a second one.
4. **A support pipeline.** An inbox, canned answers for the ten questions you will
   be asked ten times, and a bug-report template.
5. **An uninstaller.**
6. **Trial or no trial** — decide it, do not default into it.
7. **Beta testers**, recruited before you need them.
8. **Launch assets** — press kit, a short product one-pager, banner images, and a
   list of people to send free codes to. This is the same list as your YouTuber
   codes; write it early.
9. **Archived builds per release.** Tag the commit, keep the signed artefact.
10. **An upgrade path** from a `$19` module to Alpine. Customers will ask
    immediately, and it is a per-customer discount code in LemonSqueezy.
11. **Bundle icons** — `.icns` for the Standalone apps, and an icon in the
    installer.
12. **Verify the macOS deployment target** actually matches the "11+" claim.
13. **VST3 subcategory metadata** so hosts file the plugins under the right
    headings.
14. **Parameter display names** — hosts surface them in automation lanes. Check
    they are readable and, once shipped, frozen.
15. **Published CPU figures**, so a reviewer does not invent their own.
16. **Accessibility**: keyboard access and text scaling in the WebView faces, and
    the native pedals' existing zoom range.
17. **A rollback story** for the first bad release, because there will be one.

---

## Gate summary

```
D1 D2 D3   decisions: name, catalogue, platform promise
   ↓
G1  correctness   golden param file · CI · pluginval 10 · auval strict ·
                  ASan/UBSan/TSan · both known failures closed · soak harness ·
                  preset-loader fuzzing
   ↓
G5a DSP content   plate engine · latency work        ← before checksums freeze
   ↓
G1b re-baseline   *_regress checksums frozen for the release
   ↓
G2  distribution  Developer ID · hardened runtime · notarised · signed .pkg ·
                  uninstaller · clean-machine install test
   ↓
G3  commerce      LemonSqueezy SKUs · licence keys · activation UI · free codes ·
                  trial decision
   ↓
G4  updates       updates.json + in-face notice · versioning · changelog · archive
   ↓
G5b content       presets to target · Grain store migration · preset browser
   ↓
G6  hosts         the host matrix, native and Rosetta
   ↓
G7  website       screenshots · audio · video · analytics · legal · domain · live
   ↓
beta → soft launch → launch
```

Two things are worth repeating because they are the ones most likely to be
skipped: **pluginval in CI before anything else**, and **the name decision before
the first sale**.
