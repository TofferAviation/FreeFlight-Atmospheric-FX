#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <d3d11.h>
#include <wincodec.h>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "Settings.h"
#include "TelemetryProvider.h"
#include "UpdateService.h"
#include "FFAtmoResource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using ffatmo::EffectSettings;
using ffatmo::QualityPreset;

// The backend intentionally leaves this declaration to the application so
// imgui_impl_win32.h does not need to include windows.h. Keep it in global
// scope so it links to imgui_impl_win32.cpp rather than FFAtmo's namespace.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwapChain = nullptr;
ID3D11RenderTargetView* gRenderTarget = nullptr;
UINT gPendingResizeWidth = 0;
UINT gPendingResizeHeight = 0;
HWND gWindow = nullptr;
ID3D11ShaderResourceView* gBrandTexture = nullptr;
UINT gBrandWidth = 0;
UINT gBrandHeight = 0;
ID3D11ShaderResourceView* gHeroTexture = nullptr;
UINT gHeroWidth = 0;
UINT gHeroHeight = 0;
struct TextureAsset { ID3D11ShaderResourceView* view=nullptr; UINT width=0; UINT height=0; };
std::unordered_map<std::string,TextureAsset> gAssets;

enum class Page { Overview, Effects, Weather, Performance, Aircraft, Advanced };
Page gPage = Page::Overview;
EffectSettings gSettings {};
ffatmo::CompanionStatus gStatus {};
std::unique_ptr<ffatmo::app::TelemetryProvider> gTelemetry;
fs::path gXPlaneRoot;
std::string gFooter = "Choose the X-Plane 12 folder to connect.";
std::chrono::steady_clock::time_point gLastPoll {};
ffatmo::app::UpdateService gUpdateService;
bool gAutomaticUpdates = false;

ImVec4 C(unsigned r, unsigned g, unsigned b, unsigned a=255) {
    return ImVec4(r/255.0f,g/255.0f,b/255.0f,a/255.0f);
}
ImU32 U(unsigned r, unsigned g, unsigned b, unsigned a=255) {
    return IM_COL32(r,g,b,a);
}

fs::path executablePath() {
    std::array<wchar_t, 32768> value{};
    GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    return value.data();
}
fs::path detectXPlane() {
    fs::path p = executablePath().parent_path();
    for (int i=0;i<7 && !p.empty();++i,p=p.parent_path())
        if (fs::exists(p/"Resources"/"plugins") && fs::exists(p/"Output"/"preferences")) return p;
    return {};
}
void connectRoot() {
    if (gXPlaneRoot.empty()) return;
    std::string message;
    ffatmo::SettingsStore::load((gXPlaneRoot/"Output"/"preferences"/"FFAtmo.ini").string(),gSettings,&message);
    gTelemetry = std::make_unique<ffatmo::app::FileBridgeTelemetry>(
        (gXPlaneRoot/"Output"/"preferences"/"FFAtmo.status.ini").string());
}
void saveSettings() {
    if (gXPlaneRoot.empty()) { gFooter="Choose the X-Plane folder before changing live settings."; return; }
    std::string error;
    if (ffatmo::SettingsStore::save((gXPlaneRoot/"Output"/"preferences"/"FFAtmo.ini").string(),gSettings,&error))
        gFooter="Settings applied live to FFAtmospherics.";
    else gFooter="Settings error: "+error;
}
void pollTelemetry() {
    auto now=std::chrono::steady_clock::now();
    if (now-gLastPoll<std::chrono::milliseconds(500)) return;
    gLastPoll=now;
    if (!gTelemetry) return;
    ffatmo::CompanionStatus next; std::string error;
    if (gTelemetry->read(next,&error)) { gStatus=next; gFooter=gStatus.pluginRunning?"X-Plane connected — telemetry live.":"Waiting for the X-Plane plugin."; }
}
void chooseXPlane() {
    BROWSEINFOW bi{}; bi.hwndOwner=gWindow; bi.lpszTitle=L"Select X-Plane 12 folder";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    if (PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&bi)) {
        wchar_t path[MAX_PATH]{};
        if (SHGetPathFromIDListW(id,path)) {
            fs::path p(path);
            if (fs::exists(p/"Resources"/"plugins")) { gXPlaneRoot=p; connectRoot(); }
            else gFooter="The selected folder is not an X-Plane 12 root.";
        }
        CoTaskMemFree(id);
    }
}

void applyTheme() {
    ImGuiStyle& s=ImGui::GetStyle();
    s.WindowRounding=0; s.ChildRounding=10; s.FrameRounding=7; s.PopupRounding=8;
    s.ScrollbarRounding=8; s.GrabRounding=8; s.FramePadding=ImVec2(11,8);
    s.ItemSpacing=ImVec2(10,9); s.WindowPadding=ImVec2(12,12); s.ChildBorderSize=1;
    s.Colors[ImGuiCol_WindowBg]=C(2,10,22); s.Colors[ImGuiCol_ChildBg]=C(4,18,34,232);
    s.Colors[ImGuiCol_Border]=C(28,65,96); s.Colors[ImGuiCol_Text]=C(235,243,251);
    s.Colors[ImGuiCol_TextDisabled]=C(132,153,176); s.Colors[ImGuiCol_FrameBg]=C(7,22,38);
    s.Colors[ImGuiCol_FrameBgHovered]=C(13,52,82); s.Colors[ImGuiCol_FrameBgActive]=C(17,72,112);
    s.Colors[ImGuiCol_Button]=C(10,40,68); s.Colors[ImGuiCol_ButtonHovered]=C(16,76,125);
    s.Colors[ImGuiCol_ButtonActive]=C(24,118,190); s.Colors[ImGuiCol_CheckMark]=C(56,168,255);
    s.Colors[ImGuiCol_SliderGrab]=C(50,179,255); s.Colors[ImGuiCol_Header]=C(16,74,122);
    s.Colors[ImGuiCol_HeaderHovered]=C(24,108,172); s.Colors[ImGuiCol_HeaderActive]=C(32,139,218);
}
bool panel(const char* id,const char* title,const ImVec2& size) {
    ImGui::BeginChild(id,size,true,ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(C(202,221,239),"%s",title); ImGui::Separator(); return true;
}
void endPanel(){ImGui::EndChild();}
bool toggle(const char* label,bool& value) {
    ImGui::PushID(label); ImGui::TextUnformatted(label); ImGui::SameLine(ImGui::GetContentRegionMax().x-48);
    ImVec2 p=ImGui::GetCursorScreenPos(); bool clicked=ImGui::InvisibleButton("##toggle",ImVec2(42,22));
    if(clicked)value=!value; ImDrawList* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(p,ImVec2(p.x+42,p.y+22),value?U(28,137,220):U(48,68,86),11);
    float x=value?p.x+31:p.x+11; d->AddCircleFilled(ImVec2(x,p.y+11),7,U(242,248,253)); ImGui::PopID(); return clicked;
}
void metric(const char* id,const char* label,const std::string& value,const ImVec4& accent) {
    panel(id,label,ImVec2(0,82)); ImGui::SetWindowFontScale(1.55f); ImGui::TextColored(accent,"%s",value.c_str());
    ImGui::SetWindowFontScale(1.0f); endPanel();
}
void drawBrandMark(ImDrawList* d,ImVec2 p) {
    ImU32 blue=U(26,126,255), cyan=U(25,205,255), gold=U(244,178,78);
    d->AddTriangleFilled(ImVec2(p.x,p.y),ImVec2(p.x+82,p.y),ImVec2(p.x+55,p.y+12),blue);
    d->AddTriangleFilled(ImVec2(p.x+26,p.y+18),ImVec2(p.x+70,p.y+18),ImVec2(p.x+48,p.y+28),cyan);
    d->AddBezierCubic(ImVec2(p.x+78,p.y),ImVec2(p.x+55,p.y+14),ImVec2(p.x+48,p.y+36),ImVec2(p.x+47,p.y+58),gold,7);
    d->AddBezierCubic(ImVec2(p.x+78,p.y),ImVec2(p.x+64,p.y+17),ImVec2(p.x+62,p.y+38),ImVec2(p.x+50,p.y+62),cyan,5);
}
bool loadTexture(const fs::path& path, ID3D11ShaderResourceView** output, UINT* outputWidth, UINT* outputHeight) {
    IWICImagingFactory* factory=nullptr; IWICBitmapDecoder* decoder=nullptr;
    IWICBitmapFrameDecode* frame=nullptr; IWICFormatConverter* converter=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,&decoder);
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
    if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);
    if(SUCCEEDED(hr))hr=converter->GetSize(outputWidth,outputHeight);
    std::vector<unsigned char> pixels;
    if(SUCCEEDED(hr)){pixels.resize(static_cast<size_t>(*outputWidth)*(*outputHeight)*4);
        hr=converter->CopyPixels(nullptr,*outputWidth*4,static_cast<UINT>(pixels.size()),pixels.data());}
    ID3D11Texture2D* texture=nullptr;
    if(SUCCEEDED(hr)){D3D11_TEXTURE2D_DESC desc{};desc.Width=*outputWidth;desc.Height=*outputHeight;
        desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=*outputWidth*4;
        hr=gDevice->CreateTexture2D(&desc,&data,&texture);}
    if(SUCCEEDED(hr)){D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
        hr=gDevice->CreateShaderResourceView(texture,&view,output);}
    if(texture)texture->Release();if(converter)converter->Release();if(frame)frame->Release();
    if(decoder)decoder->Release();if(factory)factory->Release();return SUCCEEDED(hr);
}
void loadAssetPack(const fs::path& root) {
    if(!fs::exists(root))return;
    for(const auto& item:fs::recursive_directory_iterator(root)){
        if(!item.is_regular_file()||item.path().extension()!=L".png")continue;
        TextureAsset asset; if(loadTexture(item.path(),&asset.view,&asset.width,&asset.height))
            gAssets[fs::relative(item.path(),root).generic_string()]=asset;
    }
}
TextureAsset* asset(const char* key){auto it=gAssets.find(key);return it==gAssets.end()?nullptr:&it->second;}
void assetImage(const char* key,ImVec2 size){if(TextureAsset* a=asset(key))ImGui::Image((ImTextureID)(intptr_t)a->view,size);else ImGui::Dummy(size);}
bool assetSliderFloat(const char* id,float* value,float minimum,float maximum) {
    ImGui::PushID(id);float width=ImGui::GetContentRegionAvail().x;ImVec2 p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##assetSlider",ImVec2(width,22));bool changed=false;float usable=std::max(1.0f,width-20.0f),startX=p.x+10.0f;
    if(ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        float ratio=std::clamp((ImGui::GetIO().MousePos.x-startX)/usable,0.0f,1.0f);
        float next=minimum+(maximum-minimum)*ratio;if(next!=*value){*value=next;changed=true;}
    }
    float ratio=std::clamp((*value-minimum)/(maximum-minimum),0.0f,1.0f);ImDrawList* d=ImGui::GetWindowDrawList();
    TextureAsset* track=asset("controls/slider_track.png");TextureAsset* fill=asset("controls/slider_fill_blue.png");TextureAsset* knob=asset("controls/slider_knob.png");
    ImVec2 a(p.x,p.y+8),b(p.x+width,p.y+14);if(track)d->AddImage((ImTextureID)(intptr_t)track->view,a,b);else d->AddRectFilled(a,b,U(25,52,78),3);
    if(ratio>0.001f){ImVec2 fb(startX+usable*ratio,p.y+14);if(fill)d->AddImage((ImTextureID)(intptr_t)fill->view,a,fb,ImVec2(0,0),ImVec2(ratio,1));else d->AddRectFilled(a,fb,U(32,145,255),3);}
    ImVec2 k(startX+usable*ratio-10,p.y+1);if(knob)d->AddImage((ImTextureID)(intptr_t)knob->view,k,ImVec2(k.x+20,k.y+20));else d->AddCircleFilled(ImVec2(k.x+10,k.y+10),7,U(235,245,255));
    ImGui::PopID();return changed;
}
void sidebar(float width) {
    ImGui::BeginChild("sidebar",ImVec2(width,0),false);
    ImDrawList* d=ImGui::GetWindowDrawList(); ImVec2 wp=ImGui::GetWindowPos();
    if(asset("branding/freeflight_atmospheric_fx_logo.png")){ImGui::SetCursorPos(ImVec2(20,18));assetImage("branding/freeflight_atmospheric_fx_logo.png",ImVec2(210,68));ImGui::Dummy(ImVec2(1,8));}
    else if(gBrandTexture){ImGui::SetCursorPos(ImVec2(22,14));ImGui::Image((ImTextureID)(intptr_t)gBrandTexture,ImVec2(width-44,88));}
    else {drawBrandMark(d,ImVec2(wp.x+26,wp.y+24));ImGui::Dummy(ImVec2(1,88));}
    ImGui::Dummy(ImVec2(1,10));
    ImGui::TextDisabled("  CONTROL CENTRE");
    ImGui::Spacing();
    const char* names[]={"Live Overview","Effects Control","Weather & Realism","Quality & Performance","Aircraft Profile","Advanced & Test"};
    const char* icons[]={"live_overview.png","sliders.png","weather.png","adaptive_quality.png","user.png","settings.png"};
    for(int i=0;i<6;++i){bool active=(int)gPage==i; if(active)ImGui::PushStyleColor(ImGuiCol_Button,C(10,54,96));ImGui::PushID(i);if(ImGui::Button("##nav",ImVec2(width-20,52)))gPage=(Page)i;ImVec2 mn=ImGui::GetItemRectMin();ImGui::SetCursorScreenPos(ImVec2(mn.x+15,mn.y+12));std::string key=std::string("icons/")+(active?"active_blue/":"white/")+icons[i];assetImage(key.c_str(),ImVec2(28,28));ImGui::SetCursorScreenPos(ImVec2(mn.x+55,mn.y+16));ImGui::TextColored(active?C(71,176,255):C(190,207,224),"%s",names[i]);ImGui::SetCursorScreenPos(ImVec2(mn.x,mn.y+52));ImGui::PopID();if(active)ImGui::PopStyleColor();}
    ImGui::SetCursorPosY(ImGui::GetWindowHeight()-82); if(ImGui::Button("Choose X-Plane folder",ImVec2(width-20,40)))chooseXPlane();
    ImGui::EndChild();
}
void drawAircraftWake(ImVec2 size) {
    ImVec2 p=ImGui::GetCursorScreenPos(); ImDrawList* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),U(5,18,33),8);
    ImVec2 c(p.x+size.x*.50f,p.y+size.y*.46f);
    for(int i=4;i>0;--i) d->AddCircle(c,54.0f*i,U(16,48,78,110),0,1.0f);
    const ImU32 body=U(226,236,245), shade=U(183,203,218), engine=U(51,84,108), blue=U(65,166,239);
    ImVec2 bodyPts[]={ImVec2(c.x,c.y-155),ImVec2(c.x-16,c.y-126),ImVec2(c.x-20,c.y+84),ImVec2(c.x-65,c.y+125),ImVec2(c.x,c.y+105),ImVec2(c.x+65,c.y+125),ImVec2(c.x+20,c.y+84),ImVec2(c.x+16,c.y-126)};
    d->AddConvexPolyFilled(bodyPts,8,body);
    ImVec2 leftWing[]={ImVec2(c.x-17,c.y-48),ImVec2(c.x-178,c.y+18),ImVec2(c.x-178,c.y+38),ImVec2(c.x-18,c.y-2)};
    ImVec2 rightWing[]={ImVec2(c.x+17,c.y-48),ImVec2(c.x+178,c.y+18),ImVec2(c.x+178,c.y+38),ImVec2(c.x+18,c.y-2)};
    d->AddConvexPolyFilled(leftWing,4,body);d->AddConvexPolyFilled(rightWing,4,shade);
    d->AddCircleFilled(ImVec2(c.x-35,c.y+24),15,engine);d->AddCircleFilled(ImVec2(c.x+35,c.y+24),15,engine);
    d->AddCircleFilled(ImVec2(c.x-35,c.y+24),8,blue);d->AddCircleFilled(ImVec2(c.x+35,c.y+24),8,blue);
    d->AddTriangleFilled(ImVec2(c.x-16,c.y+92),ImVec2(c.x,c.y+119),ImVec2(c.x,c.y+88),blue);
    d->AddTriangleFilled(ImVec2(c.x+16,c.y+92),ImVec2(c.x,c.y+119),ImVec2(c.x,c.y+88),U(43,201,247));
    float strength=std::clamp(gStatus.state.primaryWakeRatio,0.12f,1.0f); ImU32 wake=U(218,235,248,(unsigned)(80+strength*150));
    d->AddBezierCubic(ImVec2(c.x-35,c.y+40),ImVec2(c.x-44,c.y+105),ImVec2(c.x-45,c.y+152),ImVec2(c.x-48,c.y+size.y*.46f),wake,4+strength*7);
    d->AddBezierCubic(ImVec2(c.x+35,c.y+40),ImVec2(c.x+44,c.y+105),ImVec2(c.x+45,c.y+152),ImVec2(c.x+48,c.y+size.y*.46f),wake,4+strength*7);
    ImGui::Dummy(size);
}
void drawAircraftThumbnail(ImVec2 size) {
    ImVec2 p=ImGui::GetCursorScreenPos();ImDrawList* d=ImGui::GetWindowDrawList();
    ImVec2 c(p.x+size.x*.5f,p.y+size.y*.48f);float h=size.y*.43f,w=size.x*.40f;
    ImU32 body=U(224,235,244),shade=U(167,194,213),blue=U(53,164,240);
    ImVec2 fus[]={ImVec2(c.x,c.y-h),ImVec2(c.x-5,c.y-h*.72f),ImVec2(c.x-7,c.y+h*.62f),ImVec2(c.x-18,c.y+h),ImVec2(c.x,c.y+h*.83f),ImVec2(c.x+18,c.y+h),ImVec2(c.x+7,c.y+h*.62f),ImVec2(c.x+5,c.y-h*.72f)};
    ImVec2 lw[]={ImVec2(c.x-5,c.y-h*.12f),ImVec2(c.x-w,c.y+h*.20f),ImVec2(c.x-w,c.y+h*.34f),ImVec2(c.x-6,c.y+h*.13f)};
    ImVec2 rw[]={ImVec2(c.x+5,c.y-h*.12f),ImVec2(c.x+w,c.y+h*.20f),ImVec2(c.x+w,c.y+h*.34f),ImVec2(c.x+6,c.y+h*.13f)};
    d->AddConvexPolyFilled(lw,4,body);d->AddConvexPolyFilled(rw,4,shade);d->AddConvexPolyFilled(fus,8,body);
    d->AddCircleFilled(ImVec2(c.x-12,c.y+h*.22f),5,U(44,76,99));d->AddCircleFilled(ImVec2(c.x+12,c.y+h*.22f),5,U(44,76,99));
    d->AddTriangleFilled(ImVec2(c.x-7,c.y+h*.66f),ImVec2(c.x,c.y+h*.91f),ImVec2(c.x+7,c.y+h*.66f),blue);ImGui::Dummy(size);
}
void ringGauge(const char* label,float value,const char* valueText,ImU32 color,float radius=36.0f) {
    ImVec2 p=ImGui::GetCursorScreenPos();ImVec2 c(p.x+radius,p.y+radius);ImDrawList* d=ImGui::GetWindowDrawList();
    constexpr float kPi=3.14159265358979323846f;
    d->AddCircle(c,radius,U(31,55,78),64,7.0f);float start=-kPi*.5f,end=start+kPi*2*std::clamp(value,0.f,1.f);d->PathArcTo(c,radius,start,end,48);d->PathStroke(color,ImDrawFlags_None,7.0f);
    ImVec2 ts=ImGui::CalcTextSize(valueText);d->AddText(ImVec2(c.x-ts.x*.5f,c.y-ts.y*.68f),U(235,243,251),valueText);ImVec2 ls=ImGui::CalcTextSize(label);d->AddText(ImVec2(c.x-ls.x*.5f,c.y+8),U(143,164,184),label);ImGui::Dummy(ImVec2(radius*2,radius*2));
}
void overview() {
    ImGui::SetWindowFontScale(1.65f);ImGui::Text("Live Overview");ImGui::SetWindowFontScale(1);ImGui::TextColored(C(83,190,240),"Real-time atmospheric conditions and effects");
    float maxX=ImGui::GetContentRegionMax().x;ImGui::SameLine(maxX-310);ImGui::TextColored(gStatus.pluginRunning?C(96,222,105):C(150,168,188),gStatus.pluginRunning?"● X-Plane connected":"● Waiting for X-Plane");
    ImGui::Spacing();float avail=ImGui::GetContentRegionAvail().x,gap=14;bool changed=false;
    float topLeft=(avail-gap)*.46f,topRight=avail-gap-topLeft;
    ImGui::BeginChild("atmosphere",ImVec2(topLeft,260),true);ImGui::SetWindowFontScale(1.35f);ImGui::Text("Atmospheric Status");ImGui::SetWindowFontScale(1);ImGui::SameLine(ImGui::GetContentRegionMax().x-70);ImGui::TextColored(C(99,225,103),gStatus.pluginRunning?"LIVE":"STANDBY");
    TextureAsset* cloud=asset("previews/cloudscape_live_preview@2x.png");if(cloud||gHeroTexture){ImVec2 hp=ImGui::GetCursorScreenPos();float hw=ImGui::GetContentRegionAvail().x,hh=150;ImGui::Image((ImTextureID)(intptr_t)(cloud?cloud->view:gHeroTexture),ImVec2(hw,hh));ImGui::SetCursorScreenPos(ImVec2(hp.x+12,hp.y+hh-48));ImGui::BeginChild("heroOverlay",ImVec2(hw-24,38),true);assetImage("icons/white/eye.png",ImVec2(20,20));ImGui::SameLine();ImGui::Text("Visibility %.0f+ km",std::max(10.0f,10.0f+gStatus.input.altitudeFt/10000.0f));ImGui::SameLine();assetImage("icons/white/cloud.png",ImVec2(20,20));ImGui::SameLine();ImGui::Text("RH ice %.0f%%",gStatus.state.relativeHumidityIcePercent);ImGui::SameLine();ImGui::TextColored(C(84,190,249),"Preset %s",(int)gSettings.quality==3?"Ultra":(int)gSettings.quality==2?"High":(int)gSettings.quality==1?"Medium":"Performance");ImGui::EndChild();}else ImGui::Dummy(ImVec2(1,188));ImGui::EndChild();
    ImGui::SameLine(0,gap);ImGui::BeginChild("conditions",ImVec2(topRight,260),true);ImGui::SetWindowFontScale(1.35f);ImGui::Text("Current Conditions");ImGui::SetWindowFontScale(1);ImGui::Separator();float cw=ImGui::GetContentRegionAvail().x/5;auto condition=[&](const char* icon,const char* value,const char* label){ImGui::BeginGroup();assetImage(icon,ImVec2(28,28));ImGui::Text("%s",value);ImGui::TextDisabled("%s",label);ImGui::EndGroup();};char t[24],wind[32],pressure[24],humidity[24];std::snprintf(t,sizeof(t),"%.0f C",gStatus.input.ambientTemperatureC);std::snprintf(wind,sizeof(wind),"%.0f kt",gStatus.input.trueAirspeedMps*1.94384f);std::snprintf(pressure,sizeof(pressure),"%.0f hPa",gStatus.input.pressurePa/100);std::snprintf(humidity,sizeof(humidity),"%.0f%%",gStatus.state.relativeHumidityWaterPercent);condition("icons/active_blue/thermometer.png",t,"Temperature");ImGui::SameLine(cw);condition("icons/active_blue/wind.png",wind,"Airspeed");ImGui::SameLine(cw*2);condition("icons/active_blue/pressure.png",pressure,"Pressure");ImGui::SameLine(cw*3);condition("icons/active_blue/humidity.png",humidity,"Humidity");ImGui::SameLine(cw*4);condition("icons/active_blue/eye.png","10+ km","Visibility");ImGui::Spacing();ImGui::Separator();ImGui::TextDisabled("Aircraft atmosphere:  Mach %.2f  •  FL%d  •  Dew point %.0f C",gStatus.input.mach,(int)(gStatus.input.altitudeFt/100),gStatus.input.dewPointC);ImGui::EndChild();
    float midLeft=(avail-gap*2)*.35f,midCenter=(avail-gap*2)*.29f,midRight=avail-gap*2-midLeft-midCenter;
    ImGui::BeginChild("activeFx",ImVec2(midLeft,218),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Active Effects");ImGui::SetWindowFontScale(1);changed|=toggle("Engine contrails",gSettings.enabled);changed|=assetSliderFloat("contrailIntensity",&gSettings.contrailIntensity,0.0f,2.0f);bool vort=gSettings.wingVortexIntensity>.01f;if(toggle("Wingtip vortices",vort)){gSettings.wingVortexIntensity=vort?1.0f:0.0f;changed=true;}changed|=assetSliderFloat("vortexIntensity",&gSettings.wingVortexIntensity,0.0f,2.0f);bool wing=gSettings.wingCondensationIntensity>.01f;if(toggle("Over-wing vapour",wing)){gSettings.wingCondensationIntensity=wing?1.0f:0.0f;changed=true;}changed|=assetSliderFloat("wingIntensity",&gSettings.wingCondensationIntensity,0.0f,2.0f);ImGui::EndChild();
    ImGui::SameLine(0,gap);ImGui::BeginChild("performance",ImVec2(midCenter,218),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Performance");ImGui::SetWindowFontScale(1);char fps[16],gpu[16],load[16];std::snprintf(fps,sizeof(fps),"%.0f",gStatus.input.frameRateFps);std::snprintf(gpu,sizeof(gpu),"%.0f%%",gStatus.state.particleBudgetRatio*100);float cost=std::max(.1f,gStatus.state.particleBudgetRatio*3.5f);std::snprintf(load,sizeof(load),"%.1f",cost);ringGauge("FPS",std::min(gStatus.input.frameRateFps/60.f,1.f),fps,U(53,154,255));ImGui::SameLine();ringGauge("LOAD",gStatus.state.particleBudgetRatio,gpu,U(38,195,255));ImGui::SameLine();ringGauge("MS",cost/6,load,U(248,179,65));ImGui::TextColored(C(99,225,103),gStatus.input.frameRateFps>=gSettings.adaptiveTargetFps?"✓ Performance stable":"Adaptive protection active");ImGui::EndChild();
    ImGui::SameLine(0,gap);ImGui::BeginChild("timeline",ImVec2(midRight,218),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Atmospheric Timeline");ImGui::SetWindowFontScale(1);ImGui::TextDisabled("Condition trend from live aircraft data");const char* times[]={"Now","+5 min","+10 min","+15 min"};const char* weatherIcons[]={"icons/color/weather_partly_cloudy.png","icons/color/weather_clear.png","icons/color/weather_cloudy.png","icons/color/weather_night_cloudy.png"};for(int i=0;i<4;++i){if(i)ImGui::SameLine();ImGui::BeginChild((std::string("time")+std::to_string(i)).c_str(),ImVec2((midRight-52)/4,104),false);ImGui::Text("%s",times[i]);assetImage(weatherIcons[i],ImVec2(38,38));ImGui::Text("%.0f C",gStatus.input.ambientTemperatureC-i);ImGui::EndChild();}ImGui::ProgressBar(std::clamp(gStatus.state.persistenceProbability,0.f,1.f),ImVec2(-1,5),"");ImGui::EndChild();
    ImGui::BeginChild("aircraftFx",ImVec2(avail,176),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Aircraft FX");ImGui::SetWindowFontScale(1);if(asset("aircraft/lineage1000_top.png"))assetImage("aircraft/lineage1000_top.png",ImVec2(101,107));else drawAircraftThumbnail(ImVec2(110,105));ImGui::SameLine();ImGui::BeginGroup();ImGui::Text("%s",gStatus.profileName.empty()?"X-Crafts Lineage 1000":gStatus.profileName.c_str());ImGui::TextColored(C(99,225,103),gStatus.pluginRunning?"LIVE MODEL":"PROFILE READY");ImGui::TextDisabled("Profile-aware emitter geometry");ImGui::EndGroup();ImGui::SameLine();auto mini=[&](const char* label,float val,const char* text){ImGui::BeginChild(label,ImVec2(132,95),false);ImGui::TextDisabled("%s",label);ImGui::SetWindowFontScale(1.35f);ImGui::Text("%s",text);ImGui::SetWindowFontScale(1);ImGui::ProgressBar(std::clamp(val,0.f,1.f),ImVec2(-1,6),"");ImGui::EndChild();};char form[16],cond[16],particles[16],gpuMs[16];std::snprintf(form,sizeof(form),"%.0f%%",gStatus.state.formationProbability*100);std::snprintf(cond,sizeof(cond),"%.0f%%",gStatus.state.wingCondensationRatio*100);std::snprintf(particles,sizeof(particles),"%.0f%%",gStatus.state.particleBudgetRatio*100);std::snprintf(gpuMs,sizeof(gpuMs),"%.1f ms",cost);mini("Contrail probability",gStatus.state.formationProbability,form);ImGui::SameLine();mini("Wing condensation",gStatus.state.wingCondensationRatio,cond);ImGui::SameLine();mini("Particle load",gStatus.state.particleBudgetRatio,particles);ImGui::SameLine();mini("GPU effect cost",cost/6,gpuMs);ImGui::SameLine();ImGui::BeginGroup();ImGui::TextDisabled("ACTIVE EMITTERS");assetImage("icons/active_blue/engine_contrails.png",ImVec2(28,28));ImGui::SameLine();assetImage("icons/active_blue/wingtip_vortices.png",ImVec2(28,28));ImGui::SameLine();assetImage("icons/active_blue/overwing_sheet.png",ImVec2(28,28));if(ImGui::Button("OPEN AIRCRAFT FX",ImVec2(170,38)))gPage=Page::Aircraft;ImGui::EndGroup();ImGui::EndChild();
    ImGui::BeginChild("statusbar",ImVec2(avail,46),true);ImGui::TextColored(gSettings.automaticWeather?C(99,225,103):C(150,168,188),gSettings.automaticWeather?"✓ Weather sync active":"Weather sync disabled");ImGui::SameLine(avail*.38f);ImGui::TextColored(gStatus.pluginRunning?C(99,225,103):C(150,168,188),gStatus.pluginRunning?"◎ Telemetry online":"◎ Waiting for telemetry");ImGui::SameLine(avail*.72f);ImGui::TextDisabled("Atmospheric FX v0.4");ImGui::EndChild();if(changed)saveSettings();
}
void simplePage(const char* title,const char* sub) {ImGui::Text("%s",title);ImGui::TextDisabled("%s",sub);ImGui::Spacing();panel("work","FUNCTIONAL CONTROLS",ImVec2(0,0));ImGui::TextWrapped("This native ImGui page remains connected to FFAtmo. Its detailed visual redesign follows after Atmospheric Operations is approved.");endPanel();}
void updateModal() {
    ffatmo::app::UpdateInfo update=gUpdateService.snapshot();if(update.available)ImGui::OpenPopup("Update available");
    ImGui::SetNextWindowSize(ImVec2(650,0),ImGuiCond_Appearing);bool open=true;
    if(ImGui::BeginPopupModal("Update available",&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse)){
        ImGui::TextColored(C(56,190,255),"NEW VERSION");ImGui::SetWindowFontScale(1.8f);ImGui::Text("Update available");ImGui::SetWindowFontScale(1);
        ImGui::TextColored(C(73,196,244),"Atmospheric FX v%s is ready to install.",update.version.c_str());ImGui::Spacing();
        ImGui::BeginChild("versions",ImVec2(0,112),true);ImGui::TextDisabled("Current version");ImGui::SetWindowFontScale(1.5f);ImGui::Text("v%s",ffatmo::app::kAppVersion);ImGui::SetWindowFontScale(1);ImGui::SameLine(255);ImGui::TextColored(C(53,167,255),"-->");ImGui::SameLine(370);ImGui::BeginGroup();ImGui::TextDisabled("Latest version");ImGui::SetWindowFontScale(1.5f);ImGui::Text("v%s",update.version.c_str());ImGui::SetWindowFontScale(1);ImGui::EndGroup();if(update.sizeBytes)ImGui::TextDisabled("Download size  %.1f MB",update.sizeBytes/1048576.0);ImGui::EndChild();
        ImGui::Spacing();ImGui::Text("What's new");ImGui::BeginChild("notes",ImVec2(0,145),true);for(const std::string& note:update.notes)ImGui::TextColored(C(218,234,247),"✓  %s",note.c_str());ImGui::EndChild();
        ImGui::TextColored(C(78,188,255),"◇  Verified FreeFlight update");ImGui::Separator();ImGui::Checkbox("Install future updates automatically",&gAutomaticUpdates);ImGui::Spacing();
        if(ImGui::Button("REMIND ME LATER",ImVec2(250,50))){gUpdateService.dismissCurrent();ImGui::CloseCurrentPopup();}ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,C(17,91,218));if(ImGui::Button(update.previewOnly?"CLOSE PREVIEW":"UPDATE NOW",ImVec2(330,50))){if(update.previewOnly){gUpdateService.dismissCurrent();ImGui::CloseCurrentPopup();}else{std::string error;if(gUpdateService.launchUpdater(executablePath().parent_path().wstring(),&error))PostMessageW(gWindow,WM_CLOSE,0,0);else gFooter=error;}}ImGui::PopStyleColor();
        ImGui::TextDisabled(update.previewOnly?"TEST PREVIEW - no files will be downloaded or installed.":"X-Plane should be closed while plugin files are replaced.");ImGui::EndPopup();
    }
    if(!open)gUpdateService.dismissCurrent();
}
void frame() {
    pollTelemetry(); ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("FFAtmoRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    if(TextureAsset* bg=asset("backgrounds/background_atmospheric_clean.png")){ImVec2 p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)bg->view,p,ImVec2(p.x+s.x,p.y+s.y));}
    sidebar(245);ImGui::SameLine(0,14);ImGui::BeginChild("content",ImVec2(0,0),false);
    switch(gPage){case Page::Overview:overview();break;case Page::Effects:simplePage("Effects Control","Shape every visible vapour layer");break;case Page::Weather:simplePage("Weather & Realism","Atmospheric triggering and persistence logic");break;case Page::Performance:simplePage("Quality & Performance","Particle budget and frame-time protection");break;case Page::Aircraft:simplePage("Aircraft Profile","Emitter calibration and integration status");break;case Page::Advanced:simplePage("Advanced & Test","Diagnostics and forced-effect controls");break;}ImGui::EndChild();updateModal();ImGui::End();
}
void cleanupTarget(){if(gRenderTarget){gRenderTarget->Release();gRenderTarget=nullptr;}}
void createTarget(){ID3D11Texture2D* back=nullptr;gSwapChain->GetBuffer(0,IID_PPV_ARGS(&back));if(back){gDevice->CreateRenderTargetView(back,nullptr,&gRenderTarget);back->Release();}}
bool createDevice(HWND h){DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=h;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;D3D_FEATURE_LEVEL fl;D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};if(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,2,D3D11_SDK_VERSION,&sd,&gSwapChain,&gDevice,&fl,&gContext)!=S_OK)return false;createTarget();return true;}
LRESULT WINAPI wndProc(HWND h,UINT m,WPARAM w,LPARAM l){if(::ImGui_ImplWin32_WndProcHandler(h,m,w,l))return true;switch(m){case WM_SIZE:if(gDevice&&w!=SIZE_MINIMIZED){gPendingResizeWidth=static_cast<UINT>(LOWORD(l));gPendingResizeHeight=static_cast<UINT>(HIWORD(l));}return 0;case WM_SYSCOMMAND:if((w&0xfff0)==SC_KEYMENU)return 0;return DefWindowProcW(h,m,w,l);case WM_DESTROY:PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,PWSTR,int){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);WNDCLASSEXW wc{sizeof(wc),CS_CLASSDC,wndProc,0,0,hi,LoadIconW(hi,MAKEINTRESOURCEW(IDI_APP_ICON)),LoadCursorW(nullptr,IDC_ARROW),nullptr,nullptr,L"FFAtmoImGui",LoadIconW(hi,MAKEINTRESOURCEW(IDI_APP_ICON))};RegisterClassExW(&wc);gWindow=CreateWindowW(wc.lpszClassName,L"FreeFlight Atmospheric FX",WS_OVERLAPPEDWINDOW,40,30,1680,980,nullptr,nullptr,hi,nullptr);if(!createDevice(gWindow))return 1;fs::path uiRoot=executablePath().parent_path()/"ui";loadTexture(uiRoot/"branding"/"FFAtmo_lockup.png",&gBrandTexture,&gBrandWidth,&gBrandHeight);loadTexture(uiRoot/"atmosphere"/"overview_clouds.png",&gHeroTexture,&gHeroWidth,&gHeroHeight);loadAssetPack(uiRoot/"assets");ShowWindow(gWindow,SW_SHOWDEFAULT);UpdateWindow(gWindow);IMGUI_CHECKVERSION();ImGui::CreateContext();applyTheme();ImGuiIO& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.IniFilename=nullptr;io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",16);ImGui_ImplWin32_Init(gWindow);ImGui_ImplDX11_Init(gDevice,gContext);gXPlaneRoot=detectXPlane();connectRoot();gUpdateService.start("stable");MSG msg{};while(msg.message!=WM_QUIT){if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);continue;}if(gPendingResizeWidth>0&&gPendingResizeHeight>0){cleanupTarget();if(SUCCEEDED(gSwapChain->ResizeBuffers(0,gPendingResizeWidth,gPendingResizeHeight,DXGI_FORMAT_UNKNOWN,0)))createTarget();gPendingResizeWidth=0;gPendingResizeHeight=0;}if(!gRenderTarget)continue;ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();frame();ImGui::Render();const float clear[4]={.01f,.035f,.07f,1};gContext->OMSetRenderTargets(1,&gRenderTarget,nullptr);gContext->ClearRenderTargetView(gRenderTarget,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());gSwapChain->Present(1,0);}for(auto& entry:gAssets)if(entry.second.view)entry.second.view->Release();gAssets.clear();if(gHeroTexture)gHeroTexture->Release();if(gBrandTexture)gBrandTexture->Release();ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();cleanupTarget();if(gSwapChain)gSwapChain->Release();if(gContext)gContext->Release();if(gDevice)gDevice->Release();DestroyWindow(gWindow);UnregisterClassW(wc.lpszClassName,hi);CoUninitialize();return 0;}
