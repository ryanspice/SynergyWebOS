#pragma once
#include "synergy/ipc.hpp"
namespace synergy {
inline constexpr char Ls2ClientName[]="dev.canopy.synergywebos.ui-control";
Json ls2_request(const Json&,int timeout_ms=1200);
int ls2_service_main(int argc,char** argv);
}
