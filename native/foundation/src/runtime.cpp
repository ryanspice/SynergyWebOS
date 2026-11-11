#include "spice/runtime.hpp"
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace spice {
  Library::Library(const std::vector<std::string>& names){
    for(auto& name:names){
#ifdef _WIN32
      handle_=reinterpret_cast<void*>(LoadLibraryExA(name.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
#else
      handle_=dlopen(name.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
      if(handle_)break;
    }
    if(!handle_)throw std::runtime_error("Missing native runtime: "+names.front());
  }
  Library::~Library(){
    if(handle_){
#ifdef _WIN32
      FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
      dlclose(handle_);
#endif
    }
  }
  void* Library::symbol(const char* name)const{
#ifdef _WIN32
    auto p=reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle_),name));
#else
    auto p=dlsym(handle_,name);
#endif
    if(!p)throw std::runtime_error(std::string("Missing runtime symbol: ")+name);
    return p;
  }
  Sdl::Sdl():lib({
    "libSDL2-2.0.so.0","libSDL2.so","SDL2.dll","libSDL2.dylib"
  }
  ){
#define LOAD(name) name=lib.get<decltype(name)>("SDL_" #name)
    LOAD(SetMainReady);
    LOAD(Init);
    LOAD(Quit);
    LOAD(GetError);
    LOAD(GetVersion);
    LOAD(CreateWindow);
    LOAD(DestroyWindow);
    LOAD(GL_SetAttribute);
    LOAD(GL_CreateContext);
    LOAD(GL_DeleteContext);
    LOAD(GL_GetProcAddress);
    LOAD(GL_SetSwapInterval);
    LOAD(GL_GetSwapInterval);
    LOAD(GL_GetDrawableSize);
    LOAD(GL_SwapWindow);
    LOAD(GetWindowSize);
    LOAD(SetWindowFullscreen);
    LOAD(GetCurrentDisplayMode);
    LOAD(PollEvent);
    LOAD(PushEvent);
    LOAD(WaitEventTimeout);
    LOAD(GetScancodeName);
    LOAD(StartTextInput);
    LOAD(StopTextInput);
    LOAD(NumJoysticks);
    LOAD(IsGameController);
    LOAD(GameControllerOpen);
    LOAD(GameControllerClose);
    LOAD(GameControllerGetJoystick);
    LOAD(JoystickInstanceID);
#undef LOAD
    SDL_version version{};
    GetVersion(&version);
    if(version.major!=2||(version.minor==0&&version.patch<10))throw std::runtime_error("SDL2 runtime required");
  }
  FreeType::FreeType(const std::string& font):lib({
    "libfreetype.so.6","freetype.dll","libfreetype-6.dll","libfreetype.6.dylib"
  }
  ){
    Init=lib.get<decltype(Init)>("FT_Init_FreeType");
    Done=lib.get<decltype(Done)>("FT_Done_FreeType");
    NewFace=lib.get<decltype(NewFace)>("FT_New_Face");
    DoneFace=lib.get<decltype(DoneFace)>("FT_Done_Face");
    SetPixels=lib.get<decltype(SetPixels)>("FT_Set_Pixel_Sizes");
    LoadChar=lib.get<decltype(LoadChar)>("FT_Load_Char");
    CharIndex=lib.get<decltype(CharIndex)>("FT_Get_Char_Index");
    Kerning=lib.get<decltype(Kerning)>("FT_Get_Kerning");
    if(Init(&instance))throw std::runtime_error("FreeType initialization failed");
    if(NewFace(instance,font.c_str(),0,&face)){
      Done(instance);
      instance=nullptr;
      throw std::runtime_error("Cannot load selected font");
    }
  }
  FreeType::~FreeType(){
    if(face)DoneFace(face);
    if(instance)Done(instance);
  }
  std::string find_font(const std::string& requested){
    if(!requested.empty()){
      if(!std::filesystem::is_regular_file(requested))throw std::runtime_error("Requested font not found");
      return requested;
    }
    if(auto* p=std::getenv("SPICE_CPP_FONT")){
      if(std::filesystem::is_regular_file(p))return p;
      throw std::runtime_error("SPICE_CPP_FONT does not point to a file");
    }
    for(auto* p:{
      "C:/Windows/Fonts/segoeui.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/fonts-go/Go-Regular.ttf","/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf","/usr/share/fonts/LG_Display.ttf","/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf","/System/Library/Fonts/Supplemental/Arial.ttf"
    }
    )if(std::filesystem::is_regular_file(p))return p;
    throw std::runtime_error("No font available. Pass --font /path/to/an/installed-font.ttf (fonts not bundled)");
  }
}
