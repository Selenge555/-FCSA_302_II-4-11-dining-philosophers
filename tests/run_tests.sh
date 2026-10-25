#!/bin/bash
# run_tests.sh -- Dining-Philosophers төслийн автомат шалгалт.
# Ашиглалт: bash tests/run_tests.sh  (эсвэл `make test`)
set -u
cd "$(dirname "$0")/.."
PASS=0
FAIL=0
pass() { PASS=$((PASS+1)); echo "  OK  $1"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL  $1"; }

echo "== 1) build ==========================================="
if make all > /tmp/dp_build.log 2>&1; then pass "build/dp compile хийгдлээ"; else fail "compile амжилтгүй (/tmp/dp_build.log)"; cat /tmp/dp_build.log; fi

echo "== 2) model.js unit тест ==============================="
if command -v node > /dev/null 2>&1; then
  if node tests/test_model.js > /tmp/dp_model_test.log 2>&1; then
    pass "test_model.js бүх шалгалт давсан"
  else
    fail "test_model.js тэнцсэнгүй"; cat /tmp/dp_model_test.log
  fi
else
  echo "  --  node олдсонгүй, model.js тест алгаслаа"
fi

echo "== 3) интеграц -- monitor, инвариант ба min-meals ======"
./build/dp -a monitor -n 5 --think-min 10 --think-max 40 --eat-min 10 --eat-max 40 \
  -d 4000 -s 1 > /tmp/dp_monitor.log
if python3 tests/check_log.py --min-meals 3 /tmp/dp_monitor.log > /tmp/dp_monitor_check.log 2>&1; then
  pass "monitor: инвариант хангасан, бүгд дор хаяж 3 удаа идсэн"
else
  fail "monitor: check_log.py алдаа илрүүлсэн"; cat /tmp/dp_monitor_check.log
fi

echo "== 4) интеграц -- naive, deadlock ======================"
./build/dp -a naive -n 5 --think-min 0 --think-max 0 --eat-min 300 --eat-max 600 \
  -d 8000 -s 1 --grab-delay 300 > /tmp/dp_naive.log
code=$?
if [ "$code" -eq 3 ]; then pass "naive: гарах код 3 (deadlock)"; else fail "naive: гарах код $code (3 байх ёстой байсан)"; fi
if python3 tests/check_log.py --expect-deadlock /tmp/dp_naive.log > /tmp/dp_naive_check.log 2>&1; then
  pass "naive: инвариант хангасан, deadlock илэрсэн"
else
  fail "naive: check_log.py алдаа илрүүлсэн"; cat /tmp/dp_naive_check.log
fi

echo "== 5) захын тохиолдол -- n=2, n=64 ======================"
./build/dp -a monitor -n 2 --think-min 5 --think-max 20 --eat-min 5 --eat-max 20 -d 1500 -s 2 > /tmp/dp_n2.log
python3 tests/check_log.py /tmp/dp_n2.log > /tmp/dp_n2_check.log 2>&1 \
  && pass "n=2 (хамгийн бага) инвариант хангасан" || { fail "n=2 алдаатай"; cat /tmp/dp_n2_check.log; }

./build/dp -a monitor -n 64 --think-min 5 --think-max 15 --eat-min 5 --eat-max 15 -d 2000 -s 3 > /tmp/dp_n64.log
python3 tests/check_log.py /tmp/dp_n64.log > /tmp/dp_n64_check.log 2>&1 \
  && pass "n=64 (хамгийн их) инвариант хангасан" || { fail "n=64 алдаатай"; cat /tmp/dp_n64_check.log; }

echo "== 6) буруу аргумент зөв мэдээлдэг эсэх ================"
./build/dp -n 1 > /dev/null 2>/tmp/dp_badarg1.log; c1=$?
./build/dp -n 5 --think-min 100 --think-max 50 > /dev/null 2>/tmp/dp_badarg2.log; c2=$?
if [ "$c1" -eq 2 ] && [ "$c2" -eq 2 ] && grep -q "." /tmp/dp_badarg1.log && grep -q "." /tmp/dp_badarg2.log; then
  pass "буруу аргументад гарах код 2, ойлгомжтой алдааны мессежтэй"
else
  fail "буруу аргументын шалгалт тэнцсэнгүй (codes: $c1 $c2)"
fi

echo "== 7) UI сервер ========================================="
python3 ui/server.py --port 8123 --quiet > /tmp/dp_server.log 2>&1 &
SV=$!
sleep 1
if curl -fs http://127.0.0.1:8123/health > /dev/null 2>&1; then pass "сервер /health хариулж байна"; else fail "сервер эхлэхгүй байна"; fi
if curl -fs http://127.0.0.1:8123/ | grep -qi "Dining"; then pass "сервер / -ээс index.html хариулж байна"; else fail "/ хариу буруу"; fi
if curl -fs "http://127.0.0.1:8123/run?n=4&tmin=5&tmax=15&emin=5&emax=15&dur=800&seed=1&algo=monitor&grab=50" | head -1 | grep -q '"type":"config"'; then
  pass "сервер /run эхний event нь config"
else
  fail "сервер /run хүлээгдсэн урсгал буцаасангүй"
fi
if curl -s "http://127.0.0.1:8123/run?n=999&tmin=5&tmax=15&emin=5&emax=15&dur=800&seed=1&algo=monitor&grab=50" | grep -q "errors"; then
  pass "сервер буруу параметрт 400/errors буцаасан"
else
  fail "сервер буруу параметрийг зөв боловсруулсангүй"
fi
# Regression: "n=--5" маягийн буруу хэлбэртэй тоо int()-д ValueError шидэж,
# холболтыг тасалдаг (код=000) алдаа байсан. Одоо 400 + errors буцаах ёстой.
rc=$(curl -s -o /tmp/dp_badnum.log -w "%{http_code}" "http://127.0.0.1:8123/run?n=--5&tmin=5&tmax=15&emin=5&emax=15&dur=800&seed=1&algo=monitor&grab=50")
if [ "$rc" = "400" ] && grep -q "errors" /tmp/dp_badnum.log; then
  pass "сервер 'n=--5' маягийн буруу тоог унахгүйгээр 400 буцаасан"
else
  fail "сервер 'n=--5' оролтод буруу хариу буцаасан (HTTP $rc)"
fi
if curl -fs http://127.0.0.1:8123/health > /dev/null 2>&1; then
  pass "сервер буруу оролтын дараа ч амьд хэвээр"
else
  fail "сервер буруу оролтын дараа унасан"
fi
kill "$SV" 2>/dev/null
wait "$SV" 2>/dev/null

echo "== 8) ThreadSanitizer (боломжтой бол, monitor ба naive хоёулаа) =="
if "${CC:-cc}" -std=gnu11 -Wall -Wextra -g -O1 -fsanitize=thread src/dp.c -o build/dp_tsan -pthread > /tmp/dp_tsan_build.log 2>&1; then
  ./build/dp_tsan -a monitor -n 5 --think-min 5 --think-max 20 --eat-min 5 --eat-max 20 -d 2500 -s 1 \
    > /dev/null 2> /tmp/dp_tsan_monitor.log
  if grep -q "WARNING: ThreadSanitizer" /tmp/dp_tsan_monitor.log; then
    fail "ThreadSanitizer (monitor) race condition илрүүлсэн (/tmp/dp_tsan_monitor.log)"
  else
    pass "ThreadSanitizer (monitor): race condition илрээгүй"
  fi

  ./build/dp_tsan -a naive -n 5 --think-min 0 --think-max 0 --eat-min 300 --eat-max 600 \
    --grab-delay 300 -d 4000 -s 1 > /dev/null 2> /tmp/dp_tsan_naive.log
  if grep -q "WARNING: ThreadSanitizer" /tmp/dp_tsan_naive.log; then
    fail "ThreadSanitizer (naive) race condition илрүүлсэн (/tmp/dp_tsan_naive.log)"
  else
    pass "ThreadSanitizer (naive): race condition илрээгүй"
  fi
else
  echo "  --  tsan build амжилтгүй, алгаслаа"
fi

echo "========================================================"
echo "Нийт: $PASS OK, $FAIL FAIL"
[ "$FAIL" -eq 0 ]
