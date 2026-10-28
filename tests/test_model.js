/*
 * test_model.js -- ui/model.js-ийн DOM-гүй логикийг шалгах unit тест.
 * Ашиглалт: node tests/test_model.js
 */
'use strict';
const path = require('path');
const M = require(path.join(__dirname, '..', 'ui', 'model.js'));

let failed = 0, passed = 0;
function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error('FAIL:', msg); }
}
function eq(a, b, msg) {
  assert(JSON.stringify(a) === JSON.stringify(b), msg + ' (got ' + JSON.stringify(a) + ', expected ' + JSON.stringify(b) + ')');
}

// --- validateParams ---
{
  const ok = M.validateParams({ algo: 'monitor', n: '5', tmin: '100', tmax: '200', emin: '100', emax: '200', dur: '1000', seed: '1', grab: '50' });
  assert(ok.ok === true, 'validateParams: зөв оролт ok байх ёстой');
  eq(ok.params.n, 5, 'validateParams: n тоо болж хувирсан');
}
{
  const bad = M.validateParams({ algo: 'bogus', n: '1', tmin: '500', tmax: '100', emin: '1', emax: '1', dur: '1000', seed: '1', grab: '0' });
  assert(bad.ok === false, 'validateParams: буруу оролт алдаатай гэж үзэх ёстой');
  assert(bad.errors.some(e => /Алгоритм/.test(e)), 'validateParams: алгоритмын алдаа мессеж агуулах ёстой');
  assert(bad.errors.some(e => /Philosopher/.test(e)), 'validateParams: n хязгаараас гарсан алдаа агуулах ёстой');
  assert(bad.errors.some(e => /Бодох доод/.test(e)), 'validateParams: tmin>tmax алдаа агуулах ёстой');
}
{
  const nonint = M.validateParams({ algo: 'monitor', n: '5.5', tmin: 'abc', tmax: '200', emin: '100', emax: '200', dur: '1000', seed: '1', grab: '50' });
  assert(nonint.ok === false, 'validateParams: бус бүхэл тоо татгалзах ёстой');
}

// --- emptySnapshot / applyEvent ---
{
  const s0 = M.emptySnapshot(3);
  eq(s0.states, ['thinking', 'thinking', 'thinking'], 'emptySnapshot: эхний төлөв бүгд thinking');
  eq(s0.forks, [-1, -1, -1], 'emptySnapshot: эхэндээ fork бүгд чөлөөтэй');

  const s1 = M.applyEvent(s0, { type: 'state', t: 10, id: 1, state: 'hungry', forks: [-1, -1, -1] });
  eq(s1.states, ['thinking', 'hungry', 'thinking'], 'applyEvent(state): зөв philosopher-ийн төлөв өөрчлөгдсөн');
  eq(s0.states, ['thinking', 'thinking', 'thinking'], 'applyEvent: өмнөх snapshot immutable хэвээр');

  const s2 = M.applyEvent(s1, { type: 'deadlock', t: 20 });
  assert(s2.deadlock === true, 'applyEvent(deadlock): deadlock flag тавигдсан');

  const s3 = M.applyEvent(s2, { type: 'end', t: 30, reason: 'timeout' });
  assert(s3.ended === true && s3.reason === 'timeout', 'applyEvent(end): ended/reason зөв тавигдсан');
}

// --- buildSnapshots ---
{
  const events = [
    { type: 'state', t: 5, id: 0, state: 'hungry', forks: [-1, -1] },
    { type: 'state', t: 8, id: 0, state: 'eating', forks: [0, 0] },
    { type: 'end', t: 20, reason: 'timeout' },
  ];
  const snaps = M.buildSnapshots(2, events);
  eq(snaps.length, 4, 'buildSnapshots: эхний хоосон + 3 event = 4 snapshot');
  assert(snaps[3].ended === true, 'buildSnapshots: сүүлийн snapshot ended');
}

// --- philPos / forkPos геометр ---
{
  const p0 = M.philPos(0, 4, 0, 0, 100);
  assert(Math.abs(p0.x - 0) < 1e-9 && Math.abs(p0.y - (-100)) < 1e-9,
    'philPos: P0 дээш (12 цагийн зүг) байрлах ёстой');
}
{
  const free = M.forkPos(1, -1, 4, 0, 0, 50, 90);
  const held = M.forkPos(1, 0, 4, 0, 0, 50, 90);
  const dFree = Math.hypot(free.x, free.y), dHeld = Math.hypot(held.x, held.y);
  assert(Math.abs(dFree - 50) < 1e-6, 'forkPos: чөлөөтэй fork rFree радиуст байх ёстой');
  assert(Math.abs(dHeld - 90) < 1e-6, 'forkPos: эзэнтэй fork rHeld радиуст байх ёстой');
  assert(free.held === false && held.held === true, 'forkPos: held flag зөв');
}

// --- describeEvent / statsRows ---
{
  eq(M.describeEvent({ type: 'deadlock', t: 7 }), 't=7 мс  DEADLOCK илэрлээ', 'describeEvent: deadlock мессеж');
  // Гадаад шүүмж: naive-д "P1 → hungry" хоёр удаа дараалж харагдвал аль
  // fork-ыг аль хэдийн авсныг ялгах боломжгүй байсан тул describeEvent-д
  // fork-ийн мэдээлэл нэмсэн (Report.docx, Хавсралт Г, #4). Гурван
  // шилжилтийг (eating/hungry/thinking) тус тусад нь шалгана.
  eq(M.describeEvent({ type: 'state', t: 5, id: 2, state: 'eating', forks: [-1, -1, 2, 2, -1] }),
    't=5 мс  P2 → eating (fork 2,3 авсан)', 'describeEvent: eating үед ХОЁР fork-ыг нэрээр нь харуулна (богино хэлбэр)');
  eq(M.describeEvent({ type: 'state', t: 0, id: 1, state: 'hungry', forks: [-1, 1, -1, -1, -1] }),
    't=0 мс  P1 → hungry (fork 1-г авсан, нөгөөг хүлээж байна)',
    'describeEvent: hungry үед аль хэдийн барьсан fork-оо ялгаж харуулна (богино хэлбэр)');
  eq(M.describeEvent({ type: 'state', t: 12, id: 0, state: 'thinking', forks: [-1, -1, -1, -1, -1] }),
    't=12 мс  P0 → thinking (fork-уудаа тавьсан)', 'describeEvent: thinking үед fork суллагдсаныг тэмдэглэнэ');
  eq(M.describeEvent({ type: 'ping' }), null,
    'describeEvent: ping (сервер-клиент холболт шалгах дохио) лог/snapshot-д оролцохгүй, null буцаана');
  const end = { meals: [3, 5], avg_wait_ms: [10.5, 20.2], max_wait_ms: [15, 30], final_hungry_wait_ms: [0, 42] };
  eq(M.statsRows(end), [
    { id: 0, meals: 3, avg: 10.5, max: 15, final: 0 },
    { id: 1, meals: 5, avg: 20.2, max: 30, final: 42 },
  ], 'statsRows: хүснэгтийн мөрүүд зөв бүрдсэн (starvation-ийн final талбартай)');
  const endNoFinal = { meals: [1], avg_wait_ms: [5], max_wait_ms: [5] };
  eq(M.statsRows(endNoFinal), [
    { id: 0, meals: 1, avg: 5, max: 5, final: 0 },
  ], 'statsRows: final_hungry_wait_ms талбаргүй үед ч эвдрэхгүй (0 анхдагч)');
}

// --- deadlock-ийн дараах cleanup event snapshot-ийг өөрчлөхгүй ---
{
  // dp.c deadlock илрүүлсний дараа fork-уудаа суллаж state event гаргадаг.
  const ev = [
    { type: 'state', t: 1, id: 0, state: 'hungry', forks: [0, -1] },
    { type: 'state', t: 2, id: 1, state: 'hungry', forks: [0, 1] },
    { type: 'deadlock', t: 3 },
    { type: 'state', t: 4, id: 0, state: 'hungry', forks: [-1, 1] },   // cleanup
    { type: 'state', t: 5, id: 1, state: 'hungry', forks: [-1, -1] },  // cleanup
    { type: 'end', t: 6, reason: 'deadlock' },
  ];
  const sn = M.buildSnapshots(2, ev);
  const last = sn[sn.length - 1];
  eq(last.forks, [0, 1], 'applyEvent: deadlock-ийн дараах cleanup нь fork-ийн эзэмшлийг өөрчлөхгүй');
  assert(last.deadlock === true && last.ended === true && last.reason === 'deadlock',
    'applyEvent: deadlock болон end тэмдэглэгээ cleanup-ийн дараа ч хадгалагдана');
  eq(sn[3].forks, sn[4].forks, 'applyEvent: deadlock snapshot ба дараагийн snapshot ижил');
  // Бодит жишээ лог дээр: эцсийн snapshot-д бүх fork барьцаалагдсан хэвээр.
  const fs = require('fs');
  const lines = fs.readFileSync(path.join(__dirname, 'examples', 'naive_deadlock_n5_sample.jsonl'), 'utf8')
    .split('\n').filter(Boolean).map(l => JSON.parse(l)).filter(e => e.type !== 'exit');
  const n = lines.find(e => e.type === 'config').n;
  const fin = M.buildSnapshots(n, lines).pop();
  assert(fin.deadlock && fin.forks.every(f => f >= 0), 'naive жишээ лог: эцсийн snapshot-д бүх fork эзэнтэй (deadlock-ийн зураг)');
}

console.log(`${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
