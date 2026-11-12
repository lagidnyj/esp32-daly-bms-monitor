#pragma once

#include <Arduino.h>

namespace WebAssets {

static const char BMS_INDEX_HTML[] PROGMEM = R"BMS_INDEX_HTML(<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>BMS Monitor</title>
<link rel="stylesheet" href="bms.css">
</head>
<body>
<main>
  <section id="pack"></section>
  <section id="grid" class="grid"></section>
  <nav class="tools"><a href="/fs">Файли</a><a href="/update">Оновлення прошивки</a></nav>
</main>

<script>
const CFG = {
  DATA_URL: '/data',
  POLL_MS: 2000,
  FETCH_STALE_MS: 30000,
  TITLE_PRIORITY: ['mac','name','id'],
  PACK_TITLE: 'Battery Pack',
  PACK_VOLTAGE: 'avg',        // 'avg' — паралельні АКБ; 'sum' — послідовні
  CHARGE_POSITIVE: true       // true: струм >0 = заряд
};
const F = { list:'devices', id:'id', mac:'mac', name:'name', connected:'connected',
            voltage:'battery_voltage', current:'charge_current', power:'battery_power',
            remain_ah:'capacity_remain', soc:'percent_remain', alarm:'alarm' };
const ALERT_LABELS = {
  unit_over_voltage_warning_1: 'Завищена напруга комірки — ! 1',
  unit_over_voltage_warning_2: 'Завищена напруга комірки — ! 2',
  total_voltage_high_alarm_1: 'Завищена загальна напруга — ! 1',
  total_voltage_high_alarm_2: 'Завищена загальна напруга — ! 2',
  total_voltage_low_alarm_1: 'Занижена загальна напруга — ! 1',
  total_voltage_low_alarm_2: 'Занижена загальна напруга — ! 2',
  charge_temperature_high_alarm_1: 'Висока темпер. заряджання — ! 1',
  charge_temperature_high_alarm_2: 'Висока темпер. заряджання — ! 2',
  charge_temperature_low_alarm_1: 'Низька темпер. заряджання — ! 1',
  charge_temperature_low_alarm_2: 'Низька темпер. заряджання — ! 2',
  discharge_temperature_high_alarm_1: 'Висока темпер. розряджання — ! 1',
  discharge_temperature_high_alarm_2: 'Висока темпер. розряджання — ! 2',
  discharge_temperature_low_alarm_1: 'Низька темпер. розряджання — ! 1',
  discharge_temperature_low_alarm_2: 'Низька темпер. розряджання — ! 2',
  charge_overcurrent_alarm_1: 'Перевищення струму заряджання — ! 1',
  charge_overcurrent_alarm_2: 'Перевищення струму заряджання — ! 2',
  discharge_overcurrent_alarm_1: 'Перевищення струму розряджання — ! 1',
  discharge_overcurrent_alarm_2: 'Перевищення струму розряджання — ! 2',
  soc_high_alarm_1: 'Високий рівень заряду — тривога 1',
  soc_high_alarm_2: 'Високий рівень заряду — тривога 2',
  soc_low_alarm_1: 'Низький рівень заряду — тривога 1',
  soc_low_alarm_2: 'Низький рівень заряду — тривога 2',
  cell_voltage_difference_alarm_1: 'Великий розкид напруг комірок — тривога 1',
  cell_voltage_difference_alarm_2: 'Великий розкид напруг комірок — тривога 2',
  temperature_difference_alarm_1: 'Великий розкид температур — тривога 1',
  temperature_difference_alarm_2: 'Великий розкид температур — тривога 2',
  charge_mos_overtemperature_warning: 'Перегрів MOSFET заряджання',
  discharge_mos_overtemperature_warning: 'Перегрів MOSFET розряджання',
  charge_mos_temperature_sensor_fault: 'Несправність датчика темпер. MOSFET заряджання',
  discharge_mos_temperature_sensor_fault: 'Несправність датчика темпер. MOSFET розряджання',
  charge_mos_stuck_fault: 'Несправність ключа заряджання (MOSFET)',
  discharge_mos_stuck_fault: 'Несправність ключа розряджання (MOSFET)',
  charge_mos_breaker_fault: 'Несправність вимикача заряджання',
  discharge_mos_breaker_fault: 'Несправність вимикача розряджання',
  afe_fault: 'Несправність AFE',
  cell_collection_fault: 'Помилка зчитування напруг комірок',
  temperature_sensor_fault: 'Несправність датчика температури',
  eeprom_fault: 'Помилка EEPROM',
  rtc_fault: 'Помилка годинника реального часу',
  precharge_fault: 'Помилка передзаряду',
  vehicle_communication_fault: 'Помилка зв’язку з транспортним засобом',
  internal_communication_fault: 'Помилка внутрішнього зв’язку BMS',
  current_module_fault: 'Несправність модуля вимірювання струму',
  main_voltage_detection_fault: 'Помилка вимірювання загальної напруги',
  short_circuit_protection_fault: 'Помилка захисту від короткого замикання',
  low_voltage_no_charge: 'Заряджання за низької напруги заборонено'
};

const $ = s => document.querySelector(s);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const num = v => (v === null || v === '' || v === undefined || !Number.isFinite(+v)) ? null : +v;
const fx = (v, d = 1) => v == null ? '—' : v.toFixed(d);
const fxP = w => w == null ? ['—','W'] : Math.abs(w) >= 1000 ? [(w/1000).toFixed(2),'kW'] : [w.toFixed(0),'W'];
const socClr = s => s == null ? 'var(--mut)' : s > 50 ? 'var(--ok)' : s > 20 ? 'var(--warn)' : 'var(--bad)';

function norm(d) {
  const v = num(d[F.voltage]), a = num(d[F.current]);
  let w = num(d[F.power]); if (w == null && v != null && a != null) w = v * a;
  let al = d[F.alarm]; if (Array.isArray(al)) al = al.join(', ');
  if (!al || /^(0|false|none|ok)$/i.test(String(al))) al = '';
  const alertData = d.alerts;
  const alerts = alertData && typeof alertData === 'object'
    ? {
        available: true,
        raw: typeof alertData.raw === 'string' ? alertData.raw : '',
        items: [
          ...(Array.isArray(alertData.active) ? alertData.active.map(name =>
            ALERT_LABELS[name] || String(name).replace(/_/g, ' ')) : []),
          ...(Array.isArray(alertData.unknown_bits) ? alertData.unknown_bits.map(bit =>
            `Невідомий біт: байт ${num(bit.byte) ?? '?'}, біт ${num(bit.bit) ?? '?'}`) : [])
        ]
      }
    : { available: Boolean(al), raw: '', items: al ? [String(al)] : [] };
  const title = CFG.TITLE_PRIORITY.map(k => d[F[k]]).find(x => x) || '?';

  const cv = (Array.isArray(d.cell_voltages) ? d.cell_voltages : []).map(num);
  const valid = cv.filter(x => x != null);
  const mn = valid.length ? Math.min(...valid) : null, mx = valid.length ? Math.max(...valid) : null;
  let delta = num(d.delta_cell_voltage); if (delta == null && mn != null) delta = mx - mn;
  const avgCell = num(d.average_cell_voltage) ?? (valid.length ? valid.reduce((p,c)=>p+c,0)/valid.length : null);

  const temps = [1,2,3,4].map(i => num(d['battery_t' + i]));
  let n = 4; while (n > 2 && temps[n-1] == null) n--;   // T3/T4 лише якщо є

  return { title, type: d.bms_type || '', v, a, w, ah: num(d[F.remain_ah]), soc: num(d[F.soc]),
           online: d[F.connected] !== false,
           alarm: String(al) || (alerts.items.length ? `${alerts.items.length} тривог` : ''),
           alerts,
           cv, mn, mx, delta, avgCell, cycles: num(d.cycle_count),
           chg: d.charge, dsg: d.discharge, temps: temps.slice(0, n) };
}

function cells(m) {
  const [pw, pu] = fxP(m.w);
  const chg = m.a != null && (CFG.CHARGE_POSITIVE ? m.a > 0 : m.a < 0);
  const soc = m.soc;
  return `
  <div class="cell c-v"><div class="lbl">Volt</div><div class="val num">${fx(m.v,2)}<small>V</small></div></div>
  <div class="cell c-a ${chg?'chg':''}"><div class="lbl">Amp</div><div class="val num">${fx(m.a,1)}<small>A</small></div></div>
  <div class="cell c-w"><div class="lbl">Power</div><div class="val num">${pw}<small>${pu}</small></div></div>
  <div class="cell c-ah"><div class="lbl">Remain</div><div class="val num">${fx(m.ah,1)}<small>Ah</small></div></div>
  <div class="cell c-soc"><div class="lbl">SOC</div><div class="val num" style="color:${socClr(soc)}">${fx(soc,0)}<small>%</small></div>
    <div class="bar"><i style="width:${soc==null?0:Math.max(0,Math.min(100,soc))}%;background:${socClr(soc)}"></i></div></div>`;
}

const chip = (name, on) => `<span class="chip ${on === false ? 'no' : ''}">${name} ${on == null ? '—' : on ? 'ON' : 'OFF'}</span>`;
const tClr = t => t == null ? '' : t >= 60 ? 'bad-t' : t >= 45 ? 'warn-t' : '';
const dClr = d => d == null ? '' : d >= 0.05 ? 'bad-t' : d >= 0.02 ? 'warn-t' : 'ok-t';

function details(m) {
  const cvs = m.cv.map((x, i) => {
    const c = m.cv.length > 1 && m.mx !== m.mn ? (x === m.mx ? 'hi' : x === m.mn ? 'lo' : '') : '';
    return `<div class="${c}"><b class="num">${fx(x,3)}</b><span>C${i+1}</span></div>`;
  }).join('');
  const tmp = m.temps.map((t, i) =>
    `<div class="cell"><div class="lbl">T${i+1}</div><div class="val num ${tClr(t)}">${fx(t,0)}<small>°C</small></div></div>`).join('');
  const alerts = !m.alerts.available
    ? '<div class="alert-empty">Дані про тривоги недоступні</div>'
    : m.alerts.items.length
      ? `<ul class="alert-list">${m.alerts.items.map(item => `<li>${esc(item)}</li>`).join('')}</ul>`
      : '<div class="alert-empty">Активних тривог немає</div>';
  const alertRaw = m.alerts.raw
    ? `<details class="alert-raw"><summary>Сирі дані</summary><code>${esc(m.alerts.raw)}</code></details>`
    : '';
  return `<div class="det">
    <div class="mos">${chip('CHG', m.chg)}${chip('DSG', m.dsg)}</div>
    <div class="st">
      <div class="cell"><div class="lbl">Δ cells</div><div class="val num ${dClr(m.delta)}">${m.delta == null ? '—' : Math.round(m.delta*1000)}<small>mV</small></div></div>
      <div class="cell"><div class="lbl">Avg cell</div><div class="val num">${fx(m.avgCell,3)}<small>V</small></div></div>
      <div class="cell"><div class="lbl">Cycles</div><div class="val num">${m.cycles ?? '—'}</div></div>
    </div>
    <div class="tmp">${tmp}</div>
    ${cvs ? `<div class="cv">${cvs}</div>` : ''}
    <section class="alerts"><div class="lbl">Тривоги BMS</div>${alerts}${alertRaw}</section>
  </div>`;
}

function card(m, extra = '') {
  const cls = !m.online ? 'off' : m.alarm ? 'alm' : '';
  const tag = !m.online ? 'Немає зв’язку'
    : m.alerts?.items.length ? `Тривог: ${m.alerts.items.length}`
    : m.alarm ? `${extra === 'pack' ? 'Статус' : 'Тривога'}: ${esc(m.alarm)}`
    : 'Без тривог';
  const sub = m.type ? `<small>${esc(m.type)}</small>` : '';
  return `<article class="card ${cls} ${extra}">
    <div class="head"><div class="title">${esc(m.title)}${sub}</div><span class="tag">${tag}</span></div>
    <div class="m">${cells(m)}</div>${extra === 'pack' ? '' : details(m)}</article>`;
}

function aggregate(list) {
  const on = list.filter(x => x.online), n = on.length;
  const sum = k => { const a = on.map(x => x[k]).filter(x => x != null); return a.length ? a.reduce((p,c)=>p+c,0) : null; };
  const avg = k => { const s = sum(k); const c = on.filter(x => x[k] != null).length; return s == null ? null : s / c; };
  const alarms = on.map(x => x.alarm).filter(Boolean);
  const off = list.length - n;
  return { title: CFG.PACK_TITLE, type: '', online: n > 0,
    v: CFG.PACK_VOLTAGE === 'sum' ? sum('v') : avg('v'),
    a: sum('a'), w: sum('w'), ah: sum('ah'), soc: avg('soc'),
    alarm: [off ? `${off} offline` : '', ...alarms].filter(Boolean).join('; '),
    cnt: `${n}/${list.length} online` };
}

let LAST = [];
let lastSuccessAt = 0;
let staleNoticeShown = false;
function render(devs, stale = false) {
  const list = devs.map(norm);
  const p = aggregate(list);
  $('#pack').innerHTML = card(p, 'pack').replace(/<span class="tag">/,
    `<span class="tag" style="margin-left:auto;margin-right:8px;color:var(--mut)">${p.cnt}</span>${stale ? '<span class="tag stale">data delayed</span>' : ''}<span class="tag">`);
  $('#grid').innerHTML = list.map(m => card(m)).join('');
}

async function tick() {
  try {
    const r = await fetch(CFG.DATA_URL, { cache: 'no-store', signal: AbortSignal.timeout(15000) });
    if (!r.ok) throw new Error(r.status);
    const j = await r.json();
    const devs = Array.isArray(j) ? j : Array.isArray(j[F.list]) ? j[F.list] :
      Object.entries(j).map(([mac, d]) => ({ ...d, [F.mac]: mac }));
    LAST = devs;
    lastSuccessAt = Date.now();
    staleNoticeShown = false;
    render(devs, false);
  } catch (e) {
    if (LAST.length && !staleNoticeShown &&
        Date.now() - lastSuccessAt >= CFG.FETCH_STALE_MS) {
      render(LAST, true);
      staleNoticeShown = true;
    }
  }
  setTimeout(tick, CFG.POLL_MS);
}
tick();
</script>
</body>
</html>
)BMS_INDEX_HTML";

static const char BMS_STYLE_CSS[] PROGMEM = R"BMS_STYLE_CSS(:root{
  --bg:#0a0e13; --card:#121922; --card2:#0e141b; --line:#1f2a37;
  --txt:#e6edf3; --mut:#7d8b9a;
  --c-v:#5eb8ff; --c-a:#ffb454; --c-w:#c792ff; --c-ah:#4fd1a5;
  --ok:#3ddc97; --warn:#ffb454; --bad:#ff5c6c;
}
*{box-sizing:border-box;margin:0}
html,body{background:var(--bg);color:var(--txt);font:15px/1.35 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
body{min-height:100vh;min-height:100dvh;
  padding:max(12px,env(safe-area-inset-top)) max(12px,env(safe-area-inset-right)) max(10px,env(safe-area-inset-bottom)) max(12px,env(safe-area-inset-left))}
main{width:100%;max-width:1400px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
.num{font-variant-numeric:tabular-nums;font-weight:650}

/* Cards */
.card{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid var(--line);border-radius:14px;padding:12px 14px;position:relative;overflow:hidden}
.card::before{content:"";position:absolute;left:0;top:0;bottom:0;width:3px;background:var(--ok)}
.card.alm::before{background:var(--warn)}
.card.off{background:linear-gradient(180deg,#2a1218,#1a0c10);border-color:#6b2531}
.card.off::before{background:var(--bad)}
.head{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:10px}
.title{font-weight:600;letter-spacing:.02em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.title small{color:var(--mut);font-weight:500;font-size:12px;margin-left:6px}
.tag{font-size:11px;padding:2px 8px;border-radius:99px;border:1px solid currentColor;white-space:nowrap;color:var(--ok)}
.tag.stale{color:var(--warn)}
.alm .tag{color:var(--warn)} .off .tag{color:var(--bad);font-weight:700}

/* Metrics: V, A, W, Ah, SOC */
.m{display:grid;grid-template-columns:repeat(2,1fr);gap:8px 12px}
.cell{min-width:0}
.lbl{font-size:11px;color:var(--mut);text-transform:uppercase;letter-spacing:.08em}
.val{font-size:22px;line-height:1.15;white-space:nowrap}
.val small{font-size:12px;color:var(--mut);font-weight:500;margin-left:3px}
.c-v .val{color:var(--c-v)} .c-a .val{color:var(--c-a)} .c-w .val{color:var(--c-w)} .c-ah .val{color:var(--c-ah)}
.c-a.chg .val{color:var(--ok)}
.c-soc{grid-column:1/-1}
.bar{height:6px;background:#1b2531;border-radius:9px;margin-top:5px;overflow:hidden}
.bar i{display:block;height:100%;width:0;border-radius:9px;background:var(--ok);transition:width .6s}
.off .val{color:#8a5a62 !important} .off .bar i{width:0 !important}

/* Pack */
.pack{padding:16px 18px}
.pack .title{font-size:18px}
.pack .val{font-size:30px}
.pack .m{gap:12px 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}

/* Individual details */
.det{margin-top:12px;padding-top:10px;border-top:1px solid var(--line);display:flex;flex-direction:column;gap:10px}
.mos{display:flex;gap:6px}
.chip{flex:1;text-align:center;font-size:11px;font-weight:600;letter-spacing:.06em;padding:3px 0;border-radius:6px;border:1px solid currentColor;color:var(--ok)}
.chip.no{color:var(--bad)}
.st{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.st .val{font-size:16px}
.tmp{display:grid;grid-template-columns:repeat(auto-fit,minmax(48px,1fr));gap:8px}
.tmp .val{font-size:16px}
.cv{display:grid;grid-template-columns:repeat(auto-fill,minmax(64px,1fr));gap:6px}
.cv div{background:#0a1016;border:1px solid var(--line);border-radius:8px;padding:4px 6px;text-align:center}
.cv b{display:block;font-size:13px;font-weight:600}
.cv span{font-size:10px;color:var(--mut)}
.cv .hi b{color:var(--ok)} .cv .lo b{color:var(--warn)}
.alerts{display:flex;flex-direction:column;gap:6px}
.alert-list{list-style:none;padding:0;display:grid;gap:4px;color:var(--warn);font-size:12px}
.alert-list li::before{content:"!";display:inline-grid;place-items:center;width:15px;height:15px;margin-right:6px;border:1px solid currentColor;border-radius:50%;font-size:10px;font-weight:700}
.alert-empty{color:var(--mut);font-size:12px}
.alert-raw{color:var(--mut);font-size:11px}
.alert-raw summary{cursor:pointer}
.alert-raw code{display:block;margin-top:4px;color:var(--txt);font-variant-numeric:tabular-nums}
.ok-t{color:var(--ok)} .warn-t{color:var(--warn)} .bad-t{color:var(--bad)}
.off .det{opacity:.45}
.tools{display:flex;justify-content:center;gap:14px;padding:4px 0 12px;font-size:12px}
.tools a{color:var(--mut)}

@media (min-width:560px){ .grid{grid-template-columns:repeat(auto-fill,minmax(260px,1fr))} }
@media (min-width:760px){
  .pack .m{grid-template-columns:repeat(5,1fr)}
  .pack .c-soc{grid-column:auto}
  .pack .val{font-size:34px}
}
@media (min-width:1100px){ .pack .val{font-size:40px} .val{font-size:24px} .st .val,.tmp .val{font-size:16px} }
@media (max-height:480px) and (orientation:landscape){ .pack .val{font-size:26px} body{font-size:14px} }
)BMS_STYLE_CSS";

}
