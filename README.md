# LV2-renku

> This repository is the source of the RenkuOS package `lv2_renku`: the LV2
> specification bundles every LV2 host needs at runtime, the hosts `lv2host` and
> `lv2guihost`, the lilv tools and the hkamp example plug-in. It replaces the old
> `lv2_haiku` package. [Kasen](https://github.com/rations/kasen), the RenkuOS DAW, and
> [rktuner](https://github.com/rations/rktuner) require it.

Native port of the **LV2 plugin hosting stack** to **RenkuOS** (a Haiku fork).

This repo vendors the upstream drobilla suite pristine under `lv2stack/` — the
[LV2](https://lv2plug.in) specification 1.18.11, zix 0.8.1, serd 0.32.10, sord 0.16.22,
sratom 0.6.22, and lilv 0.28.0 — and builds it natively as static libraries with each
project's own unmodified meson build. On top of that it adds:

- **`hosts/lv2host`** — a headless host with a stdin prompt for parameters and inputs,
  for running and validating LV2 plug-ins live on RenkuOS hardware:
  `lv2host [--backend mediakit|jack] <plugin-uri>`.
- **`hosts/lv2guihost`** — the same with the plug-in's own UI (HaikuUI or
  `ui:showInterface`).

  Both play through the RenkuOS Media Kit by default: one media node, patched in Cortex,
  whose inputs are connected from the sound card that runs the clock. With JACK installed,
  `--backend jack` runs them as a JACK client instead. The audio backends are Kasen's,
  copied unchanged into `jdbackend/` by `tools/sync-jdbackend.sh`; JACK support is the
  `jack.so` add-on (`add-ons/lv2host/`), so the hosts never link libjack. The hosting core
  and UI host are also staged as `liblv2host.a` for apps built on this stack (rktuner).
- **`ns/ui-haiku.lv2`** — a small LV2 extension bundle defining the **HaikuUI** UI type:
  a plugin UI whose `LV2UI_Widget` is a native `BView*`, exactly parallel to the spec's
  CocoaUI (`NSView*`). Plugins built for RenkuOS can ship native Interface Kit editors;
  `ui:showInterface` (UI owns its own `BWindow`) is supported as well.
- **`plugins/hkamp.lv2`** — a simple stereo amplifier plugin (with a native BView UI) used
  as the milestone test plugin.

Plugins install to `/boot/home/config/non-packaged/add-ons/media/LV2/` (user) or
`/boot/system/non-packaged/add-ons/media/LV2/` (system); the default `LV2_PATH` covers those
plus `/boot/system/add-ons/media/LV2`. No dot-folders — RenkuOS organises per-user files under
the `~/config` hierarchy.

## Building (on RenkuOS)

```sh
make deps       # pkgman install -y meson ninja pkgconfig
make            # builds the stack into build/stage + hosts + hkamp.lv2
make install-specs install   # LV2 spec bundles + hkamp into ~/config/non-packaged/add-ons/media/LV2
make validate   # lv2ls / lv2info smoke checks
```

The stack also builds unmodified on Linux (useful as a portability check); the resulting
static libraries and headers land in `build/stage/{lib,include}` and the lilv tools
(`lv2ls`, `lv2info`, `lv2apply`) in `build/stage/bin`.

## License

ISC — see `LICENSE`. The vendored upstream projects under `lv2stack/` are ISC/0BSD by
David Robillard and contributors; see their `COPYING` files.
