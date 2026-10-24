/*
 * model.js -- UI-ийн DOM-оос хамааралгүй логик.
 * Хөтөч дээр window.DPModel, Node дээр module.exports болж ажиллана
 * (tests/test_model.js-ээр шалгагдана).
 */
(function (root) {
  'use strict';

  var LIMITS = {
    n: [2, 64], ms: [0, 600000], dur: [100, 600000],
    seed: [0, 2000000000], grab: [0, 10000]
  };
  var ALGOS = ['monitor', 'naive'];
  var COLORS = { thinking: '#4f8cff', hungry: '#ff9f1a', eating: '#2ecc71', free: '#8a949e' };

  function toInt(v) {
    if (typeof v === 'number') return Number.isInteger(v) ? v : NaN;
    if (typeof v !== 'string' || !/^\s*-?\d+\s*$/.test(v)) return NaN;
    return parseInt(v, 10);
  }

  /* Формын утгыг шалгана. Буцаах: {ok, errors[], params{}} */
  function validateParams(p) {
    var errors = [], out = {};
    var fields = [
      ['n', 'Philosopher-ийн тоо', LIMITS.n],
      ['tmin', 'Бодох доод хугацаа (мс)', LIMITS.ms],
      ['tmax', 'Бодох дээд хугацаа (мс)', LIMITS.ms],
      ['emin', 'Идэх доод хугацаа (мс)', LIMITS.ms],
      ['emax', 'Идэх дээд хугацаа (мс)', LIMITS.ms],
      ['dur', 'Ажиллах хугацаа (мс)', LIMITS.dur],
      ['seed', 'Seed', LIMITS.seed],
      ['grab', 'Fork авах завсар (мс)', LIMITS.grab]
    ];
    fields.forEach(function (f) {
      var v = toInt(p[f[0]]);
      if (Number.isNaN(v)) errors.push(f[1] + ': бүхэл тоо оруулна уу');
      else if (v < f[2][0] || v > f[2][1]) errors.push(f[1] + ': ' + f[2][0] + '..' + f[2][1] + ' хооронд байх ёстой');
      else out[f[0]] = v;
    });
    if (ALGOS.indexOf(p.algo) < 0) errors.push('Алгоритм: monitor эсвэл naive байх ёстой');
    else out.algo = p.algo;
    if (out.tmin !== undefined && out.tmax !== undefined && out.tmin > out.tmax)
      errors.push('Бодох доод хугацаа дээд хугацаанаас их байж болохгүй');
    if (out.emin !== undefined && out.emax !== undefined && out.emin > out.emax)
      errors.push('Идэх доод хугацаа дээд хугацаанаас их байж болохгүй');
    return { ok: errors.length === 0, errors: errors, params: out };
  }

  function toQuery(p) {
    return ['n', 'tmin', 'tmax', 'emin', 'emax', 'dur', 'seed', 'algo', 'grab']
      .map(function (k) { return k + '=' + encodeURIComponent(p[k]); }).join('&');
  }

  function emptySnapshot(n) {
    var s = [], f = [];
    for (var i = 0; i < n; i++) { s.push('thinking'); f.push(-1); }
    return { t: 0, states: s, forks: f, deadlock: false, ended: false, reason: null };
  }

  /* Нэг event-ийг хэрэглэж ШИНЭ snapshot буцаана (өмнөхийг өөрчлөхгүй) */
  function applyEvent(snap, ev) {
    var next = {
      t: snap.t, states: snap.states.slice(), forks: snap.forks.slice(),
      deadlock: snap.deadlock, ended: snap.ended, reason: snap.reason
    };
    if (ev.type === 'state') {
      next.t = ev.t;
      next.states[ev.id] = ev.state;
      next.forks = ev.forks.slice();
    } else if (ev.type === 'deadlock') {
      next.t = ev.t; next.deadlock = true;
    } else if (ev.type === 'end') {
      next.t = ev.t; next.ended = true; next.reason = ev.reason;
    }
    return next;
  }

  function buildSnapshots(n, events) {
    var snaps = [emptySnapshot(n)];
    events.forEach(function (ev) { snaps.push(applyEvent(snaps[snaps.length - 1], ev)); });
    return snaps;
  }

  /* Philosopher i-ийн байрлал (өнцөг: дээд талаас цагийн зүүгээр) */
  function philPos(i, n, cx, cy, R) {
    var a = (-90 + i * 360 / n) * Math.PI / 180;
    return { x: cx + R * Math.cos(a), y: cy + R * Math.sin(a), angle: a };
  }

  /* Philosopher-ийн дүрсийн радиус: цөөн үед том, олон үед жижиг */
  function philRadius(n, R) {
    var chord = 2 * R * Math.sin(Math.PI / n);
    return Math.max(6, Math.min(32, chord * 0.34));
  }

  /*
   * Fork k нь philosopher k-1 ба k-ийн дунд. Чөлөөтэй бол ширээний дундуур
   * (rFree), эзэнтэй бол эзэн рүүгээ шилжсэн (rHeld) байрлалд зурна.
   */
  function forkPos(k, owner, n, cx, cy, rFree, rHeld) {
    var base = (-90 + (k - 0.5) * 360 / n) * Math.PI / 180;
    if (owner < 0) {
      return { x: cx + rFree * Math.cos(base), y: cy + rFree * Math.sin(base), angle: base, held: false };
    }
    var oa = (-90 + owner * 360 / n) * Math.PI / 180;
    var d = Math.atan2(Math.sin(oa - base), Math.cos(oa - base)); /* богино нум */
    var a = base + d * 0.6;
    return { x: cx + rHeld * Math.cos(a), y: cy + rHeld * Math.sin(a), angle: a, held: true };
  }

  function describeEvent(ev) {
    if (ev.type === 'state') return 't=' + ev.t + ' мс  P' + ev.id + ' → ' + ev.state;
    if (ev.type === 'deadlock') return 't=' + ev.t + ' мс  DEADLOCK илэрлээ';
    if (ev.type === 'end') return 't=' + ev.t + ' мс  дууссан (' + ev.reason + ')';
    if (ev.type === 'config') return 'тохиргоо: ' + ev.algorithm + ', n=' + ev.n;
    return JSON.stringify(ev);
  }

  /* Сүүлийн end event-ээс хүснэгтийн мөрүүд.
   * final: ажиллагаа дуусах мөчид тухайн philosopher ХЭВЭЭР HUNGRY байсан бол
   * (жишээ нь нэг ч удаа идэж амжаагүй бол) хэдий хугацаа хүлээснийг харуулна
   * (0 бол тухайн мөчид THINKING/EATING байсан гэсэн үг, заавал "идсэн" гэсэн
   * үг биш — starvation-ыг avg/max_wait далдлахгүй харуулах зорилготой). */
  function statsRows(end) {
    var fw = end.final_hungry_wait_ms || [];
    return end.meals.map(function (m, i) {
      return { id: i, meals: m, avg: end.avg_wait_ms[i], max: end.max_wait_ms[i], final: fw[i] || 0 };
    });
  }

  var api = {
    LIMITS: LIMITS, ALGOS: ALGOS, COLORS: COLORS,
    validateParams: validateParams, toQuery: toQuery,
    emptySnapshot: emptySnapshot, applyEvent: applyEvent, buildSnapshots: buildSnapshots,
    philPos: philPos, philRadius: philRadius, forkPos: forkPos,
    describeEvent: describeEvent, statsRows: statsRows
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.DPModel = api;
})(typeof window !== 'undefined' ? window : globalThis);
