#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace spice {
constexpr float DesignWidth=1920,DesignHeight=1080;
struct Point { float x=0,y=0; };
struct Rect { float x=0,y=0,w=0,h=0; bool contains(Point p)const{return p.x>=x&&p.y>=y&&p.x<x+w&&p.y<y+h;} Point center()const{return {x+w*.5f,y+h*.5f};} };
struct Viewport {int window_w=1920,window_h=1080,drawable_w=1920,drawable_h=1080;float scale=1,left=0,top=0;static Viewport make(int,int,int,int);std::optional<Point> pointer(float,float)const;};
struct Node {std::string id;Rect rect;};
enum class Direction {Left,Right,Up,Down};
int neighbor(const std::vector<Node>&,int,Direction);
void pop_utf8(std::string&);
}
