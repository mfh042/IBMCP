#!/usr/bin/env bash
# IBMCP remote test runner: smoke checks (deployed system) + collector unit tests (GTest).
# Intended to run on Euler (192.168.80.136). Exit 0 = all pass, non-zero = any failure.
# No passwords are hardcoded: uses passwordless `sudo -n docker` and unauthenticated API probes.
set +u

PASS=0
FAIL=0
FAILED_ITEMS=()

ok()  { echo "  [PASS] $1"; PASS=$((PASS+1)); }
bad() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); FAILED_ITEMS+=("$1"); }

http_code() { curl -s -o /dev/null -w '%{http_code}' -m 8 "$1"; }

echo "================================================"
echo " IBMCP tests  (smoke + collector unit tests)"
echo " host: $(hostname)   time: $(date '+%F %T')"
echo "================================================"

echo ""
echo "---- 1. Smoke checks (deployed system) ----"

# 1.1 systemd service active
ST=$(systemctl is-active ibmcp-api 2>/dev/null)
if [ "$ST" = "active" ]; then ok "ibmcp-api systemd: active"; else bad "ibmcp-api not active ($ST)"; fi

# 1.2 backend health
BODY=$(curl -s -m 8 http://127.0.0.1:8000/api/v1/ping)
if echo "$BODY" | grep -q '"code":0'; then ok "backend /api/v1/ping -> code:0"; else bad "backend ping abnormal: $BODY"; fi

# 1.3 nginx serves frontend root
C=$(http_code http://127.0.0.1/)
if [ "$C" = "200" ]; then ok "nginx / -> 200 (frontend)"; else bad "nginx / -> $C (expected 200)"; fi

# 1.4 nginx /nodered/ reaches Node-RED editor
C=$(http_code http://127.0.0.1/nodered/)
if [ "$C" = "200" ]; then ok "nginx /nodered/ -> 200 (editor reachable)"; else bad "nginx /nodered/ -> $C (expected 200)"; fi

# 1.5 Node-RED container running
NR=$(sudo -n docker inspect -f '{{.State.Status}}' ibmcp-nodered 2>/dev/null)
if [ "$NR" = "running" ]; then ok "Node-RED container: running"; else bad "Node-RED container: $NR (expected running)"; fi

# 1.6 node-red status route registered (no token -> 401 envelope proves route exists)
BODY=$(curl -s -m 8 http://127.0.0.1:8000/api/v1/node-red/status)
if echo "$BODY" | grep -q 'missing bearer token\|"code":401'; then
    ok "GET /api/v1/node-red/status route exists (no token -> 401)"
else
    bad "node-red status route abnormal: $BODY"
fi

echo ""
echo "---- 2. Collector unit tests (GTest) ----"
BUILD_DIR=/home/meng/ibmcp/backend/build
if cd "$BUILD_DIR" 2>/dev/null; then
    echo "  reconfigure cmake (pick up tests target)..."
    if cmake . >/tmp/ctest_cfg.log 2>&1; then
        ok "cmake configure ok"
    else
        bad "cmake configure failed"; tail -20 /tmp/ctest_cfg.log
    fi

    echo "  build target collector_tests..."
    if cmake --build . --target collector_tests -j"$(nproc)" >/tmp/ctest_build.log 2>&1; then
        ok "build collector_tests ok"
        echo "  run ctest..."
        ctest --output-on-failure >/tmp/ctest_run.log 2>&1
        RC=$?
        tail -25 /tmp/ctest_run.log
        if [ $RC -eq 0 ]; then ok "ctest all passed"; else bad "ctest had failures (exit $RC)"; fi
        # surface gtest case count for visibility (does not affect ctest verdict)
        TEST_BIN=$(find "$BUILD_DIR" -name collector_tests -type f -path '*tests*' 2>/dev/null | head -1)
        if [ -n "$TEST_BIN" ]; then
            echo "  gtest brief ($(basename "$TEST_BIN")):"
            "$TEST_BIN" --gtest_brief=1 2>&1 | grep -E 'Running [0-9]+ tests|(PASSED|FAILED)' | sed 's/^/    /'
        fi
    else
        bad "build collector_tests failed"; tail -30 /tmp/ctest_build.log
    fi
else
    bad "cannot cd to $BUILD_DIR"
fi

echo ""
echo "================================================"
echo " RESULT: pass=$PASS  fail=$FAIL"
if [ "$FAIL" -gt 0 ]; then
    echo " FAILED items:"
    for it in "${FAILED_ITEMS[@]}"; do echo "   - $it"; done
    echo "================================================"
    exit 1
fi
echo " ALL PASSED"
echo "================================================"
exit 0
