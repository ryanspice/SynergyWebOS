#include "spice/core.hpp"
#include <limits>
namespace spice {
  Viewport Viewport::make(int ww,int wh,int dw,int dh){
    Viewport v;
    v.window_w=std::max(1,ww);
    v.window_h=std::max(1,wh);
    v.drawable_w=std::max(1,dw);
    v.drawable_h=std::max(1,dh);
    v.scale=std::min(v.drawable_w/DesignWidth,v.drawable_h/DesignHeight);
    v.left=(v.drawable_w-DesignWidth*v.scale)*.5f;
    v.top=(v.drawable_h-DesignHeight*v.scale)*.5f;
    return v;
  }
  std::optional<Point> Viewport::pointer(float x,float y)const{
    Point p{
      (x*drawable_w/window_w-left)/scale,(y*drawable_h/window_h-top)/scale
    };
    if(p.x<0||p.y<0||p.x>=DesignWidth||p.y>=DesignHeight)return std::nullopt;
    return p;
  }
  int neighbor(const std::vector<Node>& nodes,int from,Direction d){
    if(nodes.empty())return -1;
    if(from<0||static_cast<size_t>(from)>=nodes.size())return 0;
    auto a=nodes[static_cast<size_t>(from)].rect.center();
    double best=std::numeric_limits<double>::max();
    int found=from;
    for(size_t i=0;i<nodes.size();++i){
      if(static_cast<int>(i)==from)continue;
      auto b=nodes[i].rect.center();
      double dx=b.x-a.x,dy=b.y-a.y,forward=0,side=0;
      switch(d){
        case Direction::Left:forward=-dx;
        side=std::abs(dy);
        break;
        case Direction::Right:forward=dx;
        side=std::abs(dy);
        break;
        case Direction::Up:forward=-dy;
        side=std::abs(dx);
        break;
        case Direction::Down:forward=dy;
        side=std::abs(dx);
        break;
      }
      if(forward<1)continue;
      // Row/column alignment beats shorter diagonals; empty staircase cells stay empty.
      double score=forward+side*2.5+(side>forward*1.2?1000:0);
      if(score<best){
        best=score;
        found=static_cast<int>(i);
      }
    }
    return found;
  }

void pop_utf8(std::string& s) { if(s.empty()) return; size_t i=s.size()-1; while(i>0 && (static_cast<unsigned char>(s[i])&0xc0)==0x80)--i; s.resize(i); }
}
