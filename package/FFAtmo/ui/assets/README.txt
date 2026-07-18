FreeFlight Atmospheric FX UI Asset Pack
===========================================

This pack was prepared from the approved Live Overview mockup.

Recommended app usage
---------------------
1. Use backgrounds/background_atmospheric_clean.png as the full-window background.
2. Use branding, icons, aircraft and previews as ordinary image assets.
3. Use the files ending in _9slice.png as stretchable nine-slice assets. Keep their corners fixed while stretching the center and edges.
4. Fixed-size empty panel PNGs are included for pixel-matching the mockup, but for a responsive Dear ImGui build it is safer to draw the panel fill/border in code using the values below.
5. Keep text rendered by the app. Text is intentionally not baked into the reusable panel assets.

Theme values
------------
Window background: #020A16
Panel fill: rgba(4, 18, 34, 0.91)
Panel border: #1E354D
Active blue: #2091FF
Cyan accent: #3EBEFF
Warm gold: #FFB746
Primary text: #E0EBF6
Secondary text: #8CA4BE
Success green: #5BE152
Panel corner radius: 16 px
Input/button corner radius: 12-13 px

Folders
-------
backgrounds  Clean atmospheric background plate
branding     Logo, emblem and avatar
panels       Stretchable and fixed-size empty glass panels
controls     Toggles, sliders, progress bars, checkboxes and status dots
gauges       Reusable performance gauge rings without text
icons        Transparent 64x64 icons in white, active blue and color variants
aircraft     Transparent top-down aircraft silhouette
previews     Clean atmospheric preview images
reference    Original approved dashboard reference

Important
---------
The individual assets are reusable, but the whole app should not be rebuilt as one static screenshot. Panels, text and live values should remain real UI elements so sliders and controls stay responsive without flicker.
