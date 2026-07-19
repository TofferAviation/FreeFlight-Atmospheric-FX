Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$path = "app/ImGuiCompanion.cpp"
if (-not (Test-Path $path)) {
    throw "Companion source not found: $path"
}

$text = Get-Content $path -Raw

$oldOverview = 'ImGui::BeginChild("activeFx",ImVec2(midLeft,218),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Active Effects");ImGui::SetWindowFontScale(1);changed|=toggle("Engine contrails",gSettings.enabled);changed|=assetSliderFloat("contrailIntensity",&gSettings.contrailIntensity,0.0f,2.0f);bool vort=gSettings.wingVortexIntensity>.01f;if(toggle("Wingtip vortices",vort)){gSettings.wingVortexIntensity=vort?1.0f:0.0f;changed=true;}changed|=assetSliderFloat("vortexIntensity",&gSettings.wingVortexIntensity,0.0f,2.0f);bool wing=gSettings.wingCondensationIntensity>.01f;if(toggle("Over-wing vapour",wing)){gSettings.wingCondensationIntensity=wing?1.0f:0.0f;changed=true;}changed|=assetSliderFloat("wingIntensity",&gSettings.wingCondensationIntensity,0.0f,2.0f);ImGui::EndChild();'
$newOverview = 'ImGui::BeginChild("activeFx",ImVec2(midLeft,218),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Active Effects");ImGui::SetWindowFontScale(1);bool contrails=gSettings.contrailIntensity>.01f;if(toggle("Engine contrails",contrails)){gSettings.contrailIntensity=contrails?1.0f:0.0f;if(contrails)gSettings.enabled=true;changed=true;}changed|=assetSliderFloat("contrailIntensity",&gSettings.contrailIntensity,0.0f,2.0f);bool vort=gSettings.wingVortexIntensity>.01f;if(toggle("Wingtip vortices",vort)){gSettings.wingVortexIntensity=vort?1.0f:0.0f;if(vort)gSettings.enabled=true;changed=true;}changed|=assetSliderFloat("vortexIntensity",&gSettings.wingVortexIntensity,0.0f,2.0f);bool wing=gSettings.wingCondensationIntensity>.01f;if(toggle("Over-wing vapour",wing)){gSettings.wingCondensationIntensity=wing?1.0f:0.0f;if(wing)gSettings.enabled=true;changed=true;}changed|=assetSliderFloat("wingIntensity",&gSettings.wingCondensationIntensity,0.0f,2.0f);ImGui::EndChild();'

if ($text.Contains($oldOverview)) {
    $text = $text.Replace($oldOverview, $newOverview)
    Write-Host "Separated the overview contrail switch from the master effect switch."
} elseif ($text.Contains($newOverview)) {
    Write-Host "Overview effect isolation is already applied."
} else {
    throw "Could not find the overview Active Effects block."
}

$oldControls = 'ImGui::BeginChild("emitters",ImVec2(0,286),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Emitter intensity");ImGui::SetWindowFontScale(1);ImGui::TextDisabled("Individual output multipliers");ImGui::Spacing();changed|=effectSlider("Engine contrails","fxContrails",&gSettings.contrailIntensity,0,2,"%",100);ImGui::Spacing();changed|=effectSlider("Wingtip vortices","fxVortices",&gSettings.wingVortexIntensity,0,2,"%",100);ImGui::Spacing();changed|=effectSlider("Over-wing condensation","fxWing",&gSettings.wingCondensationIntensity,0,2,"%",100);ImGui::EndChild();'
$newControls = 'ImGui::BeginChild("emitters",ImVec2(0,350),true);ImGui::SetWindowFontScale(1.25f);ImGui::Text("Emitter isolation & intensity");ImGui::SetWindowFontScale(1);ImGui::TextDisabled("Run each visual effect by itself while testing");ImGui::Spacing();float soloWidth=(ImGui::GetContentRegionAvail().x-18)/4;if(ImGui::Button("CONTRAILS ONLY",ImVec2(soloWidth,34))){gSettings.enabled=true;gSettings.contrailIntensity=1.0f;gSettings.wingVortexIntensity=0.0f;gSettings.wingCondensationIntensity=0.0f;changed=true;}ImGui::SameLine(0,6);if(ImGui::Button("VORTICES ONLY",ImVec2(soloWidth,34))){gSettings.enabled=true;gSettings.contrailIntensity=0.0f;gSettings.wingVortexIntensity=1.0f;gSettings.wingCondensationIntensity=0.0f;changed=true;}ImGui::SameLine(0,6);if(ImGui::Button("OVER-WING ONLY",ImVec2(soloWidth,34))){gSettings.enabled=true;gSettings.contrailIntensity=0.0f;gSettings.wingVortexIntensity=0.0f;gSettings.wingCondensationIntensity=1.0f;changed=true;}ImGui::SameLine(0,6);if(ImGui::Button("ALL OFF",ImVec2(soloWidth,34))){gSettings.contrailIntensity=0.0f;gSettings.wingVortexIntensity=0.0f;gSettings.wingCondensationIntensity=0.0f;changed=true;}ImGui::Spacing();bool fxContrailsOn=gSettings.contrailIntensity>.01f;if(toggle("Engine contrails##fx",fxContrailsOn)){gSettings.contrailIntensity=fxContrailsOn?1.0f:0.0f;if(fxContrailsOn)gSettings.enabled=true;changed=true;}changed|=effectSlider("Contrail intensity","fxContrails",&gSettings.contrailIntensity,0,2,"%",100);bool fxVorticesOn=gSettings.wingVortexIntensity>.01f;if(toggle("Wingtip vortices##fx",fxVorticesOn)){gSettings.wingVortexIntensity=fxVorticesOn?1.0f:0.0f;if(fxVorticesOn)gSettings.enabled=true;changed=true;}changed|=effectSlider("Vortex intensity","fxVortices",&gSettings.wingVortexIntensity,0,2,"%",100);bool fxWingOn=gSettings.wingCondensationIntensity>.01f;if(toggle("Over-wing condensation##fx",fxWingOn)){gSettings.wingCondensationIntensity=fxWingOn?1.0f:0.0f;if(fxWingOn)gSettings.enabled=true;changed=true;}changed|=effectSlider("Over-wing intensity","fxWing",&gSettings.wingCondensationIntensity,0,2,"%",100);ImGui::EndChild();'

if ($text.Contains($oldControls)) {
    $text = $text.Replace($oldControls, $newControls)
    Write-Host "Added independent effect switches and solo test buttons."
} elseif ($text.Contains($newControls)) {
    Write-Host "Effects-page isolation controls are already applied."
} else {
    throw "Could not find the Effects Control emitter block."
}

Set-Content -Path $path -Value $text -Encoding utf8 -NoNewline
Write-Host "Applied independent effect testing controls."
