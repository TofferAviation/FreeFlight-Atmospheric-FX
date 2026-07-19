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

$pngOnly = 'if(!item.is_regular_file()||item.path().extension()!=L".png")continue;'
$multiImage = 'auto ext=item.path().extension();if(!item.is_regular_file()||(ext!=L".png"&&ext!=L".jpg"&&ext!=L".jpeg"))continue;'
if ($text.Contains($pngOnly)) {
    $text = $text.Replace($pngOnly, $multiImage)
} elseif (-not $text.Contains($multiImage)) {
    throw "Could not update the asset loader for JPG login artwork."
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

    image("login/backgrounds/login_page_v2.jpg",0,0,1672,941);

    if(auth.phase==ffatmo::app::GithubAuthPhase::Checking){
        d->AddRectFilled(ImVec2(canvas.x+975*scale,canvas.y+285*scale),ImVec2(canvas.x+1445*scale,canvas.y+735*scale),U(2,14,30,248),18*scale);
        d->AddRect(ImVec2(canvas.x+975*scale,canvas.y+285*scale),ImVec2(canvas.x+1445*scale,canvas.y+735*scale),U(43,154,235),18*scale,0,1.5f*scale);
        centeredText(995,420,430,"CONNECTING TO GITHUB",C(225,239,251),1.15f);
        centeredText(995,463,430,"Checking for an encrypted session on this PC...",C(132,177,209),.78f);
    }else if(auth.phase==ffatmo::app::GithubAuthPhase::AwaitingUser){
        d->AddRectFilled(ImVec2(canvas.x+960*scale,canvas.y+245*scale),ImVec2(canvas.x+1460*scale,canvas.y+785*scale),U(2,14,30,250),18*scale);
        d->AddRect(ImVec2(canvas.x+960*scale,canvas.y+245*scale),ImVec2(canvas.x+1460*scale,canvas.y+785*scale),U(43,154,235),18*scale,0,1.5f*scale);
        centeredText(985,302,450,"AUTHORIZE THIS DEVICE",C(225,239,251),1.15f);
        centeredText(985,347,450,"Enter this one-time code on the GitHub page",C(147,191,222),.78f);
        d->AddRectFilled(ImVec2(canvas.x+1010*scale,canvas.y+395*scale),ImVec2(canvas.x+1410*scale,canvas.y+500*scale),U(5,27,51,255),12*scale);
        d->AddRect(ImVec2(canvas.x+1010*scale,canvas.y+395*scale),ImVec2(canvas.x+1410*scale,canvas.y+500*scale),U(48,168,247),12*scale,0,1.3f*scale);
        centeredText(1010,424,400,auth.userCode.c_str(),C(73,195,255),2.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+1010*scale,canvas.y+535*scale));if(ImGui::Button("COPY CODE",ImVec2(190*scale,52*scale)))ImGui::SetClipboardText(auth.userCode.c_str());
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+1220*scale,canvas.y+535*scale));if(ImGui::Button("OPEN GITHUB",ImVec2(190*scale,52*scale)))gGithubAuth.openVerificationPage();
        centeredText(985,625,450,"Waiting for approval in your browser...",C(132,177,209),.78f);
    }else{
        const float emailX=1040,emailY=296,emailW=350,emailH=43;
        const float passX=1040,passY=410,passW=330,passH=43;
        d->AddRectFilled(ImVec2(canvas.x+emailX*scale,canvas.y+emailY*scale),ImVec2(canvas.x+(emailX+emailW)*scale,canvas.y+(emailY+emailH)*scale),U(5,18,34,252),6*scale);
        d->AddRectFilled(ImVec2(canvas.x+passX*scale,canvas.y+passY*scale),ImVec2(canvas.x+(passX+passW)*scale,canvas.y+(passY+passH)*scale),U(5,18,34,252),6*scale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,C(5,18,34,252));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,C(7,28,50,252));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,C(7,28,50,252));
        ImGui::PushStyleColor(ImGuiCol_Border,C(5,18,34,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,6*scale);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8*scale,9*scale));
        ImGui::SetWindowFontScale(scale);
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+emailX*scale,canvas.y+emailY*scale));ImGui::SetNextItemWidth(emailW*scale);ImGui::InputTextWithHint("##standardEmail","pilot@freeflight.com",gLoginEmail.data(),gLoginEmail.size());
        ImGui::SetCursorScreenPos(ImVec2(canvas.x+passX*scale,canvas.y+passY*scale));ImGui::SetNextItemWidth(passW*scale);ImGuiInputTextFlags passFlags=gLoginShowPassword?ImGuiInputTextFlags_None:ImGuiInputTextFlags_Password;ImGui::InputTextWithHint("##standardPassword","Password",gLoginPassword.data(),gLoginPassword.size(),passFlags);
        ImGui::SetWindowFontScale(1);ImGui::PopStyleVar(3);ImGui::PopStyleColor(4);

        if(buttonRegion("showPassword",1375,404,55,55))gLoginShowPassword=!gLoginShowPassword;
        if(buttonRegion("rememberStandard",980,478,34,34))gLoginRemember=!gLoginRemember;
        image(gLoginRemember?"login/controls/checkbox_checked.png":"login/controls/checkbox_unchecked.png",984,483,22,22);

        if(buttonRegion("standardLogin",983,539,454,63)){
            gLoginMessage="Standard FreeFlight account login is reserved for a future authentication service. Use GitHub below for now.";
        }
        if(ImGui::IsItemHovered())d->AddRect(ImVec2(canvas.x+983*scale,canvas.y+539*scale),ImVec2(canvas.x+1437*scale,canvas.y+602*scale),U(255,210,86),11*scale,0,1.8f*scale);
        if(buttonRegion("forgotPassword",1290,475,155,35))gLoginMessage="Password recovery will become available when standard FreeFlight accounts are enabled.";
        if(buttonRegion("contactSupport",1175,617,155,35))gLoginMessage="Support contact integration will be added with the FreeFlight account service.";

        d->AddRectFilled(ImVec2(canvas.x+983*scale,canvas.y+660*scale),ImVec2(canvas.x+1437*scale,canvas.y+704*scale),U(4,16,31,255));
        d->AddLine(ImVec2(canvas.x+983*scale,canvas.y+682*scale),ImVec2(canvas.x+1138*scale,canvas.y+682*scale),U(86,112,141),1.0f*scale);
        d->AddLine(ImVec2(canvas.x+1282*scale,canvas.y+682*scale),ImVec2(canvas.x+1437*scale,canvas.y+682*scale),U(86,112,141),1.0f*scale);
        centeredText(1138,671,144,"LOGIN WITH",C(151,178,203),.72f);

        if(buttonRegion("githubLogin",983,715,454,62))gGithubAuth.begin(gLoginRemember);
        if(ImGui::IsItemHovered())d->AddRect(ImVec2(canvas.x+983*scale,canvas.y+715*scale),ImVec2(canvas.x+1437*scale,canvas.y+777*scale),U(61,183,255),10*scale,0,1.8f*scale);

        const std::string message=auth.phase==ffatmo::app::GithubAuthPhase::Error?auth.error:gLoginMessage;
        if(!message.empty())centeredText(965,833,500,message.c_str(),auth.phase==ffatmo::app::GithubAuthPhase::Error?C(255,116,116):C(128,185,225),.67f);
    }

    d->AddRectFilled(ImVec2(canvas.x+1450*scale,canvas.y+875*scale),ImVec2(canvas.x+1668*scale,canvas.y+935*scale),U(1,7,16,238));
    textAt(1472,895,(std::string("Atmospheric FX v")+ffatmo::app::kAppVersion).c_str(),C(121,147,174),.72f);
}
'@

$pattern = '(?s)void loginPage\(const ffatmo::app::GithubAuthState& auth\) \{.*?\r?\n\}\r?\nvoid updateModal\(\)'
$matches = [regex]::Matches($text, $pattern)
if ($matches.Count -ne 1) { throw "Expected one loginPage block, found $($matches.Count)." }
$text = [regex]::Replace($text, $pattern, ($loginFunction.TrimEnd() + $newline + "void updateModal()"), 1)
Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
Write-Host "Applied functional standard-login shell and GitHub device login UI."
