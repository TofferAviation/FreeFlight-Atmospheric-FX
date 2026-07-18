#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool download(const std::wstring& url, const fs::path& destination, std::wstring& error) {
    URL_COMPONENTSW parts{};parts.dwStructSize=sizeof(parts);wchar_t host[256]{},path[2048]{};
    parts.lpszHostName=host;parts.dwHostNameLength=255;parts.lpszUrlPath=path;parts.dwUrlPathLength=2047;
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS){error=L"Invalid update URL.";return false;}
    HINTERNET session=WinHttpOpen(L"FFAtmoUpdater/0.4",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    HINTERNET connection=session?WinHttpConnect(session,host,parts.nPort,0):nullptr;
    HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    bool ok=request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr);
    DWORD status=0,size=sizeof(status);if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)&&status==200;
    std::ofstream output(destination,std::ios::binary|std::ios::trunc);
    while(ok){DWORD available=0;if(!WinHttpQueryDataAvailable(request,&available)){ok=false;break;}if(!available)break;std::vector<char> block(available);DWORD read=0;if(!WinHttpReadData(request,block.data(),available,&read)){ok=false;break;}output.write(block.data(),read);}
    output.close();if(!ok)error=L"The update download failed.";if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);return ok;
}

std::string sha256(const fs::path& file) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD objectSize=0,resultSize=0,hashSize=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&resultSize,0);
    BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&hashSize),sizeof(hashSize),&resultSize,0);
    std::vector<unsigned char> object(objectSize),digest(hashSize);if(BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);return {};}
    std::ifstream input(file,std::ios::binary);std::vector<unsigned char> block(1024*1024);
    while(input){input.read(reinterpret_cast<char*>(block.data()),block.size());std::streamsize count=input.gcount();if(count>0&&BCryptHashData(hash,block.data(),static_cast<ULONG>(count),0)<0){input.close();BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return {};}}
    BCryptFinishHash(hash,digest.data(),hashSize,0);BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(unsigned char byte:digest)out<<std::setw(2)<<static_cast<unsigned>(byte);return out.str();
}

std::wstring psQuote(const fs::path& path){std::wstring s=path.wstring(),out=L"'";for(wchar_t c:s){out+=c;if(c==L'\'')out+=L'\'';}return out+L"'";}
bool expand(const fs::path& package,const fs::path& target){std::wstring command=L"-NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath "+psQuote(package)+L" -DestinationPath "+psQuote(target)+L" -Force\"";SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS;info.lpVerb=L"open";info.lpFile=L"powershell.exe";info.lpParameters=command.c_str();info.nShow=SW_HIDE;if(!ShellExecuteExW(&info))return false;WaitForSingleObject(info.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(info.hProcess,&code);CloseHandle(info.hProcess);return code==0;}
void fail(const std::wstring& message){MessageBoxW(nullptr,message.c_str(),L"FreeFlight Atmospheric FX Updater",MB_ICONERROR|MB_OK);}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=5){fail(L"Updater arguments are incomplete.");if(argv)LocalFree(argv);return 2;}
    const std::wstring url=argv[1];std::string expected;for(wchar_t c:std::wstring(argv[2]))expected.push_back(static_cast<char>(c));fs::path target=argv[3];DWORD pid=wcstoul(argv[4],nullptr,10);LocalFree(argv);
    if(HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid)){WaitForSingleObject(process,30000);CloseHandle(process);}
    fs::path package=fs::temp_directory_path()/L"FFAtmo-update.zip",staging=fs::temp_directory_path()/L"FFAtmo-update-stage",backup=fs::temp_directory_path()/L"FFAtmo-update-backup";std::error_code ec;fs::remove_all(staging,ec);fs::remove_all(backup,ec);fs::create_directories(staging,ec);fs::create_directories(backup,ec);
    std::wstring error;if(!download(url,package,error)){fail(error);return 3;}std::string actual=sha256(package);const auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};std::transform(actual.begin(),actual.end(),actual.begin(),lower);std::transform(expected.begin(),expected.end(),expected.begin(),lower);if(actual.empty()||actual!=expected){fail(L"The downloaded update failed SHA-256 verification. No files were installed.");return 4;}
    if(!expand(package,staging)){fail(L"The verified update package could not be extracted.");return 5;}
    std::vector<fs::path> replaced,newFiles;
    try{for(const auto& item:fs::recursive_directory_iterator(staging)){fs::path relative=fs::relative(item.path(),staging),destination=target/relative;if(item.is_directory()){fs::create_directories(destination);continue;}if(relative.filename()==L"FFAtmoUpdater.exe")continue;fs::create_directories(destination.parent_path());if(fs::exists(destination)){fs::path saved=backup/relative;fs::create_directories(saved.parent_path());fs::copy_file(destination,saved,fs::copy_options::overwrite_existing);replaced.push_back(relative);}else newFiles.push_back(relative);fs::copy_file(item.path(),destination,fs::copy_options::overwrite_existing);}}
    catch(const std::exception&){for(const fs::path& relative:newFiles)fs::remove(target/relative,ec);for(const fs::path& relative:replaced){fs::path saved=backup/relative,destination=target/relative;fs::create_directories(destination.parent_path(),ec);fs::copy_file(saved,destination,fs::copy_options::overwrite_existing,ec);}fail(L"The update could not replace all application files. The previous version was restored.");return 6;}
    fs::remove(package,ec);fs::remove_all(staging,ec);fs::remove_all(backup,ec);ShellExecuteW(nullptr,L"open",(target/L"FFAtmoCompanion.exe").c_str(),nullptr,target.c_str(),SW_SHOWNORMAL);return 0;
}
