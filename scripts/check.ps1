# The local check: the steps of the Lint and CI workflows on this machine, the one command before a build is handed
# over and the proof on a private repository, where no workflow runs. Windows PowerShell 5.1 or PowerShell 7.
#
#   powershell -NoProfile -File scripts\check.ps1 [-SkipBuild] [-SkipTests] [-KeepFl] [-BuildDir build] [-Config Release]
#
# In order: closes FL Studio without saving when it runs (it holds the VST3 files; -KeepFl leaves it open), runs
# scripts/check_repo.py, runs markdownlint-cli2, yamllint and actionlint when they are on the PATH (a missing tool is
# reported and skipped, never installed), configures when <build>/CMakeCache.txt is missing (JUCE and
# clap-juce-extensions from the local.yaml keys juce_src and clap_src when present), builds ci_targets into build.log,
# counts the warnings by the workflows' rule and fails above zero, asserts that every VST3 in packaging/plugins.txt was
# relinked by this build or is newer than its sources, runs ctest and, when goldens exist, the render regression.
# Prints one CHECK line with the counts and exits 0 only when every step passed.
param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$KeepFl,
    [string]$BuildDir = "build",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Continue"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$build = Join-Path $root $BuildDir
$failed = @()
$state = [ordered]@{}

function Pass($msg) { Write-Host "PASS  $msg" -ForegroundColor Green }
function Fail($step, $msg) { Write-Host "FAIL  $msg" -ForegroundColor Red; $script:failed += $step; $script:state[$step] = "fail" }
function Skip($step, $msg) { Write-Host "SKIP  $msg"; if (-not $script:state.Contains($step)) { $script:state[$step] = "skipped" } }

# local.yaml: flat "key: value" lines (local.example.yaml lists the keys); an empty value counts as missing.
$cfg = @{}
$localYaml = Join-Path $root "local.yaml"
if (Test-Path $localYaml) {
    foreach ($line in Get-Content $localYaml) {
        if ($line -match '^\s*([A-Za-z0-9_.]+)\s*:\s*(.*?)\s*$') {
            $k = $Matches[1]; $v = $Matches[2]
            if ($v.StartsWith("'")) { $v = $v.Substring(1, $v.LastIndexOf("'") - 1).Replace("''", "'") }
            elseif ($v.StartsWith('"')) { $v = $v.Substring(1, $v.LastIndexOf('"') - 1) }
            else { $v = ($v -replace '\s+#.*$', '') }
            if ($v) { $cfg[$k] = $v }
        }
    }
} else {
    Write-Host "note: no local.yaml (copy local.example.yaml): the defaults are used"
}
$py = if ($cfg.ContainsKey("python")) { $cfg["python"] } else { "python" }

# 1. FL Studio holds the VST3 through the plug-in folder: close it without saving. Never Stop-Process (a killed FL
#    loses its settings) and never Alt+F4 through SendKeys (it reaches whatever window is in front).
$fl = Get-Process FL64 -ErrorAction SilentlyContinue
if ($fl -and -not $KeepFl) {
    Write-Host "FL Studio is running (pid $($fl.Id)): closing it without saving"
    $fl.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 2
    if (Get-Process FL64 -ErrorAction SilentlyContinue) {
        # the save prompt: N is its No button
        $wshell = New-Object -ComObject WScript.Shell
        if ($wshell.AppActivate($fl.Id)) { Start-Sleep -Milliseconds 500; $wshell.SendKeys("n") }
        Start-Sleep -Seconds 3
    }
    if (Get-Process FL64 -ErrorAction SilentlyContinue) {
        Write-Host "FAIL  FL Studio is still running: close it by hand without saving" -ForegroundColor Red
        exit 1
    }
    Pass "FL Studio closed"
} elseif ($fl) {
    Write-Host "note: FL Studio is running and -KeepFl was given: the build may fail to write the VST3"
}

# 2. The repository checks
$out = & $py scripts/check_repo.py 2>&1
$repoFindings = 0
if ($LASTEXITCODE -eq 0) { $state["repo"] = "ok"; Pass "check_repo.py" }
else {
    $out | ForEach-Object { Write-Host "$_" }
    $repoFindings = @($out | Where-Object { "$_" -match '^\S.*:\d+: [A-Z]+: ' -or "$_" -match ': release: ' }).Count
    Fail "repo" "check_repo.py: $repoFindings finding(s)"
}

# 3. The lint tools the Lint workflow runs, when installed
$mdFiles = @(git ls-files "*.md")
$yamlFiles = @(git ls-files "*.yml" "*.yaml")
$lint = @(
    @{ Name = "markdownlint-cli2"; Args = $mdFiles },
    @{ Name = "yamllint"; Args = @("--strict", "-c", ".yamllint.yaml") + $yamlFiles },
    @{ Name = "actionlint"; Args = @() }
)
foreach ($t in $lint) {
    if (-not (Get-Command $t.Name -ErrorAction SilentlyContinue)) { Skip "lint" "$($t.Name) not found: skipped"; continue }
    $toolArgs = $t.Args
    & $t.Name @toolArgs 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) { Fail "lint" "$($t.Name)" }
    else { Pass $t.Name; if ($state["lint"] -ne "fail") { $state["lint"] = "ok" } }
}

# 4. Configure when there is no build folder yet
$prefix = $null
$cm = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cm -match 'option\((\w+)_TOOLS\b') { $prefix = $Matches[1] } elseif ($cm -match 'option\((\w+)_CLAP\b') { $prefix = $Matches[1] }
$warnings = 0
$buildStart = Get-Date
if ($SkipBuild) {
    Skip "build" "build: skipped (-SkipBuild)"
} else {
    if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
        $conf = @("-B", $build, "-A", "x64")
        if ($prefix -and $cm -match "option\($prefix`_TOOLS\b") { $conf += "-D$($prefix)_TOOLS=ON" }
        if ($cfg.ContainsKey("juce_src")) { $conf += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$($cfg['juce_src'])" }
        if ($cfg.ContainsKey("clap_src")) { $conf += "-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=$($cfg['clap_src'])" }
        Write-Host "configure: cmake $($conf -join ' ')"
        & cmake @conf 2>&1 | ForEach-Object { Write-Host "$_" }
        if ($LASTEXITCODE -ne 0) { Fail "build" "configure"; $SkipBuild = $true }
    }
}

# 5. Build ci_targets into build.log and count the warnings by the workflows' rule
if (-not $SkipBuild) {
    $log = Join-Path $root "build.log"
    $buildStart = Get-Date
    & cmake --build $build --config $Config --target ci_targets --parallel 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
    $code = $LASTEXITCODE
    if ($code -ne 0) { Fail "build" "build (exit code $code, log: build.log)" } else { $state["build"] = "ok"; Pass "build" }
    $hits = @(Select-String -Path $log -CaseSensitive -Pattern 'warning C\d+|warning ?:|warning MSB|\d+ warnings? generated' |
        Where-Object { $_.Line -notmatch '::warning|hint:' })
    $warnings = $hits.Count
    if ($warnings -gt 0) {
        $hits | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow }
        Fail "warnings" "$warnings warning line(s) in build.log (zero allowed)"
    } else { $state["warnings"] = "0"; Pass "0 warnings" }

    # 6. Every VST3 is the one this build made: relinked now, or newer than every source it is built from
    $list = Join-Path $root "packaging/plugins.txt"
    if (Test-Path $list) {
        $stale = 0
        foreach ($line in Get-Content $list) {
            if (-not $line.Trim() -or $line.StartsWith('#')) { continue }
            $t, $p = $line.Split([char]0x7C)[0..1]
            $bundle = Join-Path $build "$($t)_artefacts/$Config/VST3/$p.vst3"
            if (-not (Test-Path $bundle)) {   # a suite that adds its plug-ins with add_subdirectory builds them one level down
                $dir = Get-ChildItem $build -Directory -Recurse -Filter "$($t)_artefacts" -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($dir) { $bundle = Join-Path $dir.FullName "$Config/VST3/$p.vst3" }
            }
            if (-not (Test-Path $bundle)) { Write-Host "no VST3 at $bundle"; $stale++; continue }
            $built = (Get-ChildItem $bundle -Recurse -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
            if ($built -gt $buildStart) { continue }
            # the plug-in folder: plugin in a suite of one plug-in, plugins/<Id> in a suite of several
            $pdir = if (Test-Path "plugin/plugin.cmake") { "plugin" } else { "plugins/$t" }
            $src = @($pdir, "shared", "CMakeLists.txt") | Where-Object { Test-Path $_ } |
                ForEach-Object { Get-ChildItem $_ -Recurse -File -Include *.h, *.cpp, *.cmake, CMakeLists.txt -ErrorAction SilentlyContinue }
            $newest = ($src | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
            if ($newest -and $newest -gt $built) { Write-Host "stale: $p.vst3 ($built) is older than its sources ($newest)"; $stale++ }
        }
        if ($stale -gt 0) { Fail "vst3" "$stale VST3 not rebuilt (a host holds the file, or the build failed)" }
        else { $state["vst3"] = "ok"; Pass "every VST3 is current" }
    } else { Skip "vst3" "no packaging/plugins.txt: the VST3 timestamp check is skipped" }
}

# 7. Unit tests and the render regression
if ($SkipTests) {
    Skip "ctest" "ctest: skipped (-SkipTests)"
    Skip "regress" "render regression: skipped (-SkipTests)"
} else {
    & ctest --test-dir $build -C $Config --output-on-failure 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) { Fail "ctest" "ctest" } else { $state["ctest"] = "ok"; Pass "ctest" }
    if ((Get-ChildItem "tests/regress/golden/*.f32", "tests/regress/*/golden/*.f32" -ErrorAction SilentlyContinue)) {
        & $py scripts/render_regress.py --build $build 2>&1 | ForEach-Object { Write-Host "$_" }
        if ($LASTEXITCODE -ne 0) { Fail "regress" "render regression" } else { $state["regress"] = "ok"; Pass "render regression" }
    } else { Skip "regress" "no goldens in tests/regress yet: the render regression is skipped" }
}

$sha = (git rev-parse --short HEAD 2>$null)
$parts = foreach ($k in @("repo", "lint", "build", "warnings", "vst3", "ctest", "regress")) {
    $v = if ($state.Contains($k)) { $state[$k] } else { "skipped" }
    if ($k -eq "repo" -and $repoFindings) { $v = "$repoFindings findings" }
    if ($k -eq "warnings" -and $warnings) { $v = "$warnings" }
    "$k $v"
}
Write-Host ("CHECK {0} | {1} | {2} | {3} s" -f (Get-Date -Format "yyyy-MM-dd HH:mm"), $sha, ($parts -join " | "), [int]$sw.Elapsed.TotalSeconds)
if ($failed.Count) { exit 1 }
exit 0
