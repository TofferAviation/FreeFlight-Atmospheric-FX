Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$path = "app/ImGuiCompanion.cpp"
if (-not (Test-Path $path)) { throw "Companion source not found: $path" }
$text = Get-Content $path -Raw
$newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }

if (-not $text.Contains("std::array<char,256> gLoginEmail")) {
    $anchor = "bool gLoginRemember = true;"
    if (-not $text.Contains($anchor)) { throw "Could not find the login globals anchor." }
    $globals = @"
bool gLoginRemember = true;
std::array<char,256> gLoginEmail {};
std::array<char,256> gLoginPassword {};
bool gLoginShowPassword = false;
std::string gLoginMessage;
"@
    $text = $text.Replace($anchor, $globals.TrimEnd())
}

$loginFunction = @'
void loginPage(const ffatmo::app::GithubAuthState& auth) {
    const ImVec2 available=ImGui::GetContentRegionAvail();const ImVec2 origin=ImGui::GetCursorScreenPos();
    const float scale=std::min(available.x/1672.0f,available.y/941.0f);
    const ImVec2 canvas(origin.x+(available.x-1672.0f*scale)*.5f,origin.y+(available.y-941.0f*scale)*.5f);
    ImDrawList* d=ImGui::GetWindowDrawList();
    auto image=[&](const char* key,float x,float y,float w,float h,ImU32 tint=IM_COL32_WHITE){if(TextureAsset* a=asset(key))d->AddImage((ImTextureID)(intptr_t)a->view,ImVec2(canvas.x+x*scale,canvas.y+y*scale),ImVec2(canvas.x+(x+w)*scale,canvas.y+(y+h)*scale),ImVec2(0,0),ImVec2(1,1),tint);};
    auto textAt=[&](float x,float y,const char* value,const ImVec4& color,float fontScale=1.0f){ImGui::SetCursorScreenPos(ImVec2(canvas.x+x*scale,canvas.y+y*scale));ImGui::SetWindowFontScale(fontScale*scale);ImGui::TextColored(color,"%s",value);ImGui::SetWindowFontScale(1);};
    auto centeredText=[&](float x,float y,float w,const char* value,const ImVec4& color,float fontScale=1.0f){ImGui::SetWindowFontScale(fontScale*scale);float width=ImGui::CalcTextSize(value).x;ImGui::SetCursorScreenPos(ImVec2(canvas.x+(x+w*.5f)*scale-width*.5f,canvas.y+y*scale));ImGui::TextColored(color,"%s",value);ImGui::SetWindowFontScale(1);};
    auto buttonRegion=[&](const char* id,float x,float y,float w,float h){ImGui::SetCursorScreenPos(ImVec2(canvas.x+x*scale,canvas.y+y*scale));ImGui::PushID(id);bool clicked=ImGui::InvisibleButton("##loginRegion",ImVec2(w*scale,h*scale));ImGui::PopID();return clicked;};

    image("login/branding/freeflight_atmospheric_fx_lockup.png",175,178,665,357);
    image("login/branding/tagline_atmosphere_refined.png",333,568,310,30);
    const char* featureIcons[]={"login/icons/color/volumetric_clouds.png","login/icons/color/dynamic_weather.png","login/icons/color/adaptive_lighting.png"};
    const char* featureNames[]={"Volumetric Clouds","Dynamic Weather","Adaptive Lighting"};
    const float featureX[]={185,390,590};
    for(int i=0;i<3;++i){image(featureIcons[i],featureX[i],760,42,42);textAt(featureX[i]+49,772,featureNames[i],C(104,188,243),.80f);if(i<2)image("login/controls/feature_divider_vertical.png",featureX[i]+182,751,1,74);}

    const float cardX=910,cardY=72,cardW=625,cardH=815,contentX=988,contentW=469;
    image("login/panels/login_card_empty.png",cardX,cardY,cardW,cardH);
    textAt(contentX,121,"Welcome back",C(232,241,250),1.78f);
    textAt(contentX,169,"Sign in to continue to Atmospheric FX",C(126,190,233),.88f);

    if(auth.phase==ffatmo::app::GithubAuthPhase::Checking){
        image("login/icons/color/secure_connection.png",contentX+20,335,58,58);
        textAt(contentX+98,339,"Connecting securely to GitHub",C(230,240,249),1.05f);
        textAt(contentX+98,373,"Checking for an encrypted session on this PC...",C(138,164,190),.75f);
    }else if(auth.phase==ffatmo::app::GithubAuthPhase::AwaitingUser){
        textAt(contentX,253,"AUTHORIZE THIS DEVICE",C(230,240,249),.90f);
        textAt(contentX,291,"Enter this one-time code on the GitHub page:",C(178,205,226),.78f);
        d->AddRectFilled(ImVec2(canvas.x+contentX*scale,canvas.y+342*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+450*scale),U(5,25,47,246),12*scale);
        d->AddRect(ImVec2(canvas.x+contentX*scale,canvas.y+342*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+450*scale),U(43,157,235),12*scale,0,1.3f*scale);
        centeredText(contentX,374,contentW,auth.userCode.c_str(),C(70,194,255),2.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+contentX*scale,canvas.y+486*scale));if(ImGui::Button("COPY CODE",ImVec2(224*scale,52*scale)))ImGui::SetClipboardText(auth.userCode.c_str());
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+(contentX+245)*scale,canvas.y+486*scale));if(ImGui::Button("OPEN GITHUB",ImVec2(224*scale,52*scale)))gGithubAuth.openVerificationPage();
        centeredText(contentX,572,contentW,"Waiting for approval in your browser...",C(138,164,190),.76f);
    }else{
        textAt(contentX,224,"Email address",C(222,234,245),.80f);
        textAt(contentX,328,"Password",C(222,234,245),.80f);

        ImGui::PushStyleColor(ImGuiCol_FrameBg,C(5,18,34,252));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,C(8,30,53,252));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,C(8,30,53,252));
        ImGui::PushStyleColor(ImGuiCol_Border,C(67,132,183));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1.0f*scale);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,8.0f*scale);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(15*scale,12*scale));
        ImGui::SetWindowFontScale(scale);
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+contentX*scale,canvas.y+250*scale));ImGui::SetNextItemWidth(contentW*scale);ImGui::InputTextWithHint("##standardEmail","pilot@freeflight.com",gLoginEmail.data(),gLoginEmail.size());
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+contentX*scale,canvas.y+354*scale));ImGui::SetNextItemWidth((contentW-54)*scale);ImGuiInputTextFlags passFlags=gLoginShowPassword?ImGuiInputTextFlags_None:ImGuiInputTextFlags_Password;ImGui::InputTextWithHint("##standardPassword","Password",gLoginPassword.data(),gLoginPassword.size(),passFlags);
        ImGui::SetWindowFontScale(1);ImGui::PopStyleVar(3);ImGui::PopStyleColor(4);

        image("icons/white/eye.png",contentX+430,365,25,25,gLoginShowPassword?U(78,190,255):U(183,211,231));
        if(buttonRegion("showPassword",contentX+414,350,55,55))gLoginShowPassword=!gLoginShowPassword;

        if(buttonRegion("rememberStandard",contentX,420,32,32))gLoginRemember=!gLoginRemember;
        image(gLoginRemember?"login/controls/checkbox_checked.png":"login/controls/checkbox_unchecked.png",contentX+2,423,23,23);
        textAt(contentX+34,425,"Remember me",C(208,224,238),.76f);
        textAt(contentX+328,425,"Forgot password?",C(48,163,255),.76f);
        if(buttonRegion("forgotPassword",contentX+318,416,151,34))gLoginMessage="Password recovery will be enabled with the future FreeFlight account service.";

        image("login/buttons/sign_in_button_empty.png",contentX,471,contentW,61);
        bool standardClicked=buttonRegion("standardLogin",contentX,471,contentW,61);
        if(ImGui::IsItemHovered())d->AddRect(ImVec2(canvas.x+contentX*scale,canvas.y+471*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+532*scale),U(255,211,88),11*scale,0,1.8f*scale);
        centeredText(contentX,491,contentW,"SIGN IN",C(240,247,253),.88f);
        if(standardClicked)gLoginMessage="Standard FreeFlight account login is ready visually and will activate when the account service is connected. Use GitHub below for now.";

        centeredText(contentX,557,contentW,"Need access?",C(210,224,237),.72f);
        textAt(contentX+283,557,"Contact support",C(48,163,255),.72f);
        if(buttonRegion("contactSupport",contentX+273,549,155,34))gLoginMessage="Support contact integration will be connected with the FreeFlight account service.";

        d->AddLine(ImVec2(canvas.x+contentX*scale,canvas.y+615*scale),ImVec2(canvas.x+(contentX+162)*scale,canvas.y+615*scale),U(72,101,130),1.0f*scale);
        d->AddLine(ImVec2(canvas.x+(contentX+307)*scale,canvas.y+615*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+615*scale),U(72,101,130),1.0f*scale);
        centeredText(contentX+162,604,145,"LOGIN WITH",C(147,174,199),.72f);

        d->AddRectFilled(ImVec2(canvas.x+contentX*scale,canvas.y+643*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+705*scale),U(4,17,33,248),10*scale);
        d->AddRect(ImVec2(canvas.x+contentX*scale,canvas.y+643*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+705*scale),U(82,150,201),10*scale,0,1.2f*scale);
        d->AddCircleFilled(ImVec2(canvas.x+(contentX+55)*scale,canvas.y+674*scale),20*scale,U(238,245,251));
        centeredText(contentX+35,662,40,"GH",C(4,17,33),.72f);
        centeredText(contentX,663,contentW,"CONTINUE WITH GITHUB",C(236,244,251),.83f);
        bool githubClicked=buttonRegion("githubLogin",contentX,643,contentW,62);
        if(ImGui::IsItemHovered())d->AddRect(ImVec2(canvas.x+contentX*scale,canvas.y+643*scale),ImVec2(canvas.x+(contentX+contentW)*scale,canvas.y+705*scale),U(61,183,255),10*scale,0,1.8f*scale);
        if(githubClicked)gGithubAuth.begin(gLoginRemember);

        centeredText(contentX,737,contentW,"FreeFlight LLC  -  Secure connection",C(121,151,178),.70f);
        const std::string message=auth.phase==ffatmo::app::GithubAuthPhase::Error?auth.error:gLoginMessage;
        if(!message.empty())centeredText(contentX-8,783,contentW+16,message.c_str(),auth.phase==ffatmo::app::GithubAuthPhase::Error?C(255,116,116):C(118,184,226),.62f);
    }

    textAt(1475,900,(std::string("Atmospheric FX v")+ffatmo::app::kAppVersion).c_str(),C(121,147,174),.72f);
}
'@

$pattern = '(?s)void loginPage\(const ffatmo::app::GithubAuthState& auth\) \{.*?\r?\n\}\r?\nvoid updateModal\(\)'
$matches = [regex]::Matches($text, $pattern)
if ($matches.Count -ne 1) { throw "Expected one loginPage block, found $($matches.Count)." }
$text = [regex]::Replace($text, $pattern, ($loginFunction.TrimEnd() + $newline + "void updateModal()"), 1)
Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
Write-Host "Applied asset-driven standard login shell and working GitHub device login."
