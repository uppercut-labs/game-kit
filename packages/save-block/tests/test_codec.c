#include "sb/codec.h"
#include "check.h"

int main(void) {
    uint8_t b[8];

    /* single values: both byte orders, written and read back */
    sb_put_u16le(b, 0x1234u);
    CHECK(b[0] == 0x34u && b[1] == 0x12u && sb_get_u16le(b) == 0x1234u);
    sb_put_u16be(b, 0x1234u);
    CHECK(b[0] == 0x12u && b[1] == 0x34u && sb_get_u16be(b) == 0x1234u);
    sb_put_u32le(b, 0x89abcdefu);
    CHECK(b[0] == 0xefu && b[1] == 0xcdu && b[2] == 0xabu && b[3] == 0x89u && sb_get_u32le(b) == 0x89abcdefu);
    sb_put_u32be(b, 0x89abcdefu);
    CHECK(b[0] == 0x89u && b[1] == 0xabu && b[2] == 0xcdu && b[3] == 0xefu && sb_get_u32be(b) == 0x89abcdefu);
    sb_put_u32le(b, 0xffffffffu);
    CHECK(sb_get_u32le(b) == 0xffffffffu && sb_get_u32be(b) == 0xffffffffu);
    sb_put_u32le(b, 0u);
    CHECK(sb_get_u32le(b) == 0u);

    {
        /* a writer lays out a record field by field */
        uint8_t out[11];
        static const uint8_t blob[2] = { 0xaau, 0xbbu };
        sb_writer w;
        sb_writer_init(&w, out, sizeof(out));
        sb_writer_u8(&w, 1u);
        sb_writer_u16le(&w, 0x0302u);
        sb_writer_u16be(&w, 0x0405u);
        sb_writer_u32le(&w, 0x09080706u);
        CHECK(sb_writer_ok(&w) && w.length == 9u);
        sb_writer_bytes(&w, blob, 2u);
        CHECK(sb_writer_ok(&w) && w.length == 11u);
        {
            static const uint8_t want[11] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 0xaau, 0xbbu };
            CHECK(memcmp(out, want, sizeof(want)) == 0);
        }
    }
    {
        /* a write that does not fit writes nothing and the flag stays set, even for a later write that would fit */
        uint8_t out[5] = { 0xeeu, 0xeeu, 0xeeu, 0xeeu, 0xeeu };
        sb_writer w;
        sb_writer_init(&w, out, sizeof(out));
        sb_writer_u32le(&w, 0x04030201u);
        CHECK(sb_writer_ok(&w));
        sb_writer_u32le(&w, 0x08070605u);
        CHECK(!sb_writer_ok(&w) && w.length == 4u && out[4] == 0xeeu);
        sb_writer_u8(&w, 9u);
        CHECK(!sb_writer_ok(&w) && out[4] == 0xeeu);
    }
    {
        /* zero bytes, big-endian 32, and an exactly full buffer */
        uint8_t out[8] = { 0xeeu, 0xeeu, 0xeeu, 0xeeu, 0xeeu, 0xeeu, 0xeeu, 0xeeu };
        sb_writer w;
        sb_writer_init(&w, out, sizeof(out));
        sb_writer_zeros(&w, 4u);
        sb_writer_u32be(&w, 0x01020304u);
        CHECK(sb_writer_ok(&w) && w.length == 8u);
        CHECK(out[0] == 0u && out[3] == 0u && out[4] == 1u && out[7] == 4u);
        sb_writer_zeros(&w, 0u);
        CHECK(sb_writer_ok(&w));
        sb_writer_zeros(&w, 1u);
        CHECK(!sb_writer_ok(&w));
    }
    {
        /* a reader mirrors the writer */
        static const uint8_t in[11] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 0xaau, 0xbbu };
        uint8_t blob[2] = { 0u, 0u };
        sb_reader r;
        sb_reader_init(&r, in, sizeof(in));
        CHECK(sb_reader_u8(&r) == 1u);
        CHECK(sb_reader_u16le(&r) == 0x0302u);
        CHECK(sb_reader_u16be(&r) == 0x0405u);
        CHECK(sb_reader_u32le(&r) == 0x09080706u);
        sb_reader_bytes(&r, blob, 2u);
        CHECK(sb_reader_ok(&r) && r.position == 11u && blob[0] == 0xaau && blob[1] == 0xbbu);
    }
    {
        /* reading past the end returns zero, copies nothing and keeps the flag */
        static const uint8_t in[3] = { 1u, 2u, 3u };
        uint8_t out[4] = { 0x77u, 0x77u, 0x77u, 0x77u };
        sb_reader r;
        sb_reader_init(&r, in, sizeof(in));
        CHECK(sb_reader_u32le(&r) == 0u && !sb_reader_ok(&r) && r.position == 0u);
        CHECK(sb_reader_u8(&r) == 0u);
        sb_reader_bytes(&r, out, 2u);
        CHECK(out[0] == 0x77u && !sb_reader_ok(&r));
        sb_reader_init(&r, in, sizeof(in));
        sb_reader_skip(&r, 2u);
        CHECK(sb_reader_ok(&r) && sb_reader_u8(&r) == 3u);
        sb_reader_skip(&r, 1u);
        CHECK(!sb_reader_ok(&r));
        sb_reader_init(&r, in, sizeof(in));
        CHECK(sb_reader_u32be(&r) == 0u && !sb_reader_ok(&r));
    }
    return TEST_RESULT("codec");
}
