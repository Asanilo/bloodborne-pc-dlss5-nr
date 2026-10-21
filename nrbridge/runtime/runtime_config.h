#pragma once
#include <json.hpp>
#include <set>
#include <stdexcept>
#include <string>
namespace NrBridgeRuntime {
struct Resources {
 std::string model, forwarder, ngx_dir, ge_files;
};
inline bool AbsolutePath(const std::string& s) {
 if(s.size()<2||s.front()!='/'||s.back()=='/'||s.find("//")!=std::string::npos)return false;
 for(unsigned char c:s)if(c<32||c>126||c=='\\'||c==':')return false;
 size_t start=1;for(size_t end=1;end<=s.size();++end)if(end==s.size()||s[end]=='/'){
  auto part=s.substr(start,end-start);if(part=="."||part=="..")return false;start=end+1;
 }return true;
}
inline Resources Parse(const nlohmann::json& j) {
 const std::set<std::string> keys={"schema","model","forwarder","ngx_dir","ge_files"};
 if(!j.is_object()||j.size()!=keys.size())throw std::runtime_error("runtime config exact object");
 for(const auto& key:keys)if(!j.contains(key)||!j.at(key).is_string())throw std::runtime_error("runtime config string field");
 if(j.at("schema")!="nrbridge-helper-runtime-v1")throw std::runtime_error("runtime config schema");
 Resources r;r.model=j.at("model");r.forwarder=j.at("forwarder");r.ngx_dir=j.at("ngx_dir");r.ge_files=j.at("ge_files");
 for(const auto* p:{&r.model,&r.forwarder,&r.ngx_dir,&r.ge_files})if(!AbsolutePath(*p))throw std::runtime_error("runtime config absolute canonical ASCII Linux path");
 return r;
}
}
