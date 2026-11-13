#include "spice/render.hpp"
#include <png.h>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
namespace spice {
  constexpr GLenum GL_VENDOR_=0x1f00,GL_RENDERER_=0x1f01,GL_VERSION_=0x1f02,GL_MAX_TEXTURE_SIZE_=0xd33,GL_BLEND_=0xbe2,GL_DEPTH_TEST_=0xb71,GL_SRC_ALPHA_=0x302,GL_ONE_MINUS_SRC_ALPHA_=0x303,GL_COLOR_BUFFER_BIT_=0x4000,GL_VERTEX_SHADER_=0x8b31,GL_FRAGMENT_SHADER_=0x8b30,GL_COMPILE_STATUS_=0x8b81,GL_LINK_STATUS_=0x8b82,GL_ARRAY_BUFFER_=0x8892,GL_DYNAMIC_DRAW_=0x88e8,GL_FLOAT_=0x1406,GL_TRIANGLES_=4,GL_TEXTURE_2D_=0xde1,GL_TEXTURE0_=0x84c0,GL_RGBA_=0x1908,GL_ALPHA_=0x1906,GL_UNSIGNED_BYTE_=0x1401,GL_TEXTURE_MIN_FILTER_=0x2801,GL_TEXTURE_MAG_FILTER_=0x2800,GL_LINEAR_=0x2601,GL_TEXTURE_WRAP_S_=0x2802,GL_TEXTURE_WRAP_T_=0x2803,GL_CLAMP_TO_EDGE_=0x812f,GL_UNPACK_ALIGNMENT_=0xcf5,GL_PACK_ALIGNMENT_=0xd05;
  Gl::Gl(Sdl& sdl){
#define LOAD(name) name=reinterpret_cast<decltype(name)>(sdl.GL_GetProcAddress("gl" #name));if(!name)throw std::runtime_error("Missing GL symbol: gl" #name)
    LOAD(GetString);
    LOAD(GetIntegerv);
    LOAD(GetError);
    LOAD(Viewport);
    LOAD(ClearColor);
    LOAD(Clear);
    LOAD(Enable);
    LOAD(Disable);
    LOAD(BlendFunc);
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(DeleteShader);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(BindAttribLocation);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(UseProgram);
    LOAD(DeleteProgram);
    LOAD(GetUniformLocation);
    LOAD(Uniform1i);
    LOAD(Uniform1f);
    LOAD(Uniform2f);
    LOAD(Uniform4f);
    LOAD(GenBuffers);
    LOAD(BindBuffer);
    LOAD(BufferData);
    LOAD(BufferSubData);
    LOAD(DeleteBuffers);
    LOAD(EnableVertexAttribArray);
    LOAD(VertexAttribPointer);
    LOAD(DrawArrays);
    LOAD(GenTextures);
    LOAD(BindTexture);
    LOAD(TexParameteri);
    LOAD(TexImage2D);
    LOAD(DeleteTextures);
    LOAD(ActiveTexture);
    LOAD(PixelStorei);
    LOAD(ReadPixels);
    LOAD(Finish);
#undef LOAD
  }
  Color Color::hex(const std::string& s){
    if(s.size()!=7||s[0]!='#')return {};
    try{
      auto v=std::stoul(s.substr(1),nullptr,16);
      return {
        ((v>>16)&255)/255.f,((v>>8)&255)/255.f,(v&255)/255.f,1
      };
    }
    catch(...){
      return {};
    }
  }
  GLuint Renderer::compile(GLenum type,const std::string& src){
    auto id=gl_.CreateShader(type);
    const char* p=src.c_str();
    gl_.ShaderSource(id,1,&p,nullptr);
    gl_.CompileShader(id);
    int ok=0;
    gl_.GetShaderiv(id,GL_COMPILE_STATUS_,&ok);
    if(!ok){
      std::array<char,2048> log{};
      gl_.GetShaderInfoLog(id,log.size(),nullptr,log.data());
      gl_.DeleteShader(id);
      throw std::runtime_error(std::string("Shader failed: ")+log.data());
    }
    return id;
  }
  Renderer::Renderer(Sdl& sdl,const std::string& font,const std::filesystem::path& assets):gl_(sdl),ft_(font){
    auto str=[&](GLenum t){
      auto* p=gl_.GetString(t);
      return p?std::string(reinterpret_cast<const char*>(p)):"unknown";
    };
    vendor=str(GL_VENDOR_);
    renderer=str(GL_RENDERER_);
    version=str(GL_VERSION_);
    is_es=version.find("OpenGL ES")!=std::string::npos;
    gl_.GetIntegerv(GL_MAX_TEXTURE_SIZE_,&texture_limit_);
    if(texture_limit_<512)throw std::runtime_error("GPU texture limit is too small");
    const std::string prefix=is_es?"#version 100\nprecision mediump float;\n#define VARY_PREC mediump\n":"#version 120\n#define VARY_PREC\n";
    const std::string vertex=(is_es?"#version 100\nprecision highp float;\n#define VARY_PREC mediump\n":prefix)+R"(
attribute vec2 a_position;
attribute vec2 a_uv;
varying VARY_PREC vec2 v_uv;
void main(){v_uv=a_uv;gl_Position=vec4(a_position.x/960.0-1.0,1.0-a_position.y/540.0,0.0,1.0);}
)";
    const std::string fragment=prefix+R"(
varying VARY_PREC vec2 v_uv;
uniform sampler2D u_texture;
uniform vec4 u_color;
uniform vec4 u_color2;
uniform vec2 u_size;
uniform float u_radius;
uniform float u_border;
uniform float u_aa;
uniform int u_mode;
void main(){
  vec4 col=u_color;
  if(u_mode==1)col*=texture2D(u_texture,v_uv);
  if(u_mode==2)col=mix(u_color,u_color2,v_uv.y);
  if(u_mode==3)col.a*=texture2D(u_texture,v_uv).a;
  if(u_mode!=3){
    vec2 p=abs((v_uv-0.5)*u_size)-(u_size*0.5-vec2(u_radius));
    float d=length(max(p,0.0))+min(max(p.x,p.y),0.0)-u_radius;
    float coverage=1.0-smoothstep(-u_aa,u_aa,d);
    if(u_border>0.0)coverage*=smoothstep(-u_border-u_aa,-u_border+u_aa,d);
    col.a*=coverage;
  }
  gl_FragColor=col;
}
)";
    auto vs=compile(GL_VERTEX_SHADER_,vertex),fs=compile(GL_FRAGMENT_SHADER_,fragment);
    program_=gl_.CreateProgram();
    gl_.AttachShader(program_,vs);
    gl_.AttachShader(program_,fs);
    gl_.BindAttribLocation(program_,0,"a_position");
    gl_.BindAttribLocation(program_,1,"a_uv");
    gl_.LinkProgram(program_);
    gl_.DeleteShader(vs);
    gl_.DeleteShader(fs);
    int linked=0;
    gl_.GetProgramiv(program_,GL_LINK_STATUS_,&linked);
    if(!linked){
      std::array<char,2048> log{};
      gl_.GetProgramInfoLog(program_,log.size(),nullptr,log.data());
      throw std::runtime_error(std::string("GL program failed: ")+log.data());
    }
    gl_.UseProgram(program_);
    mode_=gl_.GetUniformLocation(program_,"u_mode");
    color_=gl_.GetUniformLocation(program_,"u_color");
    color2_=gl_.GetUniformLocation(program_,"u_color2");
    size_=gl_.GetUniformLocation(program_,"u_size");
    radius_=gl_.GetUniformLocation(program_,"u_radius");
    border_=gl_.GetUniformLocation(program_,"u_border");
    aa_=gl_.GetUniformLocation(program_,"u_aa");
    gl_.Uniform1i(gl_.GetUniformLocation(program_,"u_texture"),0);
    gl_.GenBuffers(1,&vbo_);
    gl_.BindBuffer(GL_ARRAY_BUFFER_,vbo_);
    gl_.BufferData(GL_ARRAY_BUFFER_,24*sizeof(float),nullptr,GL_DYNAMIC_DRAW_);
    const std::array<unsigned char,4> white{
      255,255,255,255
    };
    white_=upload(1,1,white.data());
    (void)assets; // Synergy uses native shapes; no homescreen artwork is bundled.

  }
  Renderer::~Renderer(){
    for(auto& [key,t]:images_){
      (void)key;
      gl_.DeleteTextures(1,&t.id);
    }
    for(auto& [key,t]:texts_){
      (void)key;
      gl_.DeleteTextures(1,&t.tex.id);
    }
    if(white_.id)gl_.DeleteTextures(1,&white_.id);
    if(vbo_)gl_.DeleteBuffers(1,&vbo_);
    if(program_)gl_.DeleteProgram(program_);
  }
  Texture Renderer::upload(int w,int h,const unsigned char* data,bool alpha){
    Texture t;
    t.w=w;
    t.h=h;
    t.bytes=static_cast<size_t>(w)*h*(alpha?1:4);
    gl_.GenTextures(1,&t.id);
    gl_.ActiveTexture(GL_TEXTURE0_);
    gl_.BindTexture(GL_TEXTURE_2D_,t.id);
    gl_.PixelStorei(GL_UNPACK_ALIGNMENT_,1);
    gl_.TexParameteri(GL_TEXTURE_2D_,GL_TEXTURE_MIN_FILTER_,GL_LINEAR_);
    gl_.TexParameteri(GL_TEXTURE_2D_,GL_TEXTURE_MAG_FILTER_,GL_LINEAR_);
    gl_.TexParameteri(GL_TEXTURE_2D_,GL_TEXTURE_WRAP_S_,GL_CLAMP_TO_EDGE_);
    gl_.TexParameteri(GL_TEXTURE_2D_,GL_TEXTURE_WRAP_T_,GL_CLAMP_TO_EDGE_);
    const GLenum format=alpha?GL_ALPHA_:GL_RGBA_;
    gl_.TexImage2D(GL_TEXTURE_2D_,0,format,w,h,0,format,GL_UNSIGNED_BYTE_,data);
    if(gl_.GetError()!=0)throw std::runtime_error("GPU texture upload failed");
    return t;
  }
  Texture Renderer::png(const std::filesystem::path& path){
    png_image image{};
    image.version=PNG_IMAGE_VERSION;
    if(!png_image_begin_read_from_file(&image,path.string().c_str()))throw std::runtime_error("Cannot decode artwork: "+path.filename().string());
    image.format=PNG_FORMAT_RGBA;
    if(image.width>8192||image.height>8192){
      png_image_free(&image);
      throw std::runtime_error("Artwork exceeds decode budget");
    }
    std::vector<unsigned char> data(PNG_IMAGE_SIZE(image));
    if(!png_image_finish_read(&image,nullptr,data.data(),0,nullptr)){
      png_image_free(&image);
      throw std::runtime_error("Artwork decode failed");
    }
    int w=static_cast<int>(image.width),h=static_cast<int>(image.height);
    png_image_free(&image);
    if(w<=texture_limit_&&h<=texture_limit_)return upload(w,h,data.data());
    float s=std::min(static_cast<float>(texture_limit_)/w,static_cast<float>(texture_limit_)/h);
    int nw=std::max(1,static_cast<int>(w*s)),nh=std::max(1,static_cast<int>(h*s));
    std::vector<unsigned char> small(static_cast<size_t>(nw)*nh*4);
    for(int y=0;y<nh;++y)for(int x=0;x<nw;++x){
      auto from=(static_cast<size_t>(static_cast<int>(y/s))*w+static_cast<int>(x/s))*4;
      auto to=(static_cast<size_t>(y)*nw+x)*4;
      std::copy_n(data.data()+from,4,small.data()+to);
    }
    return upload(nw,nh,small.data());
  }
  void Renderer::begin(const Viewport& v){
    transform();
    view_=v;
    ++frame_;
    draws=0;
    gl_.Viewport(0,0,v.drawable_w,v.drawable_h);
    gl_.ClearColor(.008f,.012f,.024f,1);
    gl_.Clear(GL_COLOR_BUFFER_BIT_);
    gl_.Viewport(static_cast<int>(v.left),static_cast<int>(v.top),static_cast<int>(DesignWidth*v.scale),static_cast<int>(DesignHeight*v.scale));
    gl_.Disable(GL_DEPTH_TEST_);
    gl_.Enable(GL_BLEND_);
    gl_.BlendFunc(GL_SRC_ALPHA_,GL_ONE_MINUS_SRC_ALPHA_);
    gl_.UseProgram(program_);
    gl_.BindBuffer(GL_ARRAY_BUFFER_,vbo_);
    gl_.EnableVertexAttribArray(0);
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(0,2,GL_FLOAT_,0,4*sizeof(float),nullptr);
    gl_.VertexAttribPointer(1,2,GL_FLOAT_,0,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
  }
  void Renderer::end(){
    while(widths_.size()>384){
      auto oldest=std::min_element(widths_.begin(),widths_.end(),[](auto& a,auto& b){
        return a.second.second<b.second.second;
      });
      widths_.erase(oldest);
    }
    while(ellipses_.size()>384){
      auto oldest=std::min_element(ellipses_.begin(),ellipses_.end(),[](auto& a,auto& b){
        return a.second.second<b.second.second;
      });
      ellipses_.erase(oldest);
    }
    // Metrics are small, but sizes/densities can change; bound the cache.
    if(glyph_metrics_.size()>8192)glyph_metrics_.clear();
    while((texts_.size()>384||text_bytes_>24*1024*1024)&&texts_.size()>1){
      auto oldest=std::min_element(texts_.begin(),texts_.end(),[](auto& a,auto& b){
        return a.second.last_use<b.second.last_use;
      }
      );
      text_bytes_-=oldest->second.tex.bytes;
      gl_.DeleteTextures(1,&oldest->second.tex.id);
      texts_.erase(oldest);
    }
  }
  void Renderer::quad(Rect r,int mode,Color c,Color c2,float radius,float border,GLuint tex,const std::array<float,4>& uv){
    if(r.w<=0||r.h<=0||(c.a<=0&&(mode!=2||c2.a<=0)))return;
    c.a*=opacity_;
    c2.a*=opacity_;
    float x=r.x+offset_x_,y=r.y+offset_y_,x2=x+r.w,y2=y+r.h;
    std::array<float,24> vertices{
      x,y,uv[0],uv[1],x2,y,uv[2],uv[1],x2,y2,uv[2],uv[3],x,y,uv[0],uv[1],x2,y2,uv[2],uv[3],x,y2,uv[0],uv[3]
    };
    gl_.Uniform1i(mode_,mode);
    gl_.Uniform4f(color_,c.r,c.g,c.b,c.a);
    gl_.Uniform4f(color2_,c2.r,c2.g,c2.b,c2.a);
    gl_.Uniform2f(size_,r.w,r.h);
    gl_.Uniform1f(radius_,radius);
    gl_.Uniform1f(border_,border);
    gl_.Uniform1f(aa_,.9f/view_.scale);
    gl_.ActiveTexture(GL_TEXTURE0_);
    gl_.BindTexture(GL_TEXTURE_2D_,tex?tex:white_.id);
    gl_.BufferSubData(GL_ARRAY_BUFFER_,0,sizeof(vertices),vertices.data());
    gl_.DrawArrays(GL_TRIANGLES_,0,6);
    ++draws;
  }
  void Renderer::rect(Rect r,Color c,float radius,float stroke){
    quad(r,0,c,c,radius,stroke,white_.id);
  }
  void Renderer::gradient(Rect r,Color a,Color b,float radius){
    quad(r,2,a,b,radius,0,white_.id);
  }
  void Renderer::image(const std::string& key,Rect r,float alpha,float radius){
    auto it=images_.find(key);
    if(it==images_.end())return;
    auto& t=it->second;
    float source=static_cast<float>(t.w)/t.h,dest=r.w/r.h;
    std::array<float,4> uv{
      0,0,1,1
    };
    if(source>dest){
      float u=dest/source;
      uv[0]=(1-u)*.5f;
      uv[2]=1-uv[0];
    }
    else{
      float v=source/dest;
      uv[1]=(1-v)*.5f;
      uv[3]=1-uv[1];
    }
    quad(r,1,{
      1,1,1,alpha
    },
    {},
    radius,0,t.id,uv);
  }
  struct Decoded {
    unsigned long code;
    size_t byte;
  };
  // Code points with the byte offset where each starts; invalid sequences
  // become '?'. Bounded to 512 code points, as drawing always was. `end`
  // receives the byte offset just after the last decoded code point.
  static std::vector<Decoded> decode_utf8(const std::string& s,size_t* end=nullptr){
    std::vector<Decoded> out;
    size_t i=0;
    while(i<s.size()&&out.size()<512){
      const size_t start=i;
      auto b=static_cast<unsigned char>(s[i++]);
      if(b<128){
        out.push_back({b,start});
        continue;
      }
      int n=b>=0xf0?3:b>=0xe0?2:b>=0xc0?1:0;
      unsigned long c=b&((1<<(6-n))-1);
      bool valid=n>0&&i+static_cast<size_t>(n)<=s.size();
      for(int j=0;j<n&&valid;++j){
        auto v=static_cast<unsigned char>(s[i]);
        if((v&0xc0)!=0x80){
          valid=false;
          break;
        }
        c=(c<<6)|(v&63);
        ++i;
      }
      out.push_back({valid?c:static_cast<unsigned long>('?'),start});
    }
    if(end)*end=i;
    return out;
  }
  float Renderer::density()const{
    return std::clamp(view_.scale,.5f,4.f);
  }
  int Renderer::pixels(float logical_size)const{
    return std::max(8,static_cast<int>(std::lround(logical_size*density())));
  }
  void Renderer::set_pixels(int px){
    if(px==ft_px_)return;
    if(ft_.SetPixels(ft_.face,0,static_cast<FT_UInt>(px)))throw std::runtime_error("Font size unavailable");
    ft_px_=px;
  }
  const Renderer::GlyphMetrics& Renderer::metrics(int px,unsigned long code){
    const auto key=(static_cast<uint64_t>(px)<<32)|static_cast<uint32_t>(code);
    auto it=glyph_metrics_.find(key);
    if(it!=glyph_metrics_.end())return it->second;
    set_pixels(px);
    GlyphMetrics m;
    m.code=code;
    m.idx=ft_.CharIndex(ft_.face,code);
    if(!m.idx){
      m.code='?';
      m.idx=ft_.CharIndex(ft_.face,m.code);
    }
    // Bounds come from the same hinted rendered bitmap that drawing uses.
    if(!ft_.LoadChar(ft_.face,m.code,FT_LOAD_RENDER)){
      auto slot=ft_.face->glyph;
      m.left=slot->bitmap_left;
      m.top=slot->bitmap_top;
      m.w=static_cast<int>(slot->bitmap.width);
      m.h=static_cast<int>(slot->bitmap.rows);
      m.advance=static_cast<int>(slot->advance.x/64);
      m.ok=true;
    }
    return glyph_metrics_.emplace(key,m).first->second;
  }
  // Callers set the pixel size first; FreeType scales kerning by it.
  int Renderer::kerning(unsigned previous,unsigned idx){
    if(!previous||!idx)return 0;
    FT_Vector kern{};
    return ft_.Kerning(ft_.face,previous,idx,0,&kern)?0:static_cast<int>(kern.x/64);
  }
  Renderer::Layout Renderer::layout(const std::string& value,int px){
    Layout out;
    unsigned previous=0;
    set_pixels(px);
    for(const auto& d:decode_utf8(value,&out.bytes)){
      const auto& m=metrics(px,d.code);
      Placed g;
      g.code=m.code;
      g.byte=d.byte;
      if(m.ok){
        out.pen+=kerning(previous,m.idx);
        g.x=out.pen+m.left;
        g.top=m.top;
        g.w=m.w;
        g.h=m.h;
        g.ok=true;
        out.asc=std::max(out.asc,m.top);
        out.desc=std::max(out.desc,m.h-m.top);
        out.min_x=std::min(out.min_x,g.x);
        out.max_x=std::max(out.max_x,g.x+g.w);
        out.pen+=m.advance;
        previous=m.idx;
      }
      out.glyphs.push_back(g);
    }
    out.max_x=std::max(out.max_x,out.pen);
    return out;
  }
  TextTexture Renderer::layout_text(const std::string& value,float logical_size,bool upload_texture){
    const float scale=density();
    const int px=pixels(logical_size);
    auto l=layout(value,px);
    const int full_w=std::max(1,l.width()),full_h=std::max(1,l.height());
    // Storage is bounded by the GPU; the measured extent is never clamped, so
    // fit/ellipsis decisions see true overflow.
    const int w=std::min(full_w,texture_limit_),h=std::min(full_h,texture_limit_);
    if(!upload_texture)return {{},w/scale,h/scale,full_w/scale,frame_};
    set_pixels(px);
    std::vector<unsigned char> coverage(static_cast<size_t>(w)*h,0);
    for(const auto& g:l.glyphs){
      if(!g.ok||!g.w||!g.h)continue;
      if(ft_.LoadChar(ft_.face,g.code,FT_LOAD_RENDER))continue;
      auto& b=ft_.face->glyph->bitmap;
      const int rows=static_cast<int>(b.rows);
      const int gw=std::min(g.w,static_cast<int>(b.width)),gh=std::min(g.h,rows);
      for(int y=0;y<gh;++y){
        const int dy=l.asc-g.top+y+2;
        if(dy<0||dy>=h)continue;
        const int sy=b.pitch>=0?y:rows-1-y;
        auto* row=b.buffer+static_cast<ptrdiff_t>(sy)*std::abs(b.pitch);
        for(int x=0;x<gw;++x){
          const int dx=g.x-l.min_x+x+2;
          if(dx<0||dx>=w)continue;
          const unsigned char a=b.pixel_mode==1?((row[x/8]&(0x80>>(x%8)))?255:0):row[x];
          auto to=static_cast<size_t>(dy)*w+dx;
          coverage[to]=std::max(coverage[to],a);
        }
      }
    }
    return {
      upload(w,h,coverage.data(),true),w/scale,h/scale,full_w/scale,frame_
    };
  }
  static std::string text_key(float size,float scale,const std::string& value){
    return std::to_string(static_cast<int>(size*64))+":"+std::to_string(static_cast<int>(scale*128))+":"+value;
  }
  float Renderer::width(const std::string& value,float size){
    std::string key=text_key(size,view_.scale,value);
    auto it=widths_.find(key);
    if(it==widths_.end()){
      auto drawn=texts_.find(key);
      const auto measured=drawn==texts_.end()?layout_text(value,size,false).measured_w:drawn->second.measured_w;
      it=widths_.emplace(std::move(key),std::pair{measured,frame_}).first;
    }
    it->second.second=frame_;
    return it->second.first;
  }
  float Renderer::text(const std::string& value,float x,float y,float size,Color color){
    std::string key=text_key(size,view_.scale,value);
    auto it=texts_.find(key);
    if(it==texts_.end()){
      auto texture=layout_text(value,size,true);
      text_bytes_+=texture.tex.bytes;
      it=texts_.emplace(std::move(key),std::move(texture)).first;
    }
    auto& t=it->second;
    t.last_use=frame_;
    quad({
      x,y,t.logical_w,t.logical_h
    },
    3,color,color,0,0,t.tex.id);
    return t.logical_w;
  }
  std::string Renderer::ellipsis(const std::string& value,float size,float max){
    std::string key=std::to_string(static_cast<int>(max*64))+":"+text_key(size,view_.scale,value);
    auto cached=ellipses_.find(key);
    if(cached!=ellipses_.end()){
      cached->second.second=frame_;
      return cached->second.first;
    }
    const float scale=density();
    const int px=pixels(size);
    // A candidate must fit both the field and one GPU texture.
    auto fits=[&](int w){
      return std::max(1,w)/scale<=max&&w<=texture_limit_;
    };
    std::string result;
    auto l=layout(value,px);
    if(fits(l.width()))result=value;
    else{
      // One layout; each candidate prefix + "..." is evaluated from the
      // prefix's extents, pen and last glyph plus the dots' own kerning and
      // bounds. Linear glyph work instead of re-laying out every candidate.
      const size_t n=l.glyphs.size();
      std::vector<int> pen(n+1,0),min_x(n+1,0),max_x(n+1,0);
      std::vector<unsigned> last(n+1,0);
      for(size_t i=0;i<n;++i){
        const auto& g=l.glyphs[i];
        pen[i+1]=pen[i];
        min_x[i+1]=min_x[i];
        max_x[i+1]=max_x[i];
        last[i+1]=last[i];
        if(!g.ok)continue;
        const auto& m=metrics(px,g.code);
        pen[i+1]=g.x-m.left+m.advance;
        min_x[i+1]=std::min(min_x[i],g.x);
        max_x[i+1]=std::max(max_x[i],g.x+g.w);
        last[i+1]=m.idx;
      }
      const auto dot=metrics(px,'.');
      set_pixels(px);
      const int dot_kern=kerning(dot.idx,dot.idx);
      std::optional<size_t> keep;
      for(size_t k=n+1;k-->0;){
        // Drawing decodes at most 512 code points; the dots must be among them.
        if(k+3>512)continue;
        int p=pen[k],lo=min_x[k],hi=max_x[k];
        if(dot.ok){
          p+=kerning(last[k],dot.idx);
          for(int i=0;i<3;++i){
            if(i)p+=dot_kern;
            lo=std::min(lo,p+dot.left);
            hi=std::max(hi,p+dot.left+dot.w);
            p+=dot.advance;
          }
        }
        hi=std::max(hi,p);
        if(fits(hi-lo+4)){
          keep=k;
          break;
        }
      }
      // As before, an unfittable field still gets a bare "...".
      result=keep?value.substr(0,*keep<n?l.glyphs[*keep].byte:l.bytes)+"...":"...";
    }
    ellipses_.emplace(std::move(key),std::pair{result,frame_});
    return result;
  }
  void Renderer::line(float x,float y,float x2,float y2,Color c,float thickness){
    x+=offset_x_;
    x2+=offset_x_;
    y+=offset_y_;
    y2+=offset_y_;
    c.a*=opacity_;
    float dx=x2-x,dy=y2-y;
    float len=std::sqrt(dx*dx+dy*dy);
    if(len<.001f)return;
    float nx=-dy/len*thickness*.5f,ny=dx/len*thickness*.5f;
    std::array<float,24> v{
      x+nx,y+ny,0,0,x2+nx,y2+ny,1,0,x2-nx,y2-ny,1,1,x+nx,y+ny,0,0,x2-nx,y2-ny,1,1,x-nx,y-ny,0,1
    };
    gl_.Uniform1i(mode_,0);
    gl_.Uniform4f(color_,c.r,c.g,c.b,c.a);
    gl_.Uniform2f(size_,len,thickness);
    gl_.Uniform1f(radius_,0);
    gl_.Uniform1f(border_,0);
    gl_.Uniform1f(aa_,.5f/view_.scale);
    gl_.BindTexture(GL_TEXTURE_2D_,white_.id);
    gl_.BufferSubData(GL_ARRAY_BUFFER_,0,sizeof(v),v.data());
    gl_.DrawArrays(GL_TRIANGLES_,0,6);
    ++draws;
  }
  void Renderer::icon(const std::string& name,Rect r,Color c){
    float x=r.x,y=r.y,w=r.w,h=r.h;
    auto l=[&](float a,float b,float d,float e){
      line(x+a*w,y+b*h,x+d*w,y+e*h,c,std::max(1.5f,w*.05f));
    };
    auto box=[&](float a,float b,float d,float e,float rad=2.f){
      rect({
        x+a*w,y+b*h,d*w,e*h
      },
      c,rad,w*.045f);
    };
    if(name=="tv"||name=="input"){
      box(.08f,.16f,.84f,.57f);
      l(.5f,.73f,.5f,.9f);
      l(.3f,.9f,.7f,.9f);
      if(name=="input"){
        l(.3f,.44f,.7f,.44f);
        l(.58f,.32f,.7f,.44f);
        l(.58f,.56f,.7f,.44f);
      }
    }
    else if(name=="cpu"){
      box(.24f,.24f,.52f,.52f);
      box(.36f,.36f,.28f,.28f);
      for(int i=0;i<3;++i){
        float v=.32f+i*.18f;
        l(v,.08f,v,.24f);
        l(v,.76f,v,.92f);
        l(.08f,v,.24f,v);
        l(.76f,v,.92f,v);
      }
    }
    else if(name=="library"||name=="apps"){
      for(int a=0;a<2;++a)for(int b=0;b<2;++b)box(.1f+a*.45f,.1f+b*.45f,.31f,.31f);
    }
    else if(name=="search"){
      rect({
        x+w*.08f,y+h*.08f,w*.62f,h*.62f
      },
      c,w*.31f,w*.05f);
      l(.62f,.62f,.92f,.92f);
    }
    else if(name=="layers"){
      for(int i=0;i<3;++i){
        float yy=.15f+i*.24f;
        l(.1f,yy+.13f,.5f,yy-.07f);
        l(.5f,yy-.07f,.9f,yy+.13f);
        l(.9f,yy+.13f,.5f,yy+.33f);
        l(.5f,yy+.33f,.1f,yy+.13f);
      }
    }
    else if(name=="settings"){
      rect({
        x+w*.22f,y+h*.22f,w*.56f,h*.56f
      },
      c,w*.28f,w*.07f);
      rect({
        x+w*.4f,y+h*.4f,w*.2f,h*.2f
      },
      c,w*.1f,w*.045f);
      for(int i=0;i<8;++i){
        float a=i*3.14159265f/4;
        l(.5f+std::cos(a)*.27f,.5f+std::sin(a)*.27f,.5f+std::cos(a)*.43f,.5f+std::sin(a)*.43f);
      }
    }
    else if(name=="back"){
      l(.75f,.27f,.25f,.27f);
      l(.25f,.27f,.43f,.1f);
      l(.25f,.27f,.43f,.44f);
      l(.75f,.27f,.75f,.78f);
      l(.75f,.78f,.28f,.78f);
    }
    else if(name=="home"){
      l(.1f,.46f,.5f,.1f);
      l(.5f,.1f,.9f,.46f);
      l(.23f,.36f,.23f,.9f);
      l(.23f,.9f,.77f,.9f);
      l(.77f,.9f,.77f,.36f);
    }
    else if(name=="star"){
      for(int i=0;i<10;++i){
        float a=-3.14159265f/2+i*3.14159265f/5,b=a+3.14159265f/5;
        float r1=i%2?.19f:.46f,r2=i%2?.46f:.19f;
        l(.5f+std::cos(a)*r1,.5f+std::sin(a)*r1,.5f+std::cos(b)*r2,.5f+std::sin(b)*r2);
      }
    }
    else if(name=="refresh"){
      for(int i=0;i<14;++i){
        float a=.5f+i*.36f,b=a+.36f;
        l(.5f+std::cos(a)*.35f,.5f+std::sin(a)*.35f,.5f+std::cos(b)*.35f,.5f+std::sin(b)*.35f);
      }
      l(.85f,.25f,.85f,.55f);
      l(.85f,.55f,.6f,.45f);
    }
    else if(name=="network"){
      for(int row=0;row<3;++row){
        float rr=.2f+row*.18f;
        for(int i=0;i<8;++i){
          float a=3.9f+i*.2f,b=a+.2f;
          l(.5f+std::cos(a)*rr,.8f+std::sin(a)*rr,.5f+std::cos(b)*rr,.8f+std::sin(b)*rr);
        }
      }
      rect({
        x+.45f*w,y+.75f*h,.1f*w,.1f*h
      },
      c,w*.05f);
    }
    else if(name=="play"||name=="media"){
      l(.3f,.16f,.8f,.5f);
      l(.8f,.5f,.3f,.84f);
      l(.3f,.84f,.3f,.16f);
    }
    else if(name=="homebrew"){
      box(.13f,.42f,.74f,.46f);
      l(.5f,.08f,.5f,.63f);
      l(.3f,.43f,.5f,.63f);
      l(.7f,.43f,.5f,.63f);
    }
    else if(name=="display"){
      box(.06f,.12f,.88f,.62f);
      l(.5f,.74f,.5f,.9f);
      l(.3f,.9f,.7f,.9f);
      l(.23f,.56f,.4f,.37f);
      l(.4f,.37f,.54f,.5f);
      l(.54f,.5f,.8f,.26f);
    }
    else if(name=="game"||name=="retropie"){
      box(.05f,.28f,.9f,.48f,w*.17f);
      l(.22f,.41f,.22f,.64f);
      l(.1f,.525f,.34f,.525f);
      rect({
        x+w*.65f,y+h*.38f,w*.09f,h*.09f
      },
      c,w*.045f);
      rect({
        x+w*.79f,y+h*.55f,w*.09f,h*.09f
      },
      c,w*.045f);
    }
    else {
      rect({
        x+w*.1f,y+h*.1f,w*.8f,h*.8f
      },
      c,w*.4f,w*.04f);
      l(.32f,.5f,.46f,.64f);
      l(.46f,.64f,.72f,.34f);
    }
  }
  void Renderer::screenshot(const std::filesystem::path& output){
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    int w=view_.drawable_w,h=view_.drawable_h;
    std::vector<unsigned char> data(static_cast<size_t>(w)*h*4),flipped(data.size());
    gl_.Finish();
    gl_.PixelStorei(GL_PACK_ALIGNMENT_,1);
    gl_.ReadPixels(0,0,w,h,GL_RGBA_,GL_UNSIGNED_BYTE_,data.data());
    if(gl_.GetError()!=0)throw std::runtime_error("Framebuffer read failed");
    for(int y=0;y<h;++y)std::memcpy(flipped.data()+static_cast<size_t>(y)*w*4,data.data()+static_cast<size_t>(h-1-y)*w*4,static_cast<size_t>(w)*4);
    png_image image{};
    image.version=PNG_IMAGE_VERSION;
    image.width=w;
    image.height=h;
    image.format=PNG_FORMAT_RGBA;
    if(!png_image_write_to_file(&image,output.string().c_str(),0,flipped.data(),0,nullptr))throw std::runtime_error("Screenshot write failed");
  }
}
