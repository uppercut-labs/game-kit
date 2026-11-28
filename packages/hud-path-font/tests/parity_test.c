/* Checks an earlier copy of this font against the package: the copy's hud_font.c, built from the directory named by
 * HUD_PATH_FONT_PARITY_DIRS, against hud_path_font_build for the glyph set the copy has. They pass when every ink call
 * matches in order (so any callback builds the same atlas), the atlases match texel for texel, the slot tables match,
 * and both give the same path for every character code.
 *
 *   hud-path-font-parity <label>
 */
#include "hud_font.h"   /* the earlier copy: hud_font_build(slot, put_ink) and hud_font_path(code) */
#include "hud_path_font/hud_path_font.h"

#include "check.h"

static Recording earlier, package;

int main(int argc, char **argv)
{
    const char *label = argc > 1 ? argv[1] : "earlier copy";
    int8_t earlier_slot[128], package_slot[128];

    recording_reset(&earlier);
    recording_target = &earlier;
    hud_font_build(earlier_slot, record_ink);

    uint8_t glyphs = 0;
    for (uint8_t code = 0; code < 128; ++code)
        glyphs = (uint8_t)(glyphs + (earlier_slot[code] >= 0));
    CHECK(glyphs == HUD_PATH_FONT_CORE_GLYPHS || glyphs == HUD_PATH_FONT_ALL_GLYPHS);

    recording_reset(&package);
    recording_target = &package;
    hud_path_font_build(package_slot, record_ink, glyphs);

    CHECK(!earlier.overflow && !package.overflow);
    CHECK(earlier.count == package.count);
    CHECK(earlier.digest == package.digest);
    uint32_t shared = earlier.count < package.count ? earlier.count : package.count;
    CHECK(memcmp(earlier.calls, package.calls, (shared < RECORD_LIMIT ? shared : RECORD_LIMIT) * sizeof(InkCall)) == 0);
    CHECK(memcmp(earlier.atlas, package.atlas, sizeof earlier.atlas) == 0);
    CHECK(memcmp(earlier_slot, package_slot, sizeof earlier_slot) == 0);

    uint8_t paths_match = 1;
    for (uint8_t code = 0; code < 128; ++code) {
        const char *a = hud_font_path(code), *b = hud_path_font_path(code, glyphs);
        if (!a != !b || (a && strcmp(a, b) != 0)) {
            fprintf(stderr, "path for code %u differs\n", (unsigned)code);
            paths_match = 0;
        }
    }
    CHECK(paths_match);

    printf("%s: %u glyphs, %u ink calls, call digest 0x%016llx, atlas digest 0x%016llx\n", label, (unsigned)glyphs,
           (unsigned)earlier.count, (unsigned long long)earlier.digest,
           (unsigned long long)fnv1a(FNV_START, earlier.atlas, sizeof earlier.atlas));
    return TEST_RESULT(label);
}
