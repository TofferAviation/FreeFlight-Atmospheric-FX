#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GithubAuth.h"

#include <windows.h>
#include <dpapi.h>
#include <shellapi.h>
#include <winhttp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ffatmo::app {
namespace {
constexpr const char* kClientId = "Ov23liMvrLCyQBmMPngE";

std::wstring widen(const std::string& value){if(value.empty())return{};int n=MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,nullptr,0);std::wstring out(static_cast<size_t>(n),L'\0');MultiByteToWideChar(CP_UTF8,0,value.c_str(),-1,out.data(),n);out.resize(static_cast<size_t>(n-1));return out;}
std::string jsonString(const std::string& json,const char* key){std::string token="\""+std::string(key)+"\"";size_t p=json.find(token);if(p==std::string::npos||(p=json.find(':',p+token.size()))==std::string::npos)return{};p=json.find('"',p+1);if(p==std::string::npos)return{};std::string out;for(++p;p<json.size();++p){if(json[p]=='"')break;if(json[p]=='\\'&&p+1<json.size())++p;out.push_back(json[p]);}return out;}
int jsonInt(const std::string& json,const char* key,int fallback){std::string token="\""+std::string(key)+"\"";size_t p=json.find(token);if(p==std::string::npos||(p=json.find(':',p+token.size()))==std::string::npos)return fallback;try{return std::stoi(json.substr(p+1));}catch(...){return fallback;}}

bool request(const std::wstring& host,const std::wstring& path,const wchar_t* verb,const std::string& body,const std::wstring& extraHeaders,std::string& response,DWORD& status){
    HINTERNET session=WinHttpOpen(L"FFAtmoAuth/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);HINTERNET connection=session?WinHttpConnect(session,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0):nullptr;HINTERNET req=connection?WinHttpOpenRequest(connection,verb,path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    std::wstring headers=L"Accept: application/json\r\nUser-Agent: FreeFlight-Atmospheric-FX\r\n"+extraHeaders;bool ok=req&&WinHttpSendRequest(req,headers.c_str(),static_cast<DWORD>(-1),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)&&WinHttpReceiveResponse(req,nullptr);DWORD size=sizeof(status);if(ok)ok=WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr);
    while(ok){DWORD available=0;if(!WinHttpQueryDataAvailable(req,&available)){ok=false;break;}if(!available)break;size_t offset=response.size();response.resize(offset+available);DWORD read=0;if(!WinHttpReadData(req,response.data()+offset,available,&read)){ok=false;break;}response.resize(offset+read);}if(req)WinHttpCloseHandle(req);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);return ok;
}

fs::path tokenPath(){wchar_t value[MAX_PATH]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",value,MAX_PATH);fs::path root=n?fs::path(value):fs::temp_directory_path();return root/L"FreeFlight"/L"AtmosphericFX"/L"github.session";}
bool saveToken(const std::string& token){DATA_BLOB input{static_cast<DWORD>(token.size()),reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))},output{};if(!CryptProtectData(&input,L"FFAtmo GitHub session",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))return false;fs::path path=tokenPath();std::error_code ec;fs::create_directories(path.parent_path(),ec);std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);return file.good();}
std::string loadToken(){std::ifstream file(tokenPath(),std::ios::binary);if(!file)return{};std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.empty())return{};DATA_BLOB input{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},output{};if(!CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output))return{};std::string token(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);return token;}
}

GithubAuth::~GithubAuth(){if(worker_.joinable())worker_.join();}
void GithubAuth::setState(GithubAuthState state){std::lock_guard<std::mutex> lock(mutex_);state_=std::move(state);}
GithubAuthState GithubAuth::snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return state_;}
void GithubAuth::start(){std::string token=loadToken();if(token.empty())return;setState({GithubAuthPhase::Checking});worker_=std::thread([this,token]{if(!validateToken(token,true)){std::error_code ec;fs::remove(tokenPath(),ec);setState({GithubAuthPhase::SignedOut});}});}
void GithubAuth::begin(bool remember){if(worker_.joinable())worker_.join();setState({GithubAuthPhase::Checking});worker_=std::thread([this,remember]{authenticateDevice(remember);});}
void GithubAuth::openVerificationPage()const{ShellExecuteW(nullptr,L"open",L"https://github.com/login/device",nullptr,nullptr,SW_SHOWNORMAL);}
void GithubAuth::signOut(){if(worker_.joinable())worker_.join();std::error_code ec;fs::remove(tokenPath(),ec);setState({GithubAuthPhase::SignedOut});}

bool GithubAuth::validateToken(const std::string& token,bool remember){std::string response;DWORD status=0;std::wstring auth=L"Authorization: Bearer "+widen(token)+L"\r\nX-GitHub-Api-Version: 2026-03-10\r\n";if(!request(L"api.github.com",L"/user",L"GET",{},auth,response,status)||status!=200)return false;GithubAuthState state;state.phase=GithubAuthPhase::Authorized;state.login=jsonString(response,"login");state.displayName=jsonString(response,"name");state.avatarUrl=jsonString(response,"avatar_url");if(state.displayName.empty())state.displayName=state.login;if(remember)saveToken(token);setState(std::move(state));return true;}

void GithubAuth::authenticateDevice(bool remember){std::string response;DWORD status=0;std::string body="client_id="+std::string(kClientId)+"&scope=read%3Auser";if(!request(L"github.com",L"/login/device/code",L"POST",body,L"Content-Type: application/x-www-form-urlencoded\r\n",response,status)||status!=200){setState({GithubAuthPhase::Error,{}, {}, {}, {}, {},"Could not contact GitHub."});return;}std::string device=jsonString(response,"device_code"),user=jsonString(response,"user_code"),url=jsonString(response,"verification_uri");int interval=jsonInt(response,"interval",5),expires=jsonInt(response,"expires_in",900);if(device.empty()||user.empty()){setState({GithubAuthPhase::Error,{}, {}, {}, {}, {},"GitHub did not return a device code."});return;}GithubAuthState awaiting;awaiting.phase=GithubAuthPhase::AwaitingUser;awaiting.userCode=user;awaiting.verificationUrl=url.empty()?"https://github.com/login/device":url;setState(awaiting);openVerificationPage();int elapsed=0;while(elapsed<expires){std::this_thread::sleep_for(std::chrono::seconds(interval));elapsed+=interval;std::ostringstream form;form<<"client_id="<<kClientId<<"&device_code="<<device<<"&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";response.clear();status=0;if(!request(L"github.com",L"/login/oauth/access_token",L"POST",form.str(),L"Content-Type: application/x-www-form-urlencoded\r\n",response,status))continue;std::string token=jsonString(response,"access_token"),error=jsonString(response,"error");if(!token.empty()){if(!validateToken(token,remember))setState({GithubAuthPhase::Error,{}, {}, {}, {}, {},"GitHub identity verification failed."});return;}if(error=="slow_down")interval+=5;else if(error=="access_denied"){setState({GithubAuthPhase::Error,{}, {}, {}, {}, {},"Access was denied on GitHub."});return;}else if(error=="expired_token")break;}setState({GithubAuthPhase::Error,{}, {}, {}, {}, {},"The GitHub authorization code expired."});}

}  // namespace ffatmo::app
