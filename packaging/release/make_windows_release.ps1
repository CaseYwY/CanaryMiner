# Build the Windows x64 release zip for one published tag.
#
# Input:  a source tarball of the tag (git archive <tag>, no top-level folder), the version, and an output folder.
#         Required: -DenyFile (the owner's local deny-list). Optional: -WorkDir (default C:\build\canaryminer-<version>).
# Output: <Out>\canaryminer-<version>-windows-x86_64.zip and <Out>\SHA256SUMS.windows (one sha256sum-format line).
# Needs: Visual Studio 2022 Build Tools (Desktop C++), CUDA 13.1 at the default install path, Python 3, tar.exe (Windows 10+).
# The work folder must not be under a user profile: MSVC and nvcc embed source paths in the binary.
param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Out,
    [string] $WorkDir = "C:\build\canaryminer-$Version",
    [Parameter(Mandatory)] [string] $DenyFile
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'make_windows_release: version must be X.Y.Z' }
if ($WorkDir -match '(?i)\\Users\\|\\home\\') { throw "make_windows_release: -WorkDir must not be under a user profile" }
if (-not (Test-Path -LiteralPath $DenyFile -PathType Leaf)) { throw "make_windows_release: local deny-list required" }
$cuda = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1'
if (-not ((& "$cuda\bin\nvcc.exe" --version) -match 'release 13\.1, V13\.1\.115')) { throw "make_windows_release: $cuda is not CUDA 13.1.115" }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "make_windows_release: vcvars64.bat not found" }

if (Test-Path -LiteralPath $WorkDir) { throw 'make_windows_release: work directory already exists; preserve it and choose a new directory' }
$src = Join-Path $WorkDir 'src'
New-Item -ItemType Directory -Force $src | Out-Null
tar -xf $Source -C $src
if ($LASTEXITCODE -ne 0) { throw "make_windows_release: could not extract $Source" }

$log = Join-Path $WorkDir 'build.log'
$vsInstaller = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$cmd = "set `"PATH=$vsInstaller;%PATH%`" && call `"$vcvars`" >nul &&set `"CUDA_PATH=$cuda`" && cd /d `"$src`" && cmake --preset production-sm120-windows -DCANARY_ENABLE_ENERGY_RUNTIME=OFF -DCANARY_ENABLE_WORKER_CONTROL_RUNTIME=OFF -DCANARY_ENABLE_POOL_TLS_RUNTIME=OFF && cmake --build --preset production-sm120-windows"
cmd /c "$cmd > `"$log`" 2>&1"
if ($LASTEXITCODE -ne 0) { Get-Content $log -Tail 30; throw "make_windows_release: build failed (see $log)" }
$exe = Join-Path $src 'build\production-sm120-windows\canary-miner.exe'
if (-not (Test-Path $exe)) { throw "make_windows_release: build did not produce canary-miner.exe" }
# Ordinary preferences require their fixed CPU sibling; unrelated development runtimes remain unqualified.
$cache = Get-Content (Join-Path $src 'build\production-sm120-windows\CMakeCache.txt')
if (-not ($cache -contains 'CANARY_ENABLE_ENERGY_RUNTIME:BOOL=OFF')) { throw "make_windows_release: development energy runtime is not release-qualified" }
if (-not ($cache -contains 'CANARY_ENABLE_WORKER_CONTROL_RUNTIME:BOOL=OFF')) { throw "make_windows_release: development worker control is not release-qualified" }

$name = "canaryminer-$Version"
$stage = Join-Path $WorkDir "stage\$name"
$helper = Join-Path (Split-Path -Parent $exe) 'canary_preferences_worker.exe'
if (-not (Test-Path -LiteralPath $helper -PathType Leaf) -or ((Get-Item -LiteralPath $helper).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "make_windows_release: mandatory preference helper unavailable"
}
New-Item -ItemType Directory -Force $stage | Out-Null
function Write-CrLf([string] $text, [string] $path) {
    # Batch files and the plain-text readme use CRLF line endings; UTF-8 without a byte-order mark.
    [IO.File]::WriteAllText($path, ($text -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $false))
}
function Copy-CrLf([string] $from, [string] $to) { Write-CrLf ([IO.File]::ReadAllText($from)) $to }

# The standard layout: miner + mandatory CPU preference helper, editable start files, plain-text readme, licences.
Copy-Item $exe $stage
Copy-Item -LiteralPath $helper -Destination $stage
foreach ($f in 'start.bat','start-f2pool.bat','start-kryptex.bat','start-herominers.bat','start-advanced.bat') {
    Copy-CrLf (Join-Path $src "launchers\$f") (Join-Path $stage $f)
}
Copy-Item (Join-Path $src 'config\config.example.json') $stage
Copy-CrLf (Join-Path $src 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-CrLf (Join-Path $src 'THIRD_PARTY.md') (Join-Path $stage 'THIRD_PARTY.txt')
Copy-CrLf (Join-Path $src 'HELP.txt') (Join-Path $stage 'HELP.txt')
Copy-Item -Recurse (Join-Path $src 'LICENSES') (Join-Path $stage 'LICENSES')
Write-CrLf ((Get-Content (Join-Path $src 'packaging\release\README-windows.txt') -Raw).Replace('@VERSION@', $Version)) (Join-Path $stage 'README.txt')

# The archive holds exactly the standard layout: a file added or dropped by mistake fails the release here.
$want = @('HELP.txt','LICENSE.txt','LICENSES','README.txt','THIRD_PARTY.txt','canary-miner.exe','canary_preferences_worker.exe','config.example.json','start-advanced.bat',
          'start-f2pool.bat','start-herominers.bat','start-kryptex.bat','start.bat')
$got = @(Get-ChildItem -Name -Force $stage)
if (Compare-Object ($want | Sort-Object) ($got | Sort-Object)) {
    throw "make_windows_release: the archive does not match the standard layout (got: $($got -join ', '))"
}

# Use the inspector next to the actual configured MSVC linker, not an arbitrary executable in the staged directory.
$linker = @($cache | Where-Object { $_ -like 'CMAKE_LINKER:FILEPATH=*' })
if ($linker.Count -ne 1) { throw "make_windows_release: configured native linker unavailable" }
$dumpbin = Join-Path (Split-Path -Parent ($linker[0] -replace '^CMAKE_LINKER:FILEPATH=', '')) 'dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw "make_windows_release: native metadata inspector unavailable" }
python -B (Join-Path $src 'packaging\release\verify_artifacts.py') --platform windows --stage $stage --source $src `
    --cache (Join-Path $src 'build\production-sm120-windows\CMakeCache.txt') --deny-file $DenyFile --tool $dumpbin
if ($LASTEXITCODE -ne 0) { throw "make_windows_release: artifact checks failed; not packaging" }

New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "$name-windows-x86_64.zip"
if ((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath (Join-Path $Out 'SHA256SUMS.windows'))) { throw 'make_windows_release: output already exists; preserve it' }
Compress-Archive -Path (Join-Path $WorkDir "stage\$name") -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path -Leaf $zip)" | Set-Content -Encoding ascii (Join-Path $Out 'SHA256SUMS.windows')
# Keep the explicitly new work directory for private inspection, never recursively delete caller-selected paths.
"archive: $zip"
"sha256: $hash"
