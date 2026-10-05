// Dependency portability checks only; this does not emulate Unreal subsystems.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <unicode/ustring.h>
#include <unicode/uversion.h>
#include "easing.h"
#include <cstdio>
#include <cstring>
#include <cmath>

int main() {
    static_assert(sizeof(void*) == 8, "This check targets Wasm64");
    const char* input = "Caf\xc3\xa9 \xf0\x9f\x8e\xae";
    UChar utf16[32]; int32_t length = 0; UErrorCode status = U_ZERO_ERROR;
    u_strFromUTF8(utf16, 32, &length, input, -1, &status);
    if (U_FAILURE(status) || length != 7) return 1;
    char roundtrip[64]; int32_t bytes = 0;
    u_strToUTF8(roundtrip, 64, &bytes, utf16, length, &status);
    if (U_FAILURE(status) || std::strcmp(input, roundtrip)) return 2;
    status = U_ZERO_ERROR;
    u_strFromUTF8(utf16, 32, &length, "\xff", 1, &status);
    if (!U_FAILURE(status)) return 3;

    FT_Library library; FT_Face face;
    if (FT_Init_FreeType(&library)) return 4;
    if (FT_New_Face(library, "/Roboto-Regular.ttf", 0, &face)) return 5;
    if (FT_Set_Pixel_Sizes(face, 0, 32) || FT_Load_Char(face, 'A', FT_LOAD_RENDER)) return 6;
    const unsigned width = face->glyph->bitmap.width, rows = face->glyph->bitmap.rows;
    if (!width || !rows) return 7;
    bool pixels = false;
    for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < width; ++x)
        pixels |= face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x] != 0;
    if (!pixels) return 8;
    int major, minor, patch; FT_Library_Version(library, &major, &minor, &patch);
    FT_Done_Face(face); FT_Done_FreeType(library);

    const AHEasingFunction functions[] = {QuadraticEaseIn, CubicEaseInOut, SineEaseInOut, BounceEaseOut};
    for (auto fn : functions) {
        if (std::abs(fn(0)) > 1e-6 || std::abs(fn(1)-1) > 1e-6 || !std::isfinite(fn(0.5))) return 9;
    }
    std::printf("{\"passed\":true,\"wasm64\":true,\"icu\":\"%s\",\"freetype\":\"%d.%d.%d\","
                "\"unicodeRoundtrip\":true,\"invalidUtf8Rejected\":true,\"glyphWidth\":%u,\"glyphRows\":%u,"
                "\"easingFunctionsChecked\":4,\"ahFloatBytes\":%zu,\"unrealIntegrated\":false}\n",
                U_ICU_VERSION, major, minor, patch, width, rows, sizeof(AHFloat));
    return 0;
}
