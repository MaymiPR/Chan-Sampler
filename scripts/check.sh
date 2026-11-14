#!/usr/bin/env bash
# The local check on Linux and macOS: the steps of the Lint and CI workflows on this machine and the hand-over checks,
# the one command before a build is handed over and the proof on a private repository, where no workflow runs.
# scripts/check.ps1 is the Windows form with the same steps and options.
#
#   bash scripts/check.sh [--skip-build] [--skip-tests] [--pluginval] [--pluginval-path exe] [--focus-check]
#       [--null-check] [--null-only text] [--targets A,B] [--jobs 4] [--clap] [--allow-incomplete] [--allow-fetch]
#       [--allow-stale] [--build-dir build] [--config Release]
#
# Steps, each with one result in the CHECK line (ok, skipped, incomplete or fail; a step that did not run is never ok):
#   fl         skipped here: closing FL Studio is the Windows form's step.
#   repo       scripts/check_repo.py.
#   lint       markdownlint-cli2, yamllint, actionlint, and shellcheck on packaging/*.sh and scripts/*.sh, as the Lint
#              workflow runs them. A tool that is not installed is never installed: the step is incomplete.
#   build      configure (on every run, so the pins are checked on every run), then ci_targets and the targets the
#              options need, at a bounded parallel level (--jobs, default 4). A first configure passes the options CI
#              passes (the tools on, CLAP off; --clap turns CLAP on), uses Ninja (required on macOS, used on Linux
#              when installed) and takes JUCE and clap-juce-extensions from the local.yaml keys juce_src and clap_src,
#              or from <build>/_deps when the copies are there; it refuses to download them unless --allow-fetch is
#              given. A build folder that is configured already keeps its options.
#   warnings   the configure and build output (build.log) by the rule the CI and release workflows use, the same
#              pattern here and there: any compiler, linker or CMake warning fails.
#   vst3       every VST3 of packaging/plugins.txt is newer than every source it is built from: the plug-in binary in
#              the bundle against the files of the plug-in's Source folder, of shared/ and of cmake/, CMakeLists.txt
#              and the plug-in's plugin.cmake. A source changed without a rebuild fails; a built suite in which
#              nothing changed passes, with --skip-build too. A CMake file may be newer than the binary when it is
#              older than the last build this script finished: a CMake change that alters no input of the plug-in
#              rewrites no binary. --allow-stale skips the comparison.
#   ctest      the unit and host tests.
#   regress    scripts/render_regress.py, when goldens exist.
#   pluginval  --pluginval: pluginval at strictness 10 on every VST3 (the executable from --pluginval-path, the
#              pluginval key of local.yaml or the PATH; never downloaded).
#   focus      --focus-check: skipped here, FocusCheck is a Windows tool.
#   null       --null-check: nulltest.py --vst3 of every plug-in, at the harness's rate and at 96 kHz (--null-only text adds --only text): the built VST3
#              against the offline renderer on every case, no reference needed. Its exit code 0 is ok, 1 (a case
#              above the threshold) fail, 2 (the VST3, the renderer or RefRender not built) incomplete.
# A step that cannot run for want of a tool is incomplete; the exit code is 0 only when no step failed and none is
# incomplete (--allow-incomplete accepts incomplete steps).
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
BUILD=build
CONFIG=Release
SKIP_BUILD=0
SKIP_TESTS=0
PLUGINVAL=0
PLUGINVAL_PATH=
FOCUS_CHECK=0
NULL_CHECK=0
NULL_ONLY=
TARGETS=
JOBS=4
CLAP=0
ALLOW_INCOMPLETE=0
ALLOW_FETCH=0
ALLOW_STALE=0
need() { if [ $# -lt 2 ]; then echo "option $1 needs a value"; exit 2; fi; }
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) SKIP_BUILD=1 ;;
        --skip-tests) SKIP_TESTS=1 ;;
        --pluginval) PLUGINVAL=1 ;;
        --pluginval-path) need "$@"; PLUGINVAL=1; PLUGINVAL_PATH="$2"; shift ;;
        --focus-check) FOCUS_CHECK=1 ;;
        --null-check) NULL_CHECK=1 ;;
        --null-only) need "$@"; NULL_CHECK=1; NULL_ONLY="$2"; shift ;;
        --targets) need "$@"; TARGETS="$2"; shift ;;
        --jobs) need "$@"; JOBS="$2"; shift ;;
        --clap) CLAP=1 ;;
        --allow-incomplete) ALLOW_INCOMPLETE=1 ;;
        --allow-fetch) ALLOW_FETCH=1 ;;
        --allow-stale) ALLOW_STALE=1 ;;
        --build-dir) need "$@"; BUILD="$2"; shift ;;
        --config) need "$@"; CONFIG="$2"; shift ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done
case "$JOBS" in ''|*[!0-9]*|0) echo "--jobs needs a whole number above zero"; exit 2 ;; esac
START=$(date +%s)
STEPS="fl repo lint build warnings vst3 ctest regress pluginval focus null"
# The state of each step (bash 3.2 on macOS has no associative arrays): STATE_<step>, DETAIL_<step>.
# One result per step: fail outranks incomplete, incomplete outranks ok, and a step of which one part ran and
# another did not is incomplete.
set_state() { eval "STATE_$1=\$2"; }
get_state() { eval "echo \"\${STATE_$1:-}\""; }
set_detail() { eval "DETAIL_$1=\$2"; }
ok() {
    echo "PASS  $2"
    case "$(get_state "$1")" in '') set_state "$1" ok ;; skipped) set_state "$1" incomplete ;; esac
}
fail() { echo "FAIL  $2"; set_state "$1" fail; }
incomplete() { echo "MISS  $2"; [ "$(get_state "$1")" = fail ] || set_state "$1" incomplete; }
skip() {
    echo "SKIP  $2"
    case "$(get_state "$1")" in '') set_state "$1" skipped ;; ok) set_state "$1" incomplete ;; esac
}
# The CHECK line and the exit code; nothing runs after it.
finish() {
    local line bad=0 open=0 k v d
    line="CHECK $(date '+%Y-%m-%d %H:%M') | $(git rev-parse --short HEAD 2> /dev/null)"
    for k in $STEPS; do
        v=$(get_state "$k"); [ -n "$v" ] || v=skipped
        [ "$v" = fail ] && bad=$((bad + 1))
        [ "$v" = incomplete ] && open=$((open + 1))
        d=$(eval "echo \"\${DETAIL_$k:-}\"")
        if [ "$v" = fail ] && [ -n "$d" ]; then v="$v ($d)"; fi
        line="$line | $k $v"
    done
    echo "$line | $(($(date +%s) - START)) s"
    [ "$bad" -eq 0 ] || exit 1
    if [ "$open" -gt 0 ] && [ "$ALLOW_INCOMPLETE" = 0 ]; then
        echo "incomplete: $open step(s) did not run in full (--allow-incomplete accepts that)"
        exit 1
    fi
    exit 0
}

# local.yaml: flat "key: value" lines (local.example.yaml lists the keys); an empty value counts as missing.
local_key() {
    [ -f local.yaml ] || return 0
    sed -n "s/^[[:space:]]*$1[[:space:]]*:[[:space:]]*//p" local.yaml | head -n 1 |
        sed -e "s/^'\(.*\)'[[:space:]]*\(#.*\)\{0,1\}$/\1/" -e 's/^"\(.*\)"[[:space:]]*\(#.*\)\{0,1\}$/\1/' -e "s/[[:space:]]#.*$//"
}
full_path() { case "$1" in /*) echo "$1" ;; *) echo "$ROOT/$1" ;; esac; }
[ -f local.yaml ] || echo "note: no local.yaml (copy local.example.yaml): the defaults are used"
PY=$(local_key python)
[ -n "$PY" ] || PY=python3
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac

# The plug-ins of packaging/plugins.txt, one "target|product name" per line
PLUGINS=()
if [ -f packaging/plugins.txt ]; then
    while IFS='|' read -r t p _; do
        case "$t" in ''|'#'*) continue ;; esac
        PLUGINS+=("$t|$p")
    done < packaging/plugins.txt
fi
# the VST3 bundle of a target and product name, or nothing when it is not built
bundle_of() {
    local b="$BUILD/${1}_artefacts/$CONFIG/VST3/$2.vst3" d
    if [ ! -e "$b" ]; then   # a suite that adds its plug-ins with add_subdirectory builds them one level down
        d=$(find "$BUILD" -type d -name "${1}_artefacts" 2> /dev/null | head -n 1)
        if [ -n "$d" ]; then b="$d/$CONFIG/VST3/$2.vst3"; fi
    fi
    if [ -e "$b" ]; then echo "$b"; fi
}
# the plug-in folder: plugin in a suite of one plug-in, plugins/<Id> in a suite of several
plugin_dir() { if [ -f plugin/plugin.cmake ]; then echo plugin; else echo "plugins/$1"; fi; }

# 1. FL Studio: the Windows form's step
skip fl "FL Studio: closing it is the step of scripts/check.ps1"

# 2. The repository checks
if OUT=$("$PY" scripts/check_repo.py 2>&1); then
    ok repo "check_repo.py"
else
    echo "$OUT"
    N=$(echo "$OUT" | grep -cE ':[0-9]+: ([A-Z]+|release): ')
    set_detail repo "$N findings"
    fail repo "check_repo.py: $N finding(s)"
fi

# 3. The lint tools the Lint workflow runs. A tool that is not installed is reported, never installed.
lint() {
    local name=$1; shift
    if [ -z "$1" ]; then incomplete lint "$name is not installed: not run"; return; fi
    if "$@"; then ok lint "$name"; else fail lint "$name"; fi
}
on_path() { if command -v "$1" > /dev/null 2>&1; then echo "$1"; fi; }
# the tracked files that exist: one deleted in the work tree and not yet staged is no file to lint
MD=(); while IFS= read -r f; do [ -e "$f" ] && MD+=("$f"); done < <(git ls-files '*.md')
YML=(); while IFS= read -r f; do [ -e "$f" ] && YML+=("$f"); done < <(git ls-files '*.yml' '*.yaml')
SH=(); while IFS= read -r f; do [ -e "$f" ] && SH+=("$f"); done < <(git ls-files 'packaging/*.sh' 'scripts/*.sh')
lint markdownlint-cli2 "$(on_path markdownlint-cli2)" ${MD[@]+"${MD[@]}"}
if [ -n "$(on_path yamllint)" ]; then
    lint yamllint yamllint --strict -c .yamllint.yaml ${YML[@]+"${YML[@]}"}
elif "$PY" -c "import yamllint" 2> /dev/null; then   # installed as a module of the interpreter, its launcher not on the PATH
    lint yamllint "$PY" -m yamllint --strict -c .yamllint.yaml ${YML[@]+"${YML[@]}"}
else
    lint yamllint ""
fi
lint actionlint "$(on_path actionlint)"
if [ ${#SH[@]} -gt 0 ]; then lint shellcheck "$(on_path shellcheck)" --severity=warning "${SH[@]}"; fi

# 4. Configure. A first configure passes what CI passes and names the dependency copies; a later one keeps the cache.
PREFIX=$(sed -n 's/^option(\([A-Za-z0-9_]*\)_TOOLS[^A-Za-z0-9_].*/\1/p' CMakeLists.txt | head -n 1)
BUILD_OK=0
TOOLS_ON=0
STAMP="$BUILD/.check-built"      # written when a build of this script finishes
if [ "$SKIP_BUILD" = 1 ]; then
    skip build "build: skipped (--skip-build)"
    skip warnings "warnings: not counted (--skip-build)"
else
    rm -f build.log
    CONF=(-S "$ROOT" -B "$BUILD")
    REFUSED=
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        CONF+=("-DCMAKE_BUILD_TYPE=$CONFIG")
        if command -v ninja > /dev/null 2>&1; then
            CONF+=(-G Ninja)
        elif [ "$(uname -s)" = Darwin ]; then
            # the Makefile generator either sets no job limit or warns that JUCE's nested helper build has no jobserver
            REFUSED="configure refused: Ninja is not installed, and macOS builds need it"
        fi
        if [ -n "$PREFIX" ]; then
            CONF+=("-D${PREFIX}_TOOLS=ON")
            if [ "$CLAP" = 1 ]; then CONF+=("-D${PREFIX}_CLAP=ON"); else CONF+=("-D${PREFIX}_CLAP=OFF"); fi
        fi
        for dep in "juce_src|FETCHCONTENT_SOURCE_DIR_JUCE|juce-src|1" "clap_src|FETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS|clap_juce_extensions-src|$CLAP"; do
            IFS='|' read -r key var dir needed <<< "$dep"
            [ "$needed" = 1 ] || continue
            V=$(local_key "$key")
            if [ -n "$V" ]; then CONF+=("-D$var=$(full_path "$V")")
            elif [ -d "$BUILD/_deps/$dir" ]; then CONF+=("-D$var=$BUILD/_deps/$dir")
            elif [ "$ALLOW_FETCH" = 0 ] && [ -z "$REFUSED" ]; then
                REFUSED="configure refused: local.yaml does not set $key and <build>/_deps/$dir does not exist (--allow-fetch lets CMake download it)"
            fi
        done
    elif [ "$CLAP" = 1 ]; then
        echo "note: the build folder is configured already and keeps its options: --clap applies to a first configure"
    fi
    if [ -n "$REFUSED" ]; then
        fail build "$REFUSED"
    else
        echo "configure: cmake ${CONF[*]}"
        cmake "${CONF[@]}" 2>&1 | tee build.log
        CODE=${PIPESTATUS[0]}
        if [ "$CODE" -ne 0 ]; then
            fail build "configure (exit code $CODE, log: build.log)"
        else
            if [ -n "$PREFIX" ] && grep -qE "^${PREFIX}_TOOLS:BOOL=(ON|TRUE|1|YES)[[:space:]]*$" "$BUILD/CMakeCache.txt"; then TOOLS_ON=1; fi

            # 5. Build ci_targets and what the options need, at a bounded parallel level
            WANT=(ci_targets)
            if [ -n "$TARGETS" ]; then
                IFS=',' read -r -a MORE <<< "$TARGETS"
                for t in "${MORE[@]}"; do t=${t// /}; if [ -n "$t" ]; then WANT+=("$t"); fi; done
            fi
            if [ "$NULL_CHECK" = 1 ] && [ "$TOOLS_ON" = 1 ]; then WANT+=(RefRender); fi
            cmake --build "$BUILD" --config "$CONFIG" --target "${WANT[@]}" --parallel "$JOBS" 2>&1 | tee -a build.log
            CODE=${PIPESTATUS[0]}
            if [ "$CODE" -ne 0 ]; then fail build "build (exit code $CODE, log: build.log)"
            else BUILD_OK=1; touch "$STAMP"; ok build "build: ${WANT[*]}"; fi      # the vst3 step reads the stamp's time
        fi
    fi

    # 6. Warnings in the configure and build output: the rule of the CI and release workflows
    if [ -f build.log ]; then
        HITS=$(grep -E 'warning [A-Z]+[0-9]+|warning ?:|[0-9]+ warnings? generated|CMake (Deprecation )?Warning' build.log | grep -vE '::warning|hint:')
        if [ -n "$HITS" ]; then
            echo "$HITS"
            N=$(echo "$HITS" | wc -l | tr -d ' ')
            set_detail warnings "$N"
            fail warnings "$N warning line(s) in build.log (zero allowed)"
        elif [ "$BUILD_OK" = 1 ]; then
            ok warnings "0 warnings in the configure and build output"
        else
            skip warnings "warnings: none so far, but the build did not finish"
        fi
    else
        skip warnings "warnings: nothing was configured or built"
    fi
fi

# 7. Every VST3 is newer than every source it is built from
if [ "$ALLOW_STALE" = 1 ]; then
    skip vst3 "VST3 freshness: not compared (--allow-stale)"
elif [ "$SKIP_BUILD" = 0 ] && [ "$BUILD_OK" = 0 ]; then
    skip vst3 "VST3 freshness: not compared (the build did not finish)"
elif [ ${#PLUGINS[@]} -eq 0 ]; then
    skip vst3 "no plug-in in packaging/plugins.txt: no VST3 to compare"
else
    for pl in "${PLUGINS[@]}"; do
        t=${pl%%|*}; p=${pl#*|}
        B=$(bundle_of "$t" "$p")
        if [ -z "$B" ]; then
            if [ "$SKIP_BUILD" = 1 ]; then incomplete vst3 "no VST3 of $p under ${t}_artefacts (--skip-build before its first build): not compared"
            else fail vst3 "no VST3 of $p under ${t}_artefacts"; fi
            continue
        fi
        # the plug-in binary, not the bundle's resources: every build rewrites moduleinfo.json
        BIN=$(find "$B" -type f ! -path '*/Resources/*' ! -path '*/_CodeSignature/*' ! -name '*.plist' ! -name '*.json' ! -name PkgInfo | head -n 1)
        if [ -z "$BIN" ]; then fail vst3 "$p.vst3 holds no plug-in binary"; continue; fi
        # what the VST3 is built from: the plug-in's sources, the shared code and the CMake files
        D=$(plugin_dir "$t")
        SRC=()
        for s in "$D/Source" shared cmake CMakeLists.txt "$D/plugin.cmake"; do if [ -e "$s" ]; then SRC+=("$s"); fi; done
        N=$(find "${SRC[@]}" -type f 2> /dev/null | wc -l | tr -d ' ')
        if [ ! -d "$D/Source" ] || [ "$N" = 0 ]; then fail vst3 "$p: no source found under $D/Source: nothing to compare the VST3 with"; continue; fi
        NEWER=
        TAKEN=0
        while IFS= read -r f; do
            # a CMake file that the last finished build of this script took in: that build left the binary as it was
            case "$f" in
                *.cmake|CMakeLists.txt|*/CMakeLists.txt) if [ -f "$STAMP" ] && [ ! "$f" -nt "$STAMP" ]; then TAKEN=$((TAKEN + 1)); continue; fi ;;
            esac
            NEWER=$f
            break
        done < <(find "${SRC[@]}" -type f -newer "$BIN" 2> /dev/null)
        if [ -n "$NEWER" ]; then fail vst3 "$p.vst3 is older than $NEWER: build it again (--allow-stale skips the comparison)"
        elif [ "$TAKEN" -gt 0 ]; then ok vst3 "$p.vst3 is newer than its $N sources, $TAKEN CMake file(s) excepted: older than the last finished build, which left the binary as it was"
        else ok vst3 "$p.vst3 is newer than its $N sources"; fi
    done
fi

# 8. Unit tests and the render regression
if [ "$SKIP_TESTS" = 1 ]; then
    skip ctest "ctest: skipped (--skip-tests)"
    skip regress "render regression: skipped (--skip-tests)"
elif [ "$SKIP_BUILD" = 0 ] && [ "$BUILD_OK" = 0 ]; then
    skip ctest "ctest: not run (the build did not finish)"
    skip regress "render regression: not run (the build did not finish)"
else
    if ctest --test-dir "$BUILD" -C "$CONFIG" --output-on-failure; then ok ctest ctest; else fail ctest ctest; fi
    if compgen -G "tests/regress/golden/*.f32" > /dev/null || compgen -G "tests/regress/*/golden/*.f32" > /dev/null; then
        if "$PY" scripts/render_regress.py --build "$BUILD"; then ok regress "render regression"
        else fail regress "render regression"; fi
    else
        skip regress "no goldens in tests/regress yet: the render regression is skipped"
    fi
fi

# 9. The hand-over checks on the built VST3s, each on request
READY=0
if [ "$SKIP_BUILD" = 1 ] || [ "$BUILD_OK" = 1 ]; then READY=1; fi

# pluginval at strictness 10
if [ "$PLUGINVAL" = 0 ]; then
    skip pluginval "pluginval: not requested (--pluginval)"
elif [ "$READY" = 0 ]; then
    skip pluginval "pluginval: not run (the build did not finish)"
else
    PV=$PLUGINVAL_PATH
    [ -n "$PV" ] || PV=$(local_key pluginval)
    [ -n "$PV" ] || PV=$(command -v pluginval 2> /dev/null)
    if [ -z "$PV" ] || [ ! -x "$PV" ]; then
        incomplete pluginval "pluginval is not installed (--pluginval-path, the pluginval key of local.yaml, the PATH): not run, never downloaded"
    elif [ ${#PLUGINS[@]} -eq 0 ]; then
        skip pluginval "pluginval: no plug-in in packaging/plugins.txt"
    else
        for pl in "${PLUGINS[@]}"; do
            t=${pl%%|*}; p=${pl#*|}
            B=$(bundle_of "$t" "$p")
            if [ -z "$B" ]; then fail pluginval "pluginval: no VST3 of $p"; continue; fi
            if "$PV" --strictness-level 10 --skip-gui-tests --timeout-ms 900000 --validate "$B"; then ok pluginval "pluginval strictness 10: $p"
            else fail pluginval "pluginval: $p"; fi
        done
    fi
fi

# FocusCheck: a Windows tool
if [ "$FOCUS_CHECK" = 0 ]; then skip focus "FocusCheck: not requested (--focus-check)"
else skip focus "FocusCheck: a Windows tool, run by scripts/check.ps1 -FocusCheck"; fi

# The built VST3 against the offline renderer on every case of each plug-in's harness; no reference is loaded
if [ "$NULL_CHECK" = 0 ]; then
    skip null "null check: not requested (--null-check)"
elif [ "$READY" = 0 ]; then
    skip null "null check: not run (the build did not finish)"
elif [ ${#PLUGINS[@]} -eq 0 ]; then
    skip null "null check: no plug-in in packaging/plugins.txt"
else
    for pl in "${PLUGINS[@]}"; do
        t=${pl%%|*}; p=${pl#*|}
        SCRIPT="$(plugin_dir "$t")/nulltest/nulltest.py"
        if [ ! -f "$SCRIPT" ]; then skip null "null check: $p has no $SCRIPT"; continue; fi
        # twice: at the harness's own rate and at 96 kHz
        for fs in "" 96000; do
            NULL_ARGS=("$SCRIPT" --vst3)
            AT="at the harness's rate"
            if [ -n "$fs" ]; then NULL_ARGS+=(--fs "$fs"); AT="at $fs Hz"; fi
            if [ -n "$NULL_ONLY" ]; then NULL_ARGS+=(--only "$NULL_ONLY"); fi
            "$PY" "${NULL_ARGS[@]}"
            CODE=$?
            if [ "$CODE" -eq 0 ]; then ok null "null check: $p $AT: the VST3 equals the renderer (nulltest.py --vst3)"
            elif [ "$CODE" -eq 2 ]; then incomplete null "null check: $p $AT: nulltest.py --vst3 could not run (exit code 2: the VST3, the renderer or RefRender is not built, or no case matches): not compared"
            else fail null "null check: $p $AT: the VST3 differs from the renderer (nulltest.py --vst3, exit code $CODE)"; fi
        done
    done
fi

finish
