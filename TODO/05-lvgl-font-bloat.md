# 05 — LVGL font bloat: `_aligned` duplicate eats ~30 KB of flash

Surfaced while diagnosing a perceived flash-size jump since
[e9ab857](../) ("remove NEWLIBC to save Flash"). The jump wasn't actually
from anything that landed in that range — but the flash budget is tight
enough that a separately-noticed ~44 KB of font data is worth claiming back.

## What's in the binary today (52dk build)

The Loki firmware enables exactly one Montserrat size:

```
# loki_app.conf transitively gives us this via the display config
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_UNSCII_8=y
CONFIG_LV_FONT_DEFAULT_UNSCII_8=y
```

Inspecting the linker map confirms two distinct Montserrat-14 objects in
the ELF:

| .obj | Linked size | Notes |
|---|---|---|
| `lv_font_montserrat_14_aligned.c.obj` | **30,136 B** | Heavy variant — includes kerning class tables (`kern_class_values` alone is 0xBAD ≈ 3 KB) |
| `lv_font_montserrat_14.c.obj`         | **13,752 B** | Lean variant — same glyphs, no kerning |
| **Sum**                               | **~44 KB**  | One is dead weight for our use case |

Both files live in
`/home/alex/ncs/v3.2.4/modules/lib/gui/lvgl/src/font/` and are real,
hand-generated font tables — neither is a thin wrapper.

The ~30 other font stubs (unscii_16, montserrat_8..48 except 14, CJK,
Persian-Hebrew, lv_font_manager*, …) cost about 111 bytes each — their
bodies compile out under `#ifdef LV_USE_FONT_MONTSERRAT_*` but the linker
still emits a stub entry per object because NCS pulls the lvgl archive
with `--whole-archive`.

## Why both `_14` variants get linked

The Zephyr-side glue in
[`<NCS>/zephyr/modules/lvgl/CMakeLists.txt`](file:///home/alex/ncs/v3.2.4/zephyr/modules/lvgl/CMakeLists.txt) lists both files
unconditionally:

```cmake
zephyr_library_sources(
    ...
    ${LVGL_DIR}/src/font/lv_font_montserrat_14.c
    ${LVGL_DIR}/src/font/lv_font_montserrat_14_aligned.c
    ...
)
```

There is **no** `CONFIG_LV_FONT_MONTSERRAT_14_ALIGNED` Kconfig knob — the
file is shipped, compiled, and linked whenever `LV_FONT_MONTSERRAT_14=y`,
regardless of whether the firmware ever references it. With Zephyr's
`--whole-archive` linkage for module libraries, every object in
`libmodules__lvgl.a` ends up in the final ELF.

For our SSD1306 (128×64 mono) we have no use for kerning. The `_aligned`
30 KB is pure dead weight.

## Remediation paths, ranked

### A. Drop Montserrat entirely; rely on the unscii fonts already linked — **biggest win, easiest**

`CONFIG_LV_FONT_UNSCII_8=y` is already on (and is `LV_FONT_DEFAULT`).
`unscii_8` is a pixel-perfect 8×8 mono font — a natural fit for SSD1306
at our text sizes, and it adds only ~1.5 KB. If we don't need Montserrat
specifically (i.e. the display code doesn't `lv_label_set_text` with a
Montserrat-styled label), the entire Montserrat-14 pair can go away.

Action: flip in the appropriate overlay (e.g. `display-ssd1306.conf`):

```
CONFIG_LV_FONT_MONTSERRAT_14=n
```

Audit display code first — `grep -rn "montserrat\|MONTSERRAT_14" src/displays/`.
If nothing references it, the flip is free. **Expected saving: ~44 KB.**

### B. Keep Montserrat-14 but exclude the `_aligned` variant — **medium win, in-tree**

If we want Montserrat for nicer label rendering at one size and don't want
to touch the upstream LVGL CMake glue, surgically remove the `_aligned`
source from the lvgl Zephyr-library target from inside our own
[`CMakeLists.txt`](../CMakeLists.txt). After the Zephyr build system has
configured the lvgl module:

```cmake
# Drop the _aligned variant — its 3 KB of kerning class data is dead
# weight for our SSD1306 use, and it ships alongside the lean
# lv_font_montserrat_14.c whenever LV_FONT_MONTSERRAT_14=y.
get_target_property(_lvgl_sources lvgl SOURCES)
list(FILTER _lvgl_sources EXCLUDE REGEX "lv_font_montserrat_14_aligned\\.c$")
set_property(TARGET lvgl PROPERTY SOURCES "${_lvgl_sources}")
```

Caveat: must run **after** the Zephyr `lvgl` module CMakeLists has
populated the source list. Right after `find_package(Zephyr)` is usually
correct. Verify with `cmake --build … --target lvgl` and check the
.obj is no longer compiled.

Also need to verify nothing references the `lv_font_montserrat_14_aligned`
symbol — if LVGL internally selects the aligned variant under some build
flag, removing the source will produce a link error rather than silently
shrinking the binary. (Quick check: `nm` the resulting ELF for the symbol;
if it's still requested, switch to remediation C below.)

**Expected saving: ~30 KB.**

### C. Configure LVGL to prefer one variant via lv_conf.h — **principled, fragile across NCS upgrades**

LVGL's own conf system has a `LV_FONT_FMT_TXT_LARGE` / `LV_FONT_SUBPX_*`
family of switches that influence which font format gets generated, but
there's no clean "use the lean variant, ignore aligned" switch in
upstream's `lv_conf_template.h` for our NCS version. The aligned variant
is shipped pre-built and selected by the `_aligned` filename — there's
no runtime selector.

This option exists in principle but isn't tractable without forking a
generated font file. Skip unless A and B are blocked.

### D. Upstream fix: file a Zephyr/NCS issue — **right long-term, slow**

The Zephyr module CMakeLists should gate `lv_font_montserrat_14_aligned.c`
behind its own Kconfig (`CONFIG_LV_FONT_MONTSERRAT_14_ALIGNED`) so the
caller chooses. Worth filing at
<https://github.com/nrfconnect/sdk-zephyr> or upstream LVGL — but won't
help our current builds. Track but don't block.

## Recommendation

Run **A first** (one-line conf change, audit display code for Montserrat
references — likely none). If A reveals that some specific label actually
needs Montserrat-14, fall back to **B**. The CMake snippet in B is
contained in our own tree, survives NCS minor-version bumps as long as
Zephyr keeps `zephyr_library_sources` and the per-target SOURCES property
working the same way, and reclaims the bigger of the two duplicate fonts.

## Verification

For either approach:

1. `west build` clean, with the change applied.
2. `arm-none-eabi-size build_52dk/coap_loki/zephyr/zephyr.elf` — compare
   `text` against the pre-change baseline.
3. `grep "lv_font_montserrat_14" build_52dk/coap_loki/zephyr/zephyr.map`
   — confirm the `_aligned` line is gone (option B) or both are gone
   (option A).
4. Flash to a 52dk and visually verify the on-screen text still renders
   correctly. If A: every Montserrat label falls back to unscii_8 (smaller,
   blockier — acceptable on SSD1306).

## Why this didn't *cause* the e9ab857-era flash jump

Worth recording, since I went hunting for it: the on-disk
`build_custom/` ELF from 2026-06-13 17:35 (one minute *before* e9ab857)
has **zero** `lv_font_*` content — the `custom` board target uses
`no_display.c`, so the LVGL config is absent and the font sources are
never compiled. The 167 KB difference between `build_custom` (576 KB
text) and `build_52dk` (743 KB text) is the entire LVGL + display stack
that the custom board strips out, not a regression from any commit in
the e9ab857..HEAD range. This duplicate-font issue is a real bloat
finding, but pre-dates e9ab857 — it's just become more visible now that
flash usage is closer to the 1 MB ceiling.

## Open questions

- **Does the display code use Montserrat at all?** If not, remediation
  A wins and B is moot. Quick audit: `grep -rn "lv_font_montserrat\|&lv_font_default" src/displays/`.
- **Is `_aligned` semantically distinct from the lean variant?** Both
  produce the same glyphs; `_aligned` adds kerning data and a 4-byte-
  aligned glyph table. For 128×64 mono, neither feature is visible. If
  some downstream LVGL widget *requires* the aligned variant by symbol
  reference, B's CMake exclusion would surface a link error during the
  Verify step, not a silent regression.
