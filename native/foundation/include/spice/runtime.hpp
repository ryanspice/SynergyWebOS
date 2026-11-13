#pragma once
#include "native_abi.hpp"
#include <memory>
#include <string>
#include <vector>
namespace spice {
  class Library {
    void* handle_=nullptr;
    public:
    explicit Library(const std::vector<std::string>& names);
    ~Library();
    Library(const Library&)=delete;
    Library& operator=(const Library&)=delete;
    void* symbol(const char* name)const;
    template<class T>T get(const char* name)const{
      return reinterpret_cast<T>(symbol(name));
    }
  };
  struct Sdl {
    Library lib;
    void (*SetMainReady)();
    int (*Init)(Uint32);
    void(*Quit)();
    const char*(*GetError)();
    void(*GetVersion)(SDL_version*);
    SDL_Window*(*CreateWindow)(const char*,int,int,int,int,Uint32);
    void(*DestroyWindow)(SDL_Window*);
    int(*GL_SetAttribute)(SDL_GLattr,int);
    SDL_GLContext(*GL_CreateContext)(SDL_Window*);
    void(*GL_DeleteContext)(SDL_GLContext);
    void*(*GL_GetProcAddress)(const char*);
    int(*GL_SetSwapInterval)(int);
    int(*GL_GetSwapInterval)();
    void(*GL_GetDrawableSize)(SDL_Window*,int*,int*);
    void(*GL_SwapWindow)(SDL_Window*);
    void(*GetWindowSize)(SDL_Window*,int*,int*);
    int(*SetWindowFullscreen)(SDL_Window*,Uint32);
    int(*GetCurrentDisplayMode)(int,SDL_DisplayMode*);
    int(*PollEvent)(SDL_Event*);
    int(*PushEvent)(SDL_Event*);
    int(*WaitEventTimeout)(SDL_Event*,int);
    const char*(*GetScancodeName)(SDL_Scancode);
    void(*StartTextInput)();
    void(*StopTextInput)();
    int(*NumJoysticks)();
    int(*IsGameController)(int);
    SDL_GameController*(*GameControllerOpen)(int);
    void(*GameControllerClose)(SDL_GameController*);
    SDL_Joystick*(*GameControllerGetJoystick)(SDL_GameController*);
    SDL_JoystickID(*JoystickInstanceID)(SDL_Joystick*);
    explicit Sdl(const std::string& runtime_path={});
  };
  struct FreeType {
    Library lib;
    FT_Library instance=nullptr;
    FT_Face face=nullptr;
    FT_Error(*Init)(FT_Library*);
    FT_Error(*Done)(FT_Library);
    FT_Error(*NewFace)(FT_Library,const char*,FT_Long,FT_Face*);
    FT_Error(*DoneFace)(FT_Face);
    FT_Error(*SetPixels)(FT_Face,FT_UInt,FT_UInt);
    FT_Error(*LoadChar)(FT_Face,FT_ULong,int32_t);
    FT_UInt(*CharIndex)(FT_Face,FT_ULong);
    FT_Error(*Kerning)(FT_Face,FT_UInt,FT_UInt,FT_UInt,FT_Vector*);
    explicit FreeType(const std::string& font);
    ~FreeType();
  };
  std::string find_font(const std::string& requested);
}
