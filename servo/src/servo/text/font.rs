export Font, test_font_bin, create_test_font;

use glyph::GlyphIndex;
use vec_to_ptr = vec::raw::to_ptr;
use libc::{ c_int, c_double, c_ulong };
use ptr::{ null, addr_of };
use native_font::NativeFont;
use font_library::FontLibrary;

#[doc = "
A font handle. Layout can use this to calculate glyph metrics
and the renderer can use it to render text.
"]
struct Font {
    // A back reference to keep the library alive
    lib: @FontLibrary,
    fontbuf: @~[u8],
    native_font: NativeFont,
}

impl Font {
    fn buf() -> @~[u8] {
        self.fontbuf
    }

    fn glyph_index(codepoint: char) -> Option<GlyphIndex> {
        self.native_font.glyph_index(codepoint)
    }

    fn glyph_h_advance(glyph: GlyphIndex) -> int {
        match self.native_font.glyph_h_advance(glyph) {
          Some(adv) => adv,
          None => /* FIXME: Need fallback strategy */ 10
        }
    }
}

fn Font(lib: @FontLibrary, fontbuf: @~[u8], +native_font: NativeFont) -> Font {
    Font {
        lib: lib,
        fontbuf : fontbuf,
        native_font : move native_font,
    }
}

const TEST_FONT: [u8 * 33004] = #include_bin("JosefinSans-SemiBold.ttf");

fn test_font_bin() -> ~[u8] {
    return vec::from_fn(33004, |i| TEST_FONT[i]);
}

fn should_destruct_on_fail_without_leaking() {
    #[test];
    #[should_fail];

    let lib = FontLibrary();
    let _font = lib.get_test_font();
    fail;
}

fn should_get_glyph_indexes() {
    #[test];

    let lib = FontLibrary();
    let font = lib.get_test_font();
    let glyph_idx = font.glyph_index('w');
    assert glyph_idx == Some(40u);
}

fn should_get_glyph_advance() {
    #[test];

    let lib = FontLibrary();
    let font = lib.get_test_font();
    let x = font.glyph_h_advance(40u);
    assert x == 15;
}

// Testing thread safety
fn should_get_glyph_advance_stress() {
    #[test];

    let mut ports = ~[];

    for iter::repeat(100) {
        let (chan, port) = pipes::stream();
        ports += [@move port];
        do task::spawn {
            let lib = FontLibrary();
            let font = lib.get_test_font();
            let x = font.glyph_h_advance(40u);
            assert x == 15;
            chan.send(());
        }
    }

    for ports.each |port| {
        port.recv();
    }
}

fn should_be_able_to_create_instances_in_multiple_threads() {
    #[test];

    for iter::repeat(10u) {
        do task::spawn {
            let lib = FontLibrary();
            let _font = lib.get_test_font();
        }
    }
}

