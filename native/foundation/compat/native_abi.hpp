#pragma once
// Narrow public ABI declarations, not an SDL/FreeType implementation.
// Desktop bootstrap only; target builds require the actual SDK headers.
#ifdef SPICE_SYSTEM_HEADERS
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#else
#include <cstdint>
using Uint8=uint8_t; using Uint16=uint16_t; using Uint32=uint32_t; using Sint32=int32_t; using Uint64=uint64_t;
struct SDL_Window; struct SDL_GameController; struct SDL_Joystick;
using SDL_GLContext=void*; using SDL_Scancode=int; using SDL_Keycode=int32_t; using SDL_JoystickID=int32_t;
struct SDL_version { Uint8 major,minor,patch; };
struct SDL_DisplayMode { Uint32 format; int w,h,refresh_rate; void* driverdata; };
struct SDL_Keysym { SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; Uint32 unused; };
struct SDL_WindowEvent { Uint32 type,timestamp,windowID; Uint8 event,padding1,padding2,padding3; Sint32 data1,data2; };
struct SDL_KeyboardEvent { Uint32 type,timestamp,windowID; Uint8 state,repeat,padding2,padding3; SDL_Keysym keysym; };
struct SDL_TextInputEvent { Uint32 type,timestamp,windowID; char text[32]; };
struct SDL_MouseMotionEvent { Uint32 type,timestamp,windowID,which,state; Sint32 x,y,xrel,yrel; };
struct SDL_MouseButtonEvent { Uint32 type,timestamp,windowID,which; Uint8 button,state,clicks,padding1; Sint32 x,y; };
struct SDL_MouseWheelEvent { Uint32 type,timestamp,windowID,which; Sint32 x,y; Uint32 direction; float preciseX,preciseY; Sint32 mouseX,mouseY; };
struct SDL_ControllerButtonEvent { Uint32 type,timestamp; SDL_JoystickID which; Uint8 button,state,padding1,padding2; };
struct SDL_ControllerAxisEvent { Uint32 type,timestamp; SDL_JoystickID which; Uint8 axis,padding1,padding2,padding3; int16_t value; Uint16 padding4; };
struct SDL_ControllerDeviceEvent { Uint32 type,timestamp; Sint32 which; };
union SDL_Event { Uint32 type; SDL_WindowEvent window; SDL_KeyboardEvent key; SDL_TextInputEvent text; SDL_MouseMotionEvent motion; SDL_MouseButtonEvent button; SDL_MouseWheelEvent wheel; SDL_ControllerButtonEvent cbutton; SDL_ControllerAxisEvent caxis; SDL_ControllerDeviceEvent cdevice; void* alignment; Uint8 padding[56]; };
static_assert(sizeof(SDL_Event)==56);
enum SDL_GLattr { SDL_GL_RED_SIZE=0,SDL_GL_GREEN_SIZE,SDL_GL_BLUE_SIZE,SDL_GL_ALPHA_SIZE,SDL_GL_BUFFER_SIZE,SDL_GL_DOUBLEBUFFER,SDL_GL_DEPTH_SIZE,SDL_GL_STENCIL_SIZE,SDL_GL_ACCUM_RED_SIZE,SDL_GL_ACCUM_GREEN_SIZE,SDL_GL_ACCUM_BLUE_SIZE,SDL_GL_ACCUM_ALPHA_SIZE,SDL_GL_STEREO,SDL_GL_MULTISAMPLEBUFFERS,SDL_GL_MULTISAMPLESAMPLES,SDL_GL_ACCELERATED_VISUAL,SDL_GL_RETAINED_BACKING,SDL_GL_CONTEXT_MAJOR_VERSION,SDL_GL_CONTEXT_MINOR_VERSION,SDL_GL_CONTEXT_EGL,SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_PROFILE_MASK };
constexpr int SDL_GL_CONTEXT_PROFILE_ES=4, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY=2;
constexpr Uint32 SDL_INIT_VIDEO=0x20,SDL_INIT_GAMECONTROLLER=0x2000;
constexpr Uint32 SDL_WINDOW_OPENGL=2,SDL_WINDOW_SHOWN=4,SDL_WINDOW_HIDDEN=8,SDL_WINDOW_RESIZABLE=0x20,SDL_WINDOW_ALLOW_HIGHDPI=0x2000,SDL_WINDOW_FULLSCREEN_DESKTOP=0x1001;
constexpr int SDL_WINDOWPOS_CENTERED=0x2fff0000;
constexpr Uint32 SDL_QUIT=0x100,SDL_WINDOWEVENT=0x200,SDL_KEYDOWN=0x300,SDL_TEXTINPUT=0x303,SDL_MOUSEMOTION=0x400,SDL_MOUSEBUTTONDOWN=0x401,SDL_MOUSEWHEEL=0x403,SDL_CONTROLLERAXISMOTION=0x650,SDL_CONTROLLERBUTTONDOWN=0x651,SDL_CONTROLLERDEVICEADDED=0x653,SDL_CONTROLLERDEVICEREMOVED=0x654;
constexpr Uint8 SDL_WINDOWEVENT_SHOWN=1,SDL_WINDOWEVENT_HIDDEN=2,SDL_WINDOWEVENT_RESIZED=5,SDL_WINDOWEVENT_SIZE_CHANGED=6,SDL_WINDOWEVENT_MINIMIZED=7,SDL_WINDOWEVENT_RESTORED=9,SDL_WINDOWEVENT_FOCUS_GAINED=12,SDL_WINDOWEVENT_FOCUS_LOST=13,SDL_WINDOWEVENT_CLOSE=14;
using FT_Long=long;using FT_ULong=unsigned long;using FT_Pos=long;using FT_Fixed=long;using FT_Int=int;using FT_UInt=unsigned;using FT_Error=int;
struct FT_LibraryRec_;using FT_Library=FT_LibraryRec_*;struct FT_FaceRec_;struct FT_GlyphSlotRec_;using FT_Face=FT_FaceRec_*;using FT_GlyphSlot=FT_GlyphSlotRec_*;
struct FT_Generic { void* data; void(*finalizer)(void*); };
struct FT_Vector { FT_Pos x,y; };
struct FT_BBox { FT_Pos xMin,yMin,xMax,yMax; };
struct FT_Bitmap { unsigned rows,width; int pitch; unsigned char* buffer; unsigned short num_grays; unsigned char pixel_mode,palette_mode; void* palette; };
struct FT_Glyph_Metrics { FT_Pos width,height,horiBearingX,horiBearingY,horiAdvance,vertBearingX,vertBearingY,vertAdvance; };
struct FT_GlyphSlotRec_ { FT_Library library;FT_Face face;FT_GlyphSlot next;FT_UInt glyph_index;FT_Generic generic;FT_Glyph_Metrics metrics;FT_Fixed linearHoriAdvance,linearVertAdvance;FT_Vector advance;unsigned format;FT_Bitmap bitmap;int bitmap_left,bitmap_top; };
struct FT_FaceRec_ { FT_Long num_faces,face_index,face_flags,style_flags,num_glyphs;char* family_name;char* style_name;FT_Int num_fixed_sizes;void* available_sizes;FT_Int num_charmaps;void* charmaps;FT_Generic generic;FT_BBox bbox;unsigned short units_per_EM;short ascender,descender,height,max_advance_width,max_advance_height,underline_position,underline_thickness;FT_GlyphSlot glyph;void* size;void* charmap; };
constexpr int FT_LOAD_RENDER=4;
#endif
