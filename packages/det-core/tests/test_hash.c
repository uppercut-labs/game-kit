#include <string.h>
#include "det/hash.h"
#include "check.h"

static uint32_t of_text(const char* text) {
    return det_fnv1a32((const uint8_t*)text, strlen(text));
}

int main(void) {
    /* published FNV-1a 32-bit test vectors */
    CHECK(det_hash32_begin() == 0x811c9dc5u);
    CHECK(det_fnv1a32(NULL, 0u) == 0x811c9dc5u);
    CHECK(of_text("a") == 0xe40c292cu);
    CHECK(of_text("b") == 0xe70c2de5u);
    CHECK(of_text("foobar") == 0xbf9cf968u);

    {
        /* pieces give the same answer as the joined bytes */
        const uint8_t whole[] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u };
        uint32_t split = det_hash32_begin();
        split = det_hash32_bytes(split, whole, 3u);
        split = det_hash32_bytes(split, whole + 3, 4u);
        CHECK(split == det_fnv1a32(whole, sizeof(whole)));
    }
    {
        /* the multi-byte helpers fold little-endian, whatever the host is */
        const uint8_t le32[] = { 0x78u, 0x56u, 0x34u, 0x12u };
        const uint8_t le16[] = { 0x34u, 0x12u };
        CHECK(det_hash32_u32(det_hash32_begin(), 0x12345678u) == det_fnv1a32(le32, 4u));
        CHECK(det_hash32_u16(det_hash32_begin(), 0x1234u) == det_fnv1a32(le16, 2u));
        CHECK(det_hash32_i32(det_hash32_begin(), -1) == det_hash32_u32(det_hash32_begin(), 0xffffffffu));
        CHECK(det_hash32_u8(det_hash32_begin(), 0x61u) == of_text("a"));
    }
    {
        /* a pinned value for a 4-byte fold, so a change to the order is caught even if both sides move together */
        const uint8_t bytes[] = { 0x78u, 0x56u, 0x34u, 0x12u };
        CHECK(det_fnv1a32(bytes, sizeof(bytes)) == 0xa3649785u);
    }
    return TEST_RESULT("hash");
}
