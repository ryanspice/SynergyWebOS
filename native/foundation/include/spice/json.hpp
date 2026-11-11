#pragma once
#include <json-c/json.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace spice {
  class Json {
    std::shared_ptr<json_object> ptr_;
    explicit Json(json_object* p,bool retain):ptr_(retain&&p?json_object_get(p):p,json_object_put){}
    public:
    Json():Json(nullptr,false){}
    static Json parse(std::string_view text,size_t limit=131072) {
      if(text.size()>limit) throw std::runtime_error("JSON exceeds size limit");
      std::unique_ptr<json_tokener,decltype(&json_tokener_free)> tok(json_tokener_new_ex(32),json_tokener_free);
      if(!tok) throw std::bad_alloc();
      json_tokener_set_flags(tok.get(),JSON_TOKENER_STRICT|JSON_TOKENER_VALIDATE_UTF8);
      auto* raw=json_tokener_parse_ex(tok.get(),text.data(),static_cast<int>(text.size()));
      Json result(raw,false);
      if(json_tokener_get_error(tok.get())!=json_tokener_success) throw std::runtime_error("Invalid JSON");
      for(size_t i=json_tokener_get_parse_end(tok.get());i<text.size();++i)
      if(text[i]!=' '&&text[i]!='\n'&&text[i]!='\r'&&text[i]!='\t') throw std::runtime_error("Trailing JSON data");
      return result;
    }
    static Json object(){
      return Json(json_object_new_object(),false);
    }
    static Json array(){
      return Json(json_object_new_array(),false);
    }
    static Json string(const std::string& s){
      return Json(json_object_new_string_len(s.data(),static_cast<int>(s.size())),false);
    }
    static Json number(double n){
      return Json(json_object_new_double(n),false);
    }
    static Json boolean(bool b){
      return Json(json_object_new_boolean(b),false);
    }
    bool is(json_type t)const{
      return ptr_&&json_object_is_type(ptr_.get(),t);
    }
    bool object_type()const{
      return is(json_type_object);
    }
    bool empty()const{
      return !ptr_;
    }
    Json get(const char* key)const{
      json_object* out=nullptr;
      if(object_type())json_object_object_get_ex(ptr_.get(),key,&out);
      return Json(out,true);
    }
    std::string str(std::string fallback={}
    )const{
      return is(json_type_string)?std::string(json_object_get_string(ptr_.get()),static_cast<size_t>(json_object_get_string_len(ptr_.get()))):fallback;
    }
    double num(double fallback=0)const{
      return is(json_type_double)||is(json_type_int)?json_object_get_double(ptr_.get()):fallback;
    }
    bool flag(bool fallback=false)const{
      return is(json_type_boolean)?json_object_get_boolean(ptr_.get())!=0:fallback;
    }
    size_t size()const{
      return is(json_type_array)?json_object_array_length(ptr_.get()):0;
    }
    Json at(size_t i)const{
      return Json(i<size()?json_object_array_get_idx(ptr_.get(),i):nullptr,true);
    }
    void set(const char* key,const Json& v){
      if(!object_type())throw std::runtime_error("Not a JSON object");
      json_object_object_add(ptr_.get(),key,v.ptr_?json_object_get(v.ptr_.get()):nullptr);
    }
    void add(const Json& v){
      if(!is(json_type_array))throw std::runtime_error("Not a JSON array");
      json_object_array_add(ptr_.get(),v.ptr_?json_object_get(v.ptr_.get()):nullptr);
    }
    std::vector<std::pair<std::string,Json>> members()const{
      std::vector<std::pair<std::string,Json>> out;
      if(!object_type())return out;
      json_object_object_foreach(ptr_.get(),key,val){
        out.emplace_back(key,Json(val,true));
      }
      return out;
    }
    std::string dump(bool pretty=false)const{
      return ptr_?json_object_to_json_string_ext(ptr_.get(),pretty?JSON_C_TO_STRING_PRETTY:JSON_C_TO_STRING_PLAIN):"null";
    }
  };
}
