#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <png.h>
#include <zlib.h>
#include <AHEasing/easing.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/putil.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORTED EMSCRIPTEN_KEEPALIVE
#else
#define EXPORTED
#endif

namespace {
int Opened=0,Closed=0;
UBool U_CALLCONV Open(const void*,void** FileContext,void** Contents,const char* Path){
    *FileContext=nullptr;*Contents=nullptr;
    FILE* File=std::fopen(Path,"rb");if(!File)return false;
    std::fseek(File,0,SEEK_END);long Size=std::ftell(File);std::rewind(File);
    if(Size<32){std::fclose(File);return false;}
    void* Data=std::malloc(Size);
    bool Read=Data&&std::fread(Data,1,Size,File)==static_cast<size_t>(Size);std::fclose(File);
    if(!Read){std::free(Data);return false;}
    *FileContext=Data;*Contents=Data;++Opened;return true;
}
void U_CALLCONV Close(const void*,void* FileContext,void* Contents){if(FileContext!=Contents)std::abort();std::free(Contents);++Closed;}
uint32_t Hash(const void* Bytes,size_t Size,uint32_t Value=2166136261u){for(size_t I=0;I<Size;++I)Value=(Value^static_cast<const uint8_t*>(Bytes)[I])*16777619u;return Value;}
uint32_t GlyphHash=2166136261u,ShapeHash=2166136261u;
unsigned Glyphs=0,LatinGlyphs=0,ArabicGlyphs=0;
bool Shape(FT_Face Face,const char* Text,hb_direction_t Direction,unsigned& Count){
    hb_font_t* Font=hb_ft_font_create_referenced(Face);
    hb_buffer_t* Buffer=hb_buffer_create();
    hb_buffer_add_utf8(Buffer,Text,-1,0,-1);hb_buffer_set_direction(Buffer,Direction);hb_buffer_guess_segment_properties(Buffer);
    hb_shape(Font,Buffer,nullptr,0);
    hb_glyph_info_t* Info=hb_buffer_get_glyph_infos(Buffer,&Count);
    hb_glyph_position_t* Position=hb_buffer_get_glyph_positions(Buffer,nullptr);
    bool Valid=Count>0;
    for(unsigned I=0;I<Count;++I){Valid &= Info[I].codepoint>0 && Position[I].x_advance>0;
        uint32_t Values[]={Info[I].codepoint,Info[I].cluster,uint32_t(Position[I].x_advance),uint32_t(Position[I].x_offset),uint32_t(Position[I].y_offset)};
        ShapeHash=Hash(Values,sizeof(Values),ShapeHash);
    }
    hb_buffer_destroy(Buffer);hb_font_destroy(Font);
    // Exercise the face-finalizer callback path as well as font ownership.
    hb_face_t* Cached=hb_ft_face_create_cached(Face);hb_face_destroy(Cached);
    return Valid;
}
}

extern "C" EXPORTED int slate_test(){
    GlyphHash=ShapeHash=2166136261u;Glyphs=LatinGlyphs=ArabicGlyphs=0;Opened=Closed=0;
    UErrorCode Status=U_ZERO_ERROR;
    u_setDataDirectory("/Content/Internationalization");udata_setFileAccess(UDATA_FILES_FIRST,&Status);
    u_setDataFileFunctions(nullptr,Open,Close,&Status);u_init(&Status);if(U_FAILURE(Status))return 1;
    FT_Library Library;if(FT_Init_FreeType(&Library))return 2;
    FT_Int Major,Minor,Patch;FT_Library_Version(Library,&Major,&Minor,&Patch);
    if(Major!=2||Minor!=14||Patch!=1)return 3;
    FT_Face Face;if(FT_New_Face(Library,"/Roboto-Regular.ttf",0,&Face))return 4;
    for(int Size:{12,24,48}){
        if(FT_Set_Pixel_Sizes(Face,0,Size))return 5;
        for(unsigned Character:{65u,103u,233u,937u}){
            if(FT_Load_Char(Face,Character,FT_LOAD_RENDER))return 6;
            const FT_Bitmap& Bitmap=Face->glyph->bitmap;
            if(!Bitmap.width||!Bitmap.rows||Bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY)return 7;
            uint32_t Geometry[]={Bitmap.width,Bitmap.rows,uint32_t(Face->glyph->advance.x)};
            GlyphHash=Hash(Geometry,sizeof(Geometry),GlyphHash);
            for(unsigned Y=0;Y<Bitmap.rows;++Y)GlyphHash=Hash(Bitmap.buffer+Y*Bitmap.pitch,Bitmap.width,GlyphHash);
            ++Glyphs;
        }
    }
    if(!Shape(Face,"office",HB_DIRECTION_LTR,LatinGlyphs)||LatinGlyphs>=6)return 8;
    FT_Done_Face(Face);
    if(FT_New_Face(Library,"/NotoNaskhArabicUI-Regular.ttf",0,&Face)||FT_Set_Pixel_Sizes(Face,0,32))return 9;
    if(!Shape(Face,"\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85",HB_DIRECTION_RTL,ArabicGlyphs)||ArabicGlyphs>=4)return 10;
    FT_Done_Face(Face);FT_Done_FreeType(Library);
    const uint8_t Pixels[]={255,0,0,255,0,255,0,128,0,0,255,255,37,92,145,64,0,0,0,0,255,255,255,255};
    png_image Image{};Image.version=PNG_IMAGE_VERSION;Image.width=3;Image.height=2;Image.format=PNG_FORMAT_RGBA;
    png_alloc_size_t Bytes=0;
    if(!png_image_write_to_memory(&Image,nullptr,&Bytes,0,Pixels,0,nullptr))return 11;
    void* Encoded=std::malloc(Bytes);
    if(!png_image_write_to_memory(&Image,Encoded,&Bytes,0,Pixels,0,nullptr))return 12;
    png_image Decoded{};Decoded.version=PNG_IMAGE_VERSION;
    if(!png_image_begin_read_from_memory(&Decoded,Encoded,Bytes))return 13;
    Decoded.format=PNG_FORMAT_RGBA;uint8_t Output[sizeof(Pixels)];
    if(Decoded.width!=3||Decoded.height!=2||!png_image_finish_read(&Decoded,nullptr,Output,0,nullptr)||std::memcmp(Pixels,Output,sizeof(Pixels)))return 14;
    png_image_free(&Decoded);
    static_cast<uint8_t*>(Encoded)[0]=0;
    Decoded={};Decoded.version=PNG_IMAGE_VERSION;
    if(png_image_begin_read_from_memory(&Decoded,Encoded,Bytes))return 15;
    png_image_free(&Decoded);std::free(Encoded);
    uint8_t Input[8192],Restored[8192],Compressed[9000];
    for(unsigned I=0;I<sizeof(Input);++I)Input[I]=uint8_t(I*37+(I>>3));
    uLongf CompressedSize=sizeof(Compressed),RestoredSize=sizeof(Restored);
    if(compress2(Compressed,&CompressedSize,Input,sizeof(Input),6)!=Z_OK)return 16;
    if(uncompress(Restored,&RestoredSize,Compressed,CompressedSize)!=Z_OK||RestoredSize!=sizeof(Input)||std::memcmp(Input,Restored,sizeof(Input)))return 17;
    for(int I=0;I<=64;++I){double T=I/64.0;
        if(std::abs(QuadraticEaseIn(T)-T*T)>1e-12 || std::abs(CubicEaseOut(T)-(1-std::pow(1-T,3)))>1e-12)return 18;
    }
    u_cleanup();if(Opened!=Closed)return 19;
    std::printf("{\"glyphs\":%u,\"glyphHash\":%u,\"shapeHash\":%u,\"latinGlyphs\":%u,\"arabicGlyphs\":%u,\"freetype\":\"%d.%d.%d\",\"harfbuzz\":\"%s\",\"png\":\"%s\",\"zlib\":\"%s\",\"pngBytesCompared\":%zu,\"compressionBytesCompared\":%zu,\"easingComparisons\":130}\n",Glyphs,GlyphHash,ShapeHash,LatinGlyphs,ArabicGlyphs,Major,Minor,Patch,hb_version_string(),PNG_LIBPNG_VER_STRING,zlibVersion(),sizeof(Pixels),sizeof(Input));
    return 0;
}
