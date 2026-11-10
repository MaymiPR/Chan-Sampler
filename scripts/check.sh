#!/usr/bin/env bash
# The local check on Linux and macOS: the steps of the Lint and CI workflows on this machine, the one command before a
# build is handed over and the proof on a private repository, where no workflow runs. scripts/check.ps1 is the
# Windows form (it also closes FL Studio; this script has no host to close).
#
#   bash scripts/check.sh [--skip-build] [--skip-tests] [--build-dir build] [--config Release]
#
# In order: scripts/check_repo.py; markdownlint-cli2, yamllint and actionlint when they are on the PATH (a missing
# tool is reported and skipped, never installed); configure when <build>/CMakeCache.txt is missing (JUCE and
# clap-juce-extensions from the local.yaml keys juce_src and clap_src when present); build ci_targets into build.log;
# the warning count by the workflows' rule (fails above zero); every VST3 in packaging/plugins.txt relinked by this
# build or newer than its sources; ctest; the render regression when goldens exist. Prints one CHECK line with the
# counts and exits 0 only when every step passed.
set -u
cd "$(dirname "$0")/.." || exit 1
BUILD=build
CONFIG=Release
SKIP_BUILD=0
SKIP_TESTS=0
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) SKIP_BUILD=1 ;;
        --skip-tests) SKIP_TESTS=1 ;;
        --build-dir) BUILD="$2"; shift ;;
        --config) CONFIG="$2"; shift ;;
        *) echo "unknown option: $1"; exit 2 ;;
    esac
    shift
done
START=$(date +%s)
FAILED=0
# The state of each step (bash 3.2 on macOS has no associative arrays): STATE_<step>
set_state() { eval "STATE_$1=\$2"; }
get_state() { eval "echo \"\${STATE_$1:-}\""; }
pass() { echo "PASS  $1"; }
fail() { echo "FAIL  $2"; FAILED=$((FAILED + 1)); set_state "$1" fail; }
skip() { echo "SKIP  $2"; [ -n "$(get_state "$1")" ] || set_state "$1" skipped; }

# local.yaml: flat "key: value" lines (local.example.yaml lists the keys); an empty value counts as missing.
local_key() {
    [ -f local.yaml ] || return 0
    sed -n "s/^[[:space:]]*$1[[:space:]]*:[[:space:]]*//p" local.yaml | head -n 1 |
        sed -e "s/^'\(.*\)'[[:space:]]*\(#.*\)\{0,1\}$/\1/" -e 's/^"\(.*\)"[[:space:]]*\(#.*\)\{0,1\}$/\1/' -e "s/[[:space:]]#.*$//"
}
[ -f local.yaml ] || echo "note: no local.yaml (copy local.example.yaml): the defaults are used"
PY=$(local_key python)
[ -n "$PY" ] || PY=python3

# 1. The repository checks
REPO_FINDINGS=0
if OUT=$("$PY" scripts/check_repo.py 2>&1); then
    set_state repo ok; pass "check_repo.py"
else
    echo "$OUT"
    REPO_FINDINGS=$(echo "$OUT" | grep -cE ':[0-9]+: ([A-Z]+|release): ')
    fail repo "check_repo.py: $REPO_FINDINGS finding(s)"
fi

# 2. The lint tools the Lint workflow runs, when installed
lint() {
    local tool=$1; shift
    if ! command -v "$tool" > /dev/null 2>&1; then skip lint "$tool not found: skipped"; return; fi
    if "$tool" "$@"; then pass "$tool"; [ "$(get_state lint)" = fail ] || set_state lint ok; else fail lint "$tool"; fi
}
MD=(); while IFS= read -r f; do MD+=("$f"); done < <(git ls-files '*.md')
YML=(); while IFS= read -r f; do YML+=("$f"); done < <(git ls-files '*.yml' '*.yaml')
lint markdownlint-cli2 ${MD[@]+"${MD[@]}"}
lint yamllint --strict -c .yamllint.yaml ${YML[@]+"${YML[@]}"}
lint actionlint

# 3. Configure when there is no build folder yet
PREFIX=$(sed -n 's/^option(\([A-Za-z0-9_]*\)_TOOLS[^A-Za-z0-9_].*/\1/p' CMakeLists.txt | head -n 1)
WARNINGS=0
if [ "$SKIP_BUILD" = 1 ]; then
    skip build "build: skipped (--skip-build)"
elif [ ! -f "$BUILD/CMakeCache.txt" ]; then
    CONF=(-B "$BUILD" -DCMAKE_BUILD_TYPE="$CONFIG")
    command -v ninja > /dev/null 2>&1 && CONF+=(-G Ninja)
    [ -n "$PREFIX" ] && CONF+=("-D${PREFIX}_TOOLS=ON")
    J=$(local_key juce_src); [ -n "$J" ] && CONF+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$J")
    C=$(local_key clap_src); [ -n "$C" ] && CONF+=("-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=$C")
    echo "configure: cmake ${CONF[*]}"
    cmake "${CONF[@]}" || { fail build "configure"; SKIP_BUILD=1; }
fi

# 4. Build ci_targets into build.log and count the warnings by the workflows' rule
if [ "$SKIP_BUILD" = 0 ]; then
    STAMP="$BUILD/.check-start"
    mkdir -p "$BUILD"; touch "$STAMP"
    cmake --build "$BUILD" --config "$CONFIG" --target ci_targets --parallel 2>&1 | tee build.log
    if [ "${PIPESTATUS[0]}" -ne 0 ]; then fail build "build (log: build.log)"; else set_state build ok; pass build; fi
    HITS=$(grep -E 'warning C[0-9]+|warning ?:|warning MSB|[0-9]+ warnings? generated' build.log | grep -vE '::warning|hint:')
    if [ -n "$HITS" ]; then
        echo "$HITS"
        WARNINGS=$(echo "$HITS" | wc -l | tr -d ' ')
        fail warnings "$WARNINGS warning line(s) in build.log (zero allowed)"
    else
        set_state warnings 0; pass "0 warnings"
    fi

    # 5. Every VST3 is the one this build made: relinked now, or newer than every source it is built from
    if [ -f packaging/plugins.txt ]; then
        STALE=0
        while IFS='|' read -r t p _; do
            case "$t" in ''|'#'*) continue ;; esac
            B="$BUILD/${t}_artefacts/$CONFIG/VST3/$p.vst3"
            if [ ! -e "$B" ]; then   # a suite that adds its plug-ins with add_subdirectory builds them one level down
                D=$(find "$BUILD" -type d -name "${t}_artefacts" 2>/dev/null | head -n 1)
                if [ -n "$D" ]; then B="$D/$CONFIG/VST3/$p.vst3"; fi
            fi
            if [ ! -e "$B" ]; then echo "no VST3 at $B"; STALE=$((STALE + 1)); continue; fi
            [ -n "$(find "$B" -type f -newer "$STAMP" | head -n 1)" ] && continue
            BIN=$(find "$B" -type f -path '*/Contents/*' ! -name '*.plist' | head -n 1)
            NEWER=$(find "plugins/$t" shared CMakeLists.txt \( -name '*.h' -o -name '*.cpp' -o -name '*.cmake' -o -name CMakeLists.txt \) \
                -newer "${BIN:-$B}" 2> /dev/null | head -n 1)
            if [ -n "$NEWER" ]; then echo "stale: $p.vst3 is older than $NEWER"; STALE=$((STALE + 1)); fi
        done < packaging/plugins.txt
        if [ "$STALE" -gt 0 ]; then fail vst3 "$STALE VST3 not rebuilt (a host holds the file, or the build failed)"
        else set_state vst3 ok; pass "every VST3 is current"; fi
    else
        skip vst3 "no packaging/plugins.txt: the VST3 timestamp check is skipped"
    fi
fi

# 6. Unit tests and the render regression
if [ "$SKIP_TESTS" = 1 ]; then
    skip ctest "ctest: skipped (--skip-tests)"
    skip regress "render regression: skipped (--skip-tests)"
else
    if ctest --test-dir "$BUILD" -C "$CONFIG" --output-on-failure; then set_state ctest ok; pass ctest; else fail ctest ctest; fi
    if compgen -G "tests/regress/*/golden/*.f32" > /dev/null; then
        if "$PY" scripts/render_regress.py --build "$BUILD"; then set_state regress ok; pass "render regression"
        else fail regress "render regression"; fi
    else
        skip regress "no goldens in tests/regress yet: the render regression is skipped"
    fi
fi

LINE="CHECK $(date '+%Y-%m-%d %H:%M') | $(git rev-parse --short HEAD 2> /dev/null)"
for k in repo lint build warnings vst3 ctest regress; do
    v=$(get_state "$k"); [ -n "$v" ] || v=skipped
    [ "$k" = repo ] && [ "$REPO_FINDINGS" -gt 0 ] && v="$REPO_FINDINGS findings"
    [ "$k" = warnings ] && [ "$WARNINGS" -gt 0 ] && v="$WARNINGS"
    LINE="$LINE | $k $v"
done
echo "$LINE | $(($(date +%s) - START)) s"
[ "$FAILED" -eq 0 ]
