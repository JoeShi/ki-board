/**
 * webpage.h - Web 配置前端页面 (嵌入 PROGMEM)
 *
 * 原型阶段把 HTML/CSS/JS 嵌入固件, 省去 LittleFS 上传步骤。
 * 页面通过 REST API (/api/config, /api/keys) 与设备交互。
 */

#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kiro 键盘配置</title>
<style>
  body { font-family: system-ui, sans-serif; background:#1e1e1e; color:#ddd; margin:0; padding:16px; }
  h1 { font-size:20px; color:#4ec9b0; }
  h2 { font-size:15px; color:#9cdcfe; margin-top:24px; border-bottom:1px solid #333; padding-bottom:4px; }
  .card { background:#252526; border:1px solid #333; border-radius:8px; padding:12px; margin:10px 0; }
  label { display:inline-block; width:70px; color:#999; font-size:13px; }
  input[type=text] { background:#3c3c3c; border:1px solid #555; color:#ddd; border-radius:4px; padding:4px 6px; width:120px; }
  select { background:#3c3c3c; border:1px solid #555; color:#ddd; border-radius:4px; padding:4px; }
  .mods { display:inline-block; }
  .mods label { width:auto; margin-right:8px; color:#ccc; }
  .row { margin:6px 0; }
  button { background:#0e639c; color:#fff; border:none; border-radius:4px; padding:8px 18px; font-size:14px; cursor:pointer; margin-top:14px; }
  button:hover { background:#1177bb; }
  #status { margin-left:12px; color:#4ec9b0; }
  .small { font-size:12px; color:#888; }
</style>
</head>
<body>
<h1>⌨ Kiro 快捷键盘配置</h1>
<p class="small">修改后点击"保存并应用"。配置存入设备, 重启后保留。</p>

<h2>按键</h2>
<div id="keys"></div>

<h2>旋钮</h2>
<div id="encoder"></div>

<button onclick="saveConfig()">保存并应用</button>
<span id="status"></span>

<script>
let KEYS = [], MODS = [];
let config = null;

async function init() {
  const meta = await (await fetch('/api/keys')).json();
  KEYS = meta.keys; MODS = meta.modifiers;
  config = await (await fetch('/api/config')).json();
  render();
}

function keySelect(val) {
  let s = '<select>';
  s += '<option value="">(无)</option>';
  for (const k of KEYS) s += `<option ${k===val?'selected':''}>${k}</option>`;
  s += '</select>';
  return s;
}

function modBoxes(active) {
  active = active || [];
  let s = '<span class="mods">';
  for (const m of MODS) {
    const on = active.includes(m) ? 'checked' : '';
    s += `<label><input type="checkbox" value="${m}" ${on}>${m}</label>`;
  }
  s += '</span>';
  return s;
}

function actionRow(prefix, a) {
  a = a || {type:'none'};
  return `<div class="row" data-prefix="${prefix}">
    <label>标签</label><input type="text" class="f-label" value="${a.label||''}">
    <label>按键</label>${keySelect(a.key||'')}
    ${modBoxes(a.modifiers)}
  </div>`;
}

function render() {
  // 按键
  let kh = '';
  config.keys.forEach((k, i) => {
    kh += `<div class="card"><b>Key ${i+1}</b>${actionRow('key'+i, k)}</div>`;
  });
  document.getElementById('keys').innerHTML = kh;

  // 旋钮
  const e = config.encoder;
  let eh = `<div class="card"><b>长按</b>${actionRow('encLong', e.longPress)}</div>`;
  e.modes.forEach((m, i) => {
    eh += `<div class="card"><b>模式 ${i+1}: </b>
      <input type="text" class="m-label" data-mode="${i}" value="${m.label||''}" style="width:90px">
      <div>顺时针 ${actionRow('mode'+i+'cw', m.cw)}</div>
      <div>逆时针 ${actionRow('mode'+i+'ccw', m.ccw)}</div>
    </div>`;
  });
  document.getElementById('encoder').innerHTML = eh;
}

function readAction(div) {
  const label = div.querySelector('.f-label').value;
  const key = div.querySelector('select').value;
  const mods = [...div.querySelectorAll('input[type=checkbox]:checked')].map(c=>c.value);
  if (!key) return {type:'none', label:label};
  return {type:'hotkey', label:label, key:key, modifiers:mods};
}

function saveConfig() {
  const rows = document.querySelectorAll('.row');
  // 按键
  config.keys.forEach((k, i) => {
    const div = document.querySelector('[data-prefix="key'+i+'"]');
    Object.assign(config.keys[i], readAction(div));
  });
  // 旋钮长按
  Object.assign(config.encoder.longPress, readAction(document.querySelector('[data-prefix="encLong"]')));
  // 模式
  config.encoder.modes.forEach((m, i) => {
    const lbl = document.querySelector('.m-label[data-mode="'+i+'"]').value;
    config.encoder.modes[i].label = lbl;
    Object.assign(config.encoder.modes[i].cw, readAction(document.querySelector('[data-prefix="mode'+i+'cw"]')));
    Object.assign(config.encoder.modes[i].ccw, readAction(document.querySelector('[data-prefix="mode'+i+'ccw"]')));
  });

  document.getElementById('status').textContent = '保存中...';
  fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(config)})
    .then(r => r.json())
    .then(d => { document.getElementById('status').textContent = d.ok ? '✓ 已保存' : '✗ 失败'; })
    .catch(() => { document.getElementById('status').textContent = '✗ 网络错误'; });
}

init();
</script>
</body>
</html>
)HTMLPAGE";

#endif // WEBPAGE_H
