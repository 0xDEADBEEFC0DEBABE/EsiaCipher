# The README's pictures (docs/*.jpg), from the demo in build\bin: fixed time steps, scripted input, the bundled robot
# as the pet. A run the real cursor moved into (the demo says so) is retried.
#
#   powershell tools/docs_pictures.ps1                  every picture
#   powershell tools/docs_pictures.ps1 -Only hero,toasts
param([string[]]$Only = @())
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\bin"
$model = Join-Path $root "assets\RobotExpressive\RobotExpressive.glb"
$out = Join-Path $root "docs"
$raw = Join-Path $env:TEMP "cipher-docs"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $out, $raw | Out-Null
$jpeg = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$quality = New-Object System.Drawing.Imaging.EncoderParameters 1
$quality.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 90L

# a run of the demo (1440 x 900 at scale 1.5: 2160 x 1350 pixels) up to frame $frames; its PNG
function Capture([string]$name, [string[]]$extra, [int]$frames, [switch]$WithIntro)
{
    $introArgs = @()
    if (-not $WithIntro) { $introArgs = @("--no-boot") }
    $png = Join-Path $raw "$name.png"
    for ($try = 1; $try -le 4; $try++)
    {
        $err = & (Join-Path $bin "cipher.exe") --api d3d12 --size 1440x900 --fixed-dt 0.0166667 @introArgs --model $model --frames $frames @extra --screenshot $png 2>&1 | Out-String
        if (-not (Test-Path $png)) { throw "$name failed: $err" }
        if ($err -notmatch "real cursor") { break }
        "  $name : the real cursor moved the pointer (try $try)"
    }
    return $png
}

# $crop: pixels of the capture; $scale: of the crop (2/3: UI units at 1x)
function Shot([string]$name, [string[]]$extra, [int[]]$crop, [int]$frames, [float]$scale = 0.6667, [switch]$WithIntro)
{
    if ($Only.Count -and $Only -notcontains $name) { return }
    $png = Capture $name $extra $frames -WithIntro:$WithIntro
    $im = [System.Drawing.Image]::FromFile($png)
    $w = [int]($crop[2] * $scale); $h = [int]($crop[3] * $scale)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($im, (New-Object System.Drawing.Rectangle 0, 0, $w, $h), (New-Object System.Drawing.Rectangle $crop[0], $crop[1], $crop[2], $crop[3]), [System.Drawing.GraphicsUnit]::Pixel)
    $bmp.Save((Join-Path $out "$name.jpg"), $jpeg, $quality)
    $g.Dispose(); $bmp.Dispose(); $im.Dispose()
    "$name ok"
}

$full = @(0, 0, 2160, 1350)
$hub = @(540, 0, 1500, 1220)   # the hub and the pet on it
# script verbs: frame:move|click|down|up:x,y in UI units, wheel=<notches> (- down); frames past 150: the watermark's
# numbers have rolled in
Shot "hero" @("--tab", "Data", "--script", "30:click:794,256;60:move:1400,600") $full 200
Shot "intro" @("--script", "200:move:300,700;206:move:420,660;212:move:560,630;218:move:700,610;224:move:840,600;230:move:980,610;236:move:1120,640;242:move:1250,690") $full 330 -WithIntro
Shot "sliders" @("--tab", "Sliders", "--script", "100:move:1102,366;104:down:1102,366;110:move:1125,366;116:move:1150,366") $hub 140
Shot "menus" @("--tab", "Menus", "--script", "100:click:1214,366;104:move:1214,430") $hub 150
Shot "table" @("--tab", "Data", "--script", "30:click:993,256;60:move:1400,600") $hub 160
Shot "plots" @("--tab", "Data", "--script", "30:click:1091,256;60:move:1400,600") $hub 220
Shot "toasts" @("--tab", "Notifications", "--script", "100:click:676,350;104:click:784,350;108:click:904,350;112:click:1020,350;116:move:1400,600") @(1440, 0, 720, 600) 170
Shot "themes" @("--tab", "Themes", "--script", "100:click:980,394;104:move:1400,600") $hub 200
Shot "turntable" @("--tab", "Pet", "--script", "60:move:1400,600") $hub 160

# the pet bursting (Scatter at frame 100) and coming back (Restore at 200): four moments, 2 x 2 at 1:1
if (-not $Only.Count -or $Only -contains "datamosh")
{
    $mosh = "40:move:1000,600;42:wheel=-10;100:click:1270,606;104:move:1400,700;200:click:1270,606;204:move:1400,700"
    $sheet = New-Object System.Drawing.Bitmap 844, 404
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 20, 24, 32))
    $i = 0
    foreach ($f in 116, 130, 248, 262)
    {
        $im = [System.Drawing.Image]::FromFile((Capture "datamosh$f" @("--tab", "Pet", "--script", $mosh) $f))
        $g.DrawImage($im, (New-Object System.Drawing.Rectangle (($i % 2) * 424), ([Math]::Floor($i / 2) * 204), 420, 200), (New-Object System.Drawing.Rectangle 780, 0, 420, 200), [System.Drawing.GraphicsUnit]::Pixel)
        $im.Dispose()
        $i++
    }
    $sheet.Save((Join-Path $out "datamosh.jpg"), $jpeg, $quality)
    $g.Dispose(); $sheet.Dispose()
    "datamosh ok"
}
