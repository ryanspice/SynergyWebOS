#include "synergy/ipc.hpp"
#include <fstream>
#include <iostream>
namespace synergy {
    int control_main(int argc,char** argv){
        auto root=default_runtime();
        std::string op,file;
        bool once=false;
        Json custom;
        for(int i=1;i<argc;++i){
            std::string a=argv[i];
            auto value=[&]{
                if(++i>=argc)throw std::runtime_error("Missing option value");
                return std::string(argv[i]);
            }
            ;
            if(a=="--runtime-dir")root=value();
            else if(a=="--file")file=value();
            else if(a=="--once")once=true;
            else if(a=="--json")custom=Json::parse(value(),16384);
            else if(a=="--version"||a=="version"){
                std::cout<<SYNERGY_VERSION<<'\n';
                return 0;
            }
            else if(a=="--help"){
                std::cout<<"synergy-tvctl [--runtime-dir ABS] status|snapshot|probe|start|restart|stop|logs|configure|profile-save|profile-load|autostart-enable|autostart-disable\nconfigure/profile-save: --file FILE; start: --once\n--json OBJECT accepts one explicit allowlisted local-control request.\n";
                return 0;
            }
            else if(op.empty())op=a;
            else throw std::runtime_error("Unknown option: "+a);
        }
        if(op.empty()&&custom.empty())op="status";
        auto j=custom.empty()?Json::object():custom;
        if(custom.empty()){
            j.set("op",Json::string(op));
            if(once)j.set("once",Json::boolean(true));
            if(op=="configure"||op=="profile-save"){
                if(file.empty())throw std::runtime_error("--file is required");
                std::ifstream f(file,std::ios::binary);
                if(!f)throw std::runtime_error("Cannot open input file");
                std::string s(16385,'\0');
                f.read(s.data(),static_cast<std::streamsize>(s.size()));
                s.resize(static_cast<size_t>(f.gcount()));
                j.set(op=="configure"?"config":"profile",Json::parse(s,16384));
            }
        }
        auto r=request(root,j);
        if(op=="snapshot"&&r.get("ok").flag()){
            auto c=r.get("client"),cfg=r.get("saved_config");
            std::cout<<"===STATE===\n"<<(c.get("running").flag()?"running pid=":"stopped pid=")<<static_cast<int>(r.get("pid").num())<<'\n'<<c.dump()<<"\n===AUTOSTART===\n"<<(r.get("autostart").flag()?"enabled":"disabled")<<"\n===CONFIG===\nhome_target="<<cfg.get("home_target").str()<<"\nhome_delay_ms="<<static_cast<int>(cfg.get("home_delay_ms").num())<<"\n===LOG===\n";
            auto logs=c.get("log");
            for(size_t i=0;i<logs.size();++i)std::cout<<logs.at(i).str()<<'\n';
        }
        else std::cout<<r.dump(true)<<'\n';
        return r.get("ok").flag()?0:1;
    }
}
int main(int argc,char** argv){
    try{
        return synergy::control_main(argc,argv);
    }
    catch(const std::exception& e){
        std::cerr<<"synergy-tvctl: "<<e.what()<<'\n';
        return 2;
    }
}
