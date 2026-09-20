<#
.SYNOPSIS
  GCF — Windows da bir buyruq bilan to'liq yig'ish (backend + GUI + test).
.DESCRIPTION
  1) CMake + MSVC ni topadi (vswhere, WinGet, standart yo'llar)
  2) CUDA Toolkit bo'lsa GPU rejim, bo'lmasa CPU-fallback yig'adi
  3) gcf.exe ni gcf_gui/native ga nusxalaydi
  4) .NET ni topib GUI ni single-file qilib publish qiladi
  5) Roundtrip test (compress/decompress MATCH) o'tkazadi
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File build-windows.ps1
  powershell -ExecutionPolicy Bypass -File build-windows.ps1 -Config Release -SkipGui
#>
param(
  [string]$Config = "Release",
  [switch]$SkipGui,
  [switch]$SkipTest,
  [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

function Find-Cmake {
  $c = Get-Command cmake -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  $paths = @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\CMake\bin\cmake.exe",
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_*\cmake.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  )
  foreach ($p in $paths) {
    $g = Get-Item $p -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($g) { return $g.FullName }
  }
  # WinGet paketlari ichidan qidirish
  $wg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "*CMake*" } | Select-Object -First 1
  if ($wg) {
    $exe = Get-ChildItem $wg.FullName -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) { return $exe.FullName }
  }
  # WinLibs (oldingi build da ishlatilgan)
  $wl = Get-ChildItem "C:\Users\*\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($wl) {
    $exe = Join-Path $wl.FullName "mingw64\bin\cmake.exe"
    if (Test-Path $exe) { return $exe }
  }
  return $null
}

function Find-Dotnet {
  $c = Get-Command dotnet -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  $p = "C:\Program Files\dotnet\dotnet.exe"
  if (Test-Path $p) { return $p }
  return $null
}

function Find-Nvcc {
  $c = Get-Command nvcc -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  $cands = @(
    "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v*\bin\nvcc.exe",
    "D:\System Apps\bin\nvcc.exe"
  ) | ForEach-Object { Get-Item $_ -ErrorAction SilentlyContinue } | Sort-Object FullName -Descending | Select-Object -First 1
  if ($cands) { return $cands.FullName }
  return $null
}

# --- 1. Asboblar ---
$Cmake = Find-Cmake
if (-not $Cmake) {
  Write-Host "❌ cmake topilmadi. O'rnating:" -ForegroundColor Red
  Write-Host "   winget install Kitware.CMake   # yoki https://cmake.org/download/"
  exit 1
}
Write-Host "✅ cmake: $Cmake" -ForegroundColor Green

$Nvcc = Find-Nvcc
if ($Nvcc) { Write-Host "✅ nvcc: $Nvcc (GPU rejim yoqiladi)" -ForegroundColor Green }
else { Write-Host "⚠️ nvcc topilmadi — CPU-fallback yig'iladi (baribir ishlaydi)" -ForegroundColor Yellow }

# VS generatorni cmake ning o'zi topsin (vswhere orqali). Bo'lmasa Ninja/MinGW ga tushadi.
Write-Host "--- [1/4] Backend (gcf.exe) ---"
if (Test-Path $BuildDir) {
  # Kesh eski mashinadan qolgan bo'lishi mumkin (D:/EpicProject...), tozalaymiz
  $cache = Join-Path $BuildDir "CMakeCache.txt"
  if (Test-Path $cache -PathType Leaf) {
    $txt = Get-Content $cache -Raw -ErrorAction SilentlyContinue
    if ($txt -and $txt -notlike "*$Root*") {
      Write-Host "⚠️ Eski build keshi boshqa yo'ldan — tozalanmoqda" -ForegroundColor Yellow
      Remove-Item -Recurse -Force $BuildDir
    }
  }
}

& $Cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { exit 1 }
& $Cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit 1 }

# Chiqqan exe ni topish (VS multi-config: Release/ pastida, Ninja: ildizda)
$Exe = Join-Path $BuildDir "$Config\gcf.exe"
if (-not (Test-Path $Exe)) { $Exe = Join-Path $BuildDir "gcf.exe" }
if (-not (Test-Path $Exe)) {
  $Exe = Get-ChildItem $BuildDir -Recurse -Filter gcf.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Exe -or -not (Test-Path $Exe)) { Write-Host "❌ gcf.exe topilmadi" -ForegroundColor Red; exit 1 }
Write-Host "✅ backend: $Exe ($([math]::Round((Get-Item $Exe).Length/1KB)) KB)" -ForegroundColor Green
& $Exe gpus

# --- 2. GUI uchun nusxalash ---
Write-Host "--- [2/4] GUI native nusxa ---"
$NativeDir = Join-Path $Root "gcf_gui\native"
New-Item -ItemType Directory -Force -Path $NativeDir | Out-Null
Copy-Item $Exe (Join-Path $NativeDir "gcf.exe") -Force
Write-Host "✅ gcf_gui\native\gcf.exe yangilandi" -ForegroundColor Green

if (-not $SkipGui) {
  Write-Host "--- [3/4] GUI (GCF.exe) ---"
  $Dotnet = Find-Dotnet
  if (-not $Dotnet) {
    Write-Host "⚠️ dotnet topilmadi — GUI o'tkazib yuborildi." -ForegroundColor Yellow
    Write-Host "   winget install Microsoft.DotNet.SDK.8"
  } else {
    Write-Host "✅ dotnet: $Dotnet" -ForegroundColor Green
    # TFM nomuvofiqlik himoyasi: csproj net8, lekin bin/obj da eski net10 qolishi mumkin
    $csprojTfm = (Select-String -Path "gcf_gui\gcf_gui.csproj" -Pattern "<TargetFramework>(.*)</TargetFramework>" | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($csprojTfm -and (Test-Path "gcf_gui\bin\Release\net10.0-windows") -and $csprojTfm -like "net8*") {
      Write-Host "⚠️ bin/obj da eski net10 artefakt bor — tozalanmoqda (csproj $csprojTfm)" -ForegroundColor Yellow
      Remove-Item -Recurse -Force "gcf_gui\bin","gcf_gui\obj" -ErrorAction SilentlyContinue
    }
    & $Dotnet publish gcf_gui\gcf_gui.csproj -c $Config -o publish
    if ($LASTEXITCODE -ne 0) { exit 1 }
    if (-not (Test-Path "publish\GCF.exe")) { Write-Host "❌ publish\GCF.exe topilmadi (publish xato)" -ForegroundColor Red; exit 1 }
    Write-Host "✅ publish\GCF.exe tayyor ($([math]::Round((Get-Item publish\GCF.exe).Length/1MB)) MB)" -ForegroundColor Green
  }
} else { Write-Host "--- [3/4] GUI o'tkazib yuborildi ---" }

# --- 3. Test ---
if (-not $SkipTest) {
  Write-Host "--- [4/4] Roundtrip test ---"
  $Tmp = Join-Path ([IO.Path]::GetTempPath()) "gcf_win_test"
  New-Item -ItemType Directory -Force -Path $Tmp | Out-Null
  $In = Join-Path $Tmp "hello.txt"
  ("Hello GPU Compressor test! " * 5000) | Set-Content $In -NoNewline -Encoding ascii
  $Arch = Join-Path $Tmp "hello.gcf"
  $Out = Join-Path $Tmp "restored.txt"
  & $Exe compress $In $Arch
  if ($LASTEXITCODE -ne 0) { Write-Host "❌ compress xato" -ForegroundColor Red; exit 1 }
  & $Exe info $Arch
  & $Exe decompress $Arch $Out
  if ($LASTEXITCODE -ne 0) { Write-Host "❌ decompress xato" -ForegroundColor Red; exit 1 }
  $a = (Get-FileHash $In).Hash; $b = (Get-FileHash $Out).Hash
  if ($a -eq $b) { Write-Host "✅ roundtrip MATCH" -ForegroundColor Green }
  else { Write-Host "❌ roundtrip MISMATCH" -ForegroundColor Red; exit 1 }
  # Bo'sh fayl regressiya (backend [] bo'sh bo'lmasligi kerak)
  $Empty = Join-Path $Tmp "empty.bin"
  [IO.File]::WriteAllBytes($Empty, @())
  $EmptyArch = Join-Path $Tmp "empty.gcf"
  $EmptyOut = Join-Path $Tmp "empty.out"
  & $Exe compress $Empty $EmptyArch
  & $Exe decompress $EmptyArch $EmptyOut
  if ((Get-Item $EmptyOut).Length -eq 0) { Write-Host "✅ empty roundtrip OK" -ForegroundColor Green }
  else { Write-Host "❌ empty test xato" -ForegroundColor Red; exit 1 }
}

Write-Host ""
Write-Host "🎉 Tayyor! Ishga tushirish: .\publish\GCF.exe  (yoki .\build\Release\gcf.exe gpus)" -ForegroundColor Green
