#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../stb/stb_truetype.h"

using FT_Byte = std::uint8_t;
using FT_Int = int;
using FT_UInt = unsigned int;
using FT_Long = long;
using FT_ULong = unsigned long;
using FT_Pos = long;
using FT_Bool = int;
using FT_Error = int;

#define FT_LOAD_DEFAULT 0x0
#define FT_LOAD_RENDER 0x1
#define FT_LOAD_TARGET_NORMAL 0x2
#define FT_LOAD_FORCE_AUTOHINT 0x4

#define FT_SIZE_REQUEST_TYPE_REAL_DIM 0

struct FT_Bitmap
{
    unsigned int rows{};
    unsigned int width{};
    int pitch{};
    unsigned char* buffer{};
};

struct FT_Vector
{
    FT_Pos x{};
    FT_Pos y{};
};

struct FT_GlyphSlotRec;
using FT_GlyphSlot = FT_GlyphSlotRec*;

struct FT_GlyphSlotRec
{
    FT_Bitmap bitmap{};
    std::vector<unsigned char> owned_bitmap{};
    FT_Vector advance{};
    int bitmap_left{};
    int bitmap_top{};
};

struct FT_Size_Metrics
{
    long height{};
    long y_scale{};
};

struct FT_SizeRec
{
    FT_Size_Metrics metrics{};
};

using FT_Size = FT_SizeRec*;

struct FT_FaceRec;
using FT_Face = FT_FaceRec*;

struct FT_FaceRec
{
    float pixel_size{ 16.0f };
    FT_Long ascender{};
    FT_Long descender{};
    FT_Long height{};
    FT_Size size{};
    FT_GlyphSlot glyph{};

    stbtt_fontinfo font_info{};
    const FT_Byte* font_data{};
    FT_Long font_data_size{};
    FT_Long font_index{};
    float scale{};
    int font_ascent{};
    int font_descent{};
    int font_line_gap{};
};

struct FT_LibraryRec
{
    int dummy{};
};

using FT_Library = FT_LibraryRec*;

struct FT_Size_RequestRec
{
    FT_Int type{};
    FT_Long height{};
};

namespace ft_local {

inline const char* glyph_rows( char32_t cp )
{
    switch ( cp )
    {
    case U'0': return "111101101101101101111";
    case U'1': return "010110010010010010111";
    case U'2': return "111001001111100100111";
    case U'3': return "111001001111001001111";
    case U'4': return "101101101111001001001";
    case U'5': return "111100100111001001111";
    case U'6': return "111100100111101101111";
    case U'7': return "111001001010010010010";
    case U'8': return "111101101111101101111";
    case U'9': return "111101101111001001111";

    case U'a': case U'A': return "010101101111101101101";
    case U'b': case U'B': return "110101101110101101110";
    case U'c': case U'C': return "011100100100100100011";
    case U'd': case U'D': return "110101101101101101110";
    case U'e': case U'E': return "111100100110100100111";
    case U'f': case U'F': return "111100100110100100100";
    case U'g': case U'G': return "011100100101101101011";
    case U'h': case U'H': return "101101101111101101101";
    case U'i': case U'I': return "111010010010010010111";
    case U'j': case U'J': return "001001001001101101010";
    case U'k': case U'K': return "101101110100110101101";
    case U'l': case U'L': return "100100100100100100111";
    case U'm': case U'M': return "101111111101101101101";
    case U'n': case U'N': return "101111111111101101101";
    case U'o': case U'O': return "010101101101101101010";
    case U'p': case U'P': return "110101101110100100100";
    case U'q': case U'Q': return "010101101101101111011";
    case U'r': case U'R': return "110101101110110101101";
    case U's': case U'S': return "011100100010001001110";
    case U't': case U'T': return "111010010010010010010";
    case U'u': case U'U': return "101101101101101101111";
    case U'v': case U'V': return "101101101101101010010";
    case U'w': case U'W': return "101101101101111111101";
    case U'x': case U'X': return "101101010010010101101";
    case U'y': case U'Y': return "101101101010010010010";
    case U'z': case U'Z': return "111001001010100100111";

    case U'.': return "000000000000000010010";
    case U',': return "000000000000000010100";
    case U':': return "000010010000010010000";
    case U';': return "000010010000010010100";
    case U'!': return "010010010010010000010";
    case U'?': return "111001001010010000010";
    case U'-': return "000000000111000000000";
    case U'_': return "000000000000000000111";
    case U'+': return "000010010111010010000";
    case U'/': return "001001010010010100100";
    case U'\\': return "100100010010010001001";
    case U'(': return "001010100100100010001";
    case U')': return "100010001001001010100";
    case U'[': return "111100100100100100111";
    case U']': return "111001001001001001111";
    case U'<': return "001010100100010001000";
    case U'>': return "100010001001100010000";
    case U'=': return "000000111000111000000";
    case U'*': return "000101010111010101000";
    case U'%': return "101001010010010100101";
    case U'#': return "101101111101111101101";
    case U' ': return "000000000000000000000";
    default: return "111101001010010000010";
    }
}

} // namespace ft_local

inline FT_Pos FT_MulFix(FT_Pos a, FT_Pos b)
{
    return a * b / 64;
}

inline FT_Error FT_Init_FreeType(FT_Library* alibrary)
{
    if ( !alibrary )
    {
        return 1;
    }

    *alibrary = new FT_LibraryRec{};
    return 0;
}

inline void FT_Done_FreeType(FT_Library library)
{
    delete library;
}

inline FT_Error FT_New_Memory_Face(FT_Library, const FT_Byte* data, FT_Long size, FT_Long index, FT_Face* aface)
{
    if ( !aface || !data || size <= 0 )
    {
        return 1;
    }

    auto* face = new FT_FaceRec{};
    face->size = new FT_SizeRec{};
    face->glyph = new FT_GlyphSlotRec{};
    face->font_data = data;
    face->font_data_size = size;
    face->font_index = index;

    const int font_offset = stbtt_GetFontOffsetForIndex( data, static_cast<int>( index ) );
    if ( stbtt_InitFont( &face->font_info, data, font_offset ) != 0 )
    {
        *aface = face;
        return 0;
    }

    delete face->glyph;
    delete face->size;
    delete face;
    return 1;
}

inline void FT_Done_Face(FT_Face face)
{
    if ( !face )
    {
        return;
    }

    delete face->glyph;
    delete face->size;
    delete face;
}

inline FT_Error FT_Request_Size(FT_Face face, FT_Size_RequestRec* req)
{
    if ( !face || !face->size )
    {
        return 1;
    }

    if ( req && req->height > 0 )
    {
        face->pixel_size = static_cast<float>( req->height ) / 64.0f;
    }

    face->scale = stbtt_ScaleForPixelHeight( &face->font_info, face->pixel_size );
    stbtt_GetFontVMetrics( &face->font_info, &face->font_ascent, &face->font_descent, &face->font_line_gap );

    face->ascender = static_cast<FT_Long>( face->font_ascent * face->scale * 64.0f );
    face->descender = static_cast<FT_Long>( face->font_descent * face->scale * 64.0f );
    face->height = static_cast<FT_Long>( ( face->font_ascent - face->font_descent + face->font_line_gap ) * face->scale * 64.0f );
    face->size->metrics.height = face->height;
    face->size->metrics.y_scale = 64;
    return 0;
}

inline FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong cp)
{
    if ( !face )
    {
        return 0;
    }

    if ( cp > INT_MAX )
    {
        return 0;
    }

    return static_cast<FT_UInt>( stbtt_FindGlyphIndex( &face->font_info, static_cast<int>( cp ) ) );
}

inline FT_Error FT_Load_Char(FT_Face face, FT_ULong cp, FT_Int)
{
    if ( !face || !face->glyph )
    {
        return 1;
    }

    const auto glyph_index = stbtt_FindGlyphIndex( &face->font_info, static_cast<int>( cp ) );
    if ( glyph_index == 0 )
    {
        return 1;
    }

    int width = 0;
    int height = 0;
    int xoff = 0;
    int yoff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap( &face->font_info, face->scale, face->scale, static_cast<int>( cp ), &width, &height, &xoff, &yoff );
    if ( !bitmap && ( width != 0 || height != 0 ) )
    {
        return 1;
    }

    auto& glyph = *face->glyph;
    const auto pitch = width;
    glyph.owned_bitmap.assign( static_cast<std::size_t>( pitch ) * static_cast<std::size_t>( height ), 0 );
    if ( bitmap )
    {
        std::memcpy( glyph.owned_bitmap.data( ), bitmap, static_cast<std::size_t>( pitch ) * static_cast<std::size_t>( height ) );
        stbtt_FreeBitmap( bitmap, nullptr );
    }

    int advance = 0;
    int left_side_bearing = 0;
    stbtt_GetCodepointHMetrics( &face->font_info, static_cast<int>( cp ), &advance, &left_side_bearing );

    glyph.bitmap.rows = static_cast<unsigned int>( height );
    glyph.bitmap.width = static_cast<unsigned int>( width );
    glyph.bitmap.pitch = pitch;
    glyph.bitmap.buffer = glyph.owned_bitmap.data( );
    glyph.advance.x = static_cast<FT_Pos>( advance * face->scale * 64.0f );
    glyph.advance.y = 0;
    glyph.bitmap_left = xoff;
    glyph.bitmap_top = -yoff;
    return 0;
}
