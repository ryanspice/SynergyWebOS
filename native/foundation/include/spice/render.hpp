#pragma once
#include "spice/core.hpp"
#include "spice/runtime.hpp"
#include <cstddef>
#include <map>
#include <memory>
namespace spice {
#ifdef _WIN32
#define SPICE_GL_CALL __stdcall
#else
#define SPICE_GL_CALL
#endif
  using GLenum=unsigned;
  using GLuint=unsigned;
  using GLint=int;
  using GLsizei=int;
  using GLboolean=unsigned char;
  using GLfloat=float;
  using GLchar=char;
  using GLsizeiptr=ptrdiff_t;
  struct Gl {
    const unsigned char*(SPICE_GL_CALL *GetString)(GLenum);
    void(SPICE_GL_CALL *GetIntegerv)(GLenum,GLint*);
    GLenum(SPICE_GL_CALL *GetError)();
    void(SPICE_GL_CALL *Viewport)(GLint,GLint,GLsizei,GLsizei);
    void(SPICE_GL_CALL *ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat);
    void(SPICE_GL_CALL *Clear)(GLenum);
    void(SPICE_GL_CALL *Enable)(GLenum);
    void(SPICE_GL_CALL *Disable)(GLenum);
    void(SPICE_GL_CALL *BlendFunc)(GLenum,GLenum);
    GLuint(SPICE_GL_CALL *CreateShader)(GLenum);
    void(SPICE_GL_CALL *ShaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*);
    void(SPICE_GL_CALL *CompileShader)(GLuint);
    void(SPICE_GL_CALL *GetShaderiv)(GLuint,GLenum,GLint*);
    void(SPICE_GL_CALL *GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
    void(SPICE_GL_CALL *DeleteShader)(GLuint);
    GLuint(SPICE_GL_CALL *CreateProgram)();
    void(SPICE_GL_CALL *AttachShader)(GLuint,GLuint);
    void(SPICE_GL_CALL *BindAttribLocation)(GLuint,GLuint,const GLchar*);
    void(SPICE_GL_CALL *LinkProgram)(GLuint);
    void(SPICE_GL_CALL *GetProgramiv)(GLuint,GLenum,GLint*);
    void(SPICE_GL_CALL *GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
    void(SPICE_GL_CALL *UseProgram)(GLuint);
    void(SPICE_GL_CALL *DeleteProgram)(GLuint);
    GLint(SPICE_GL_CALL *GetUniformLocation)(GLuint,const GLchar*);
    void(SPICE_GL_CALL *Uniform1i)(GLint,GLint);
    void(SPICE_GL_CALL *Uniform1f)(GLint,GLfloat);
    void(SPICE_GL_CALL *Uniform2f)(GLint,GLfloat,GLfloat);
    void(SPICE_GL_CALL *Uniform4f)(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
    void(SPICE_GL_CALL *GenBuffers)(GLsizei,GLuint*);
    void(SPICE_GL_CALL *BindBuffer)(GLenum,GLuint);
    void(SPICE_GL_CALL *BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
    void(SPICE_GL_CALL *BufferSubData)(GLenum,ptrdiff_t,GLsizeiptr,const void*);
    void(SPICE_GL_CALL *DeleteBuffers)(GLsizei,const GLuint*);
    void(SPICE_GL_CALL *EnableVertexAttribArray)(GLuint);
    void(SPICE_GL_CALL *VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
    void(SPICE_GL_CALL *DrawArrays)(GLenum,GLint,GLsizei);
    void(SPICE_GL_CALL *GenTextures)(GLsizei,GLuint*);
    void(SPICE_GL_CALL *BindTexture)(GLenum,GLuint);
    void(SPICE_GL_CALL *TexParameteri)(GLenum,GLenum,GLint);
    void(SPICE_GL_CALL *TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
    void(SPICE_GL_CALL *DeleteTextures)(GLsizei,const GLuint*);
    void(SPICE_GL_CALL *ActiveTexture)(GLenum);
    void(SPICE_GL_CALL *PixelStorei)(GLenum,GLint);
    void(SPICE_GL_CALL *ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
    void(SPICE_GL_CALL *Finish)();
    explicit Gl(Sdl& sdl);
  };
  struct Color {
    float r=1,g=1,b=1,a=1;
    Color opacity(float x)const{
      return {
        r,g,b,a*x
      };
    }
    static Color hex(const std::string& s);
  };
  struct Texture {
    GLuint id=0;
    int w=0,h=0;
    size_t bytes=0;
  };
  struct TextTexture {
    Texture tex;
    float logical_w=0,logical_h=0;
    uint64_t last_use=0;
  };
  class Renderer {
    Gl gl_;
    FreeType ft_;
    GLuint program_=0,vbo_=0;
    Texture white_;
    GLint mode_=0,color_=0,color2_=0,size_=0,radius_=0,border_=0,aa_=0;
    std::map<std::string,Texture> images_;
    std::map<std::string,TextTexture> texts_;
    uint64_t frame_=0;
    size_t text_bytes_=0,image_bytes_=0;
    int texture_limit_=2048;
    Viewport view_;
    float offset_x_=0,offset_y_=0,opacity_=1;
    GLuint compile(GLenum type,const std::string& src);
    Texture upload(int w,int h,const unsigned char* bytes);
    Texture png(const std::filesystem::path& path);
    TextTexture rasterize(const std::string& text,float size);
    void quad(Rect rect,int mode,Color color,Color color2,float radius,float border,GLuint texture,const std::array<float,4>& uv={
      0,0,1,1
    }
    );
    public:
    std::string vendor,renderer,version;
    bool is_es=false;
    int draws=0;
    Renderer(Sdl& sdl,const std::string& font,const std::filesystem::path& assets);
    ~Renderer();
    void transform(float x=0,float y=0,float opacity=1){
      offset_x_=x;
      offset_y_=y;
      opacity_=opacity;
    }
    void begin(const Viewport& v);
    void end();
    void rect(Rect r,Color c,float radius=0,float stroke=0);
    void gradient(Rect r,Color top,Color bottom,float radius=0);
    void image(const std::string& name,Rect r,float alpha=1,float radius=0);
    float text(const std::string& value,float x,float y,float size,Color color={
      1,1,1,1
    }
    );
    float width(const std::string& value,float size);
    std::string ellipsis(const std::string& value,float size,float max);
    void line(float x1,float y1,float x2,float y2,Color c,float thickness=2);
    void icon(const std::string& name,Rect r,Color c={
      1,1,1,1
    }
    );
    void screenshot(const std::filesystem::path& output);
    size_t texture_bytes()const{
      return text_bytes_+image_bytes_;
    }
    size_t text_cache_size()const{
      return texts_.size();
    }
    const Viewport& viewport()const{
      return view_;
    }
  };
}
