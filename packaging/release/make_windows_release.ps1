# Build the Windows x64 release zip for one published tag.
#
# Input:  a source tarball of the tag (git archive <tag>, no top-level folder), the version, and an output folder.
#         Optional: -DenyFile (the owner's local deny-list) and -WorkDir (default C:\build\canaryminer-<version>).
# Output: <Out>\canaryminer-<version>-windows-x86_64.zip and <Out>\SHA256SUMS.windows (one sha256sum-format line).
# Needs: Visual Studio 2022 Build Tools (Desktop C++), CUDA 13.1 at the default install path, Python 3, tar.exe (Windows 10+).
# The work folder must not be under a user profile: MSVC and nvcc embed source paths in the binary.
param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Out,
    [string] $WorkDir = "C:\build\canaryminer-$Version",
    [string] $DenyFile = ""
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($WorkDir -match '(?i)\\Users\\|\\home\\') { throw "make_windows_release: -WorkDir must not be under a user profile" }
$cuda = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1'
if (-not ((& "$cuda\bin\nvcc.exe" --version) -match 'release 13\.1, V13\.1\.115')) { throw "make_windows_release: $cuda is not CUDA 13.1.115" }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "make_windows_release: vcvars64.bat not found" }

if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
$src = Join-Path $WorkDir 'src'
New-Item -ItemType Directory -Force $src | Out-Null
tar -xf $Source -C $src
if ($LASTEXITCODE -ne 0) { throw "make_windows_release: could not extract $Source" }

$log = Join-Path $WorkDir 'build.log'
$vsInstaller = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$cmd = "set `"PATH=$vsInstaller;%PATH%`" && call `"$vcvars`" >nul &&set `"CUDA_PATH=$cuda`" && cd /d `"$src`" && cmake --preset production-sm120-windows && cmake --build --preset production-sm120-windows"
cmd /c "$cmd > `"$log`" 2>&1"
if ($LASTEXITCODE -ne 0) { Get-Content $log -Tail 30; throw "make_windows_release: build failed (see $log)" }
$exe = Join-Path $src 'build\production-sm120-windows\canary-miner.exe'
if (-not (Test-Path $exe)) { throw "make_windows_release: build did not produce canary-miner.exe" }

$name = "canaryminer-$Version"
$stage = Join-Path $WorkDir "stage\$name"
New-Item -ItemType Directory -Force $stage | Out-Null
function Write-CrLf([string] $text, [string] $path) {
    # Batch files and the plain-text readme use CRLF line endings; UTF-8 without a byte-order mark.
    [IO.File]::WriteAllText($path, ($text -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $false))
}
function Copy-CrLf([string] $from, [string] $to) { Write-CrLf ([IO.File]::ReadAllText($from)) $to }

# The standard layout (H-031 amendment): one binary, ready-to-edit start files, plain-text readme, licences.
Copy-Item $exe $stage
foreach ($f in 'start.bat','start-f2pool.bat','start-kryptex.bat','start-herominers.bat','start-advanced.bat') {
    Copy-CrLf (Join-Path $src "launchers\$f") (Join-Path $stage $f)
}
Copy-Item (Join-Path $src 'config\config.example.json') $stage
Copy-CrLf (Join-Path $src 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-CrLf (Join-Path $src 'THIRD_PARTY.md') (Join-Path $stage 'THIRD_PARTY.txt')
Copy-CrLf (Join-Path $src 'HELP.txt') (Join-Path $stage 'HELP.txt')
Copy-Item -Recurse (Join-Path $src 'LICENSES') (Join-Path $stage 'LICENSES')
Write-CrLf ((Get-Content (Join-Path $here 'README-windows.txt') -Raw).Replace('@VERSION@', $Version)) (Join-Path $stage 'README.txt')

# The archive holds exactly the standard layout: a file added or dropped by mistake fails the release here.
$want = @('HELP.txt','LICENSE.txt','LICENSES','README.txt','THIRD_PARTY.txt','canary-miner.exe','config.example.json','start-advanced.bat',
          'start-f2pool.bat','start-herominers.bat','start-kryptex.bat','start.bat')
$got = @(Get-ChildItem -Name -Force $stage)
if (Compare-Object ($want | Sort-Object) ($got | Sort-Object)) {
    throw "make_windows_release: the archive does not match the standard layout (got: $($got -join ', '))"
}

# Refuse to ship a binary that names a build machine (same rules as make_linux_release.sh).
$scan = @'
import re, sys, os
data = open(sys.argv[1], 'rb').read()
text = '\n'.join(m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{6,}', data))
leaks = sorted(set(re.findall(r'(?i)[a-z]:[\\/]+users[\\/]+[^\\/\s"]+|/home/[^/\s]+|/mnt/[a-z]/', text)))
if leaks:
    print('make_windows_release: the binary embeds build-machine paths: ' + ', '.join(leaks), file=sys.stderr); sys.exit(1)
if len(sys.argv) > 3 and sys.argv[3]:
    sys.path.insert(0, os.path.join(sys.argv[2], 'tools'))
    os.environ['CANARY_SECRETS_FILE'] = sys.argv[3]
    import check_secrets as c
    _, local = c.local_patterns('.')
    fee = re.search(r'#define CANARY_FEE_DESTINATION "(prl1[a-z0-9]+)"', open(os.path.join(sys.argv[2], 'src', 'product', 'fee.h')).read()).group(1)
    text = re.sub(re.escape(fee[:30]) + r'[a-z0-9]*', '<donation-address>', text)
    findings = []
    c.scan_text('canary-miner.exe', text.encode(), local, findings)
    bad = sorted({f[2] for f in findings})
    if bad:
        print('make_windows_release: the binary matches the deny-list: ' + ', '.join(bad), file=sys.stderr); sys.exit(1)
'@
$scanPy = Join-Path $WorkDir 'scan.py'
Set-Content -Encoding utf8 $scanPy $scan
python $scanPy (Join-Path $stage 'canary-miner.exe') $src $DenyFile
if ($LASTEXITCODE -ne 0) { throw "make_windows_release: binary scan failed; not packaging" }

New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "$name-windows-x86_64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $WorkDir "stage\$name") -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path -Leaf $zip)" | Set-Content -Encoding ascii (Join-Path $Out 'SHA256SUMS.windows')
Remove-Item -Recurse -Force $WorkDir
"archive: $zip"
"sha256: $hash"
