#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kiro Keyboard WiFi</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 18px;
    font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    background: #101214;
    color: #e7ecef;
  }
  main { max-width: 560px; margin: 0 auto; }
  h1 { font-size: 22px; margin: 8px 0 4px; color: #7dd3fc; }
  p { color: #9aa6ad; line-height: 1.5; }
  section {
    border: 1px solid #2f3a40;
    border-radius: 8px;
    padding: 14px;
    margin: 14px 0;
    background: #171b1f;
  }
  h2 { font-size: 15px; margin: 0 0 12px; color: #c7d2da; }
  label { display: block; font-size: 13px; color: #9aa6ad; margin: 12px 0 6px; }
  input, select {
    width: 100%;
    min-height: 40px;
    border: 1px solid #46545c;
    border-radius: 6px;
    padding: 8px 10px;
    background: #0d1114;
    color: #f4f7f8;
    font-size: 15px;
  }
  .row { display: flex; gap: 8px; align-items: center; }
  .row > * { flex: 1; }
  button {
    min-height: 40px;
    border: 0;
    border-radius: 6px;
    padding: 8px 14px;
    background: #0e7490;
    color: white;
    font-size: 14px;
    cursor: pointer;
  }
  button.secondary { background: #334155; }
  button.danger { background: #b91c1c; }
  button:disabled { opacity: .55; cursor: default; }
  .kv { display: grid; grid-template-columns: 120px 1fr; gap: 8px; font-size: 14px; }
  .kv div:nth-child(odd) { color: #8f9ba3; }
  .status { min-height: 22px; margin-top: 12px; color: #86efac; }
  .error { color: #fca5a5; }
  .muted { color: #8f9ba3; font-size: 13px; }
</style>
</head>
<body>
<main>
  <h1>Kiro Keyboard WiFi</h1>
  <p>配置设备要连接的 WiFi。USB HID 和按键功能不依赖 WiFi。</p>

  <section>
    <h2>设备状态</h2>
    <div class="kv">
      <div>模式</div><div id="mode">--</div>
      <div>当前 WiFi</div><div id="currentSsid">--</div>
      <div>IP</div><div id="ip">--</div>
      <div>mDNS</div><div id="mdns">--</div>
      <div>配置热点</div><div id="ap">--</div>
    </div>
  </section>

  <section>
    <h2>连接 WiFi</h2>
    <label for="network">网络</label>
    <div class="row">
      <select id="network"></select>
      <button class="secondary" id="scanBtn" onclick="scanNetworks()">扫描</button>
    </div>
    <label for="ssid">SSID</label>
    <input id="ssid" autocomplete="off" placeholder="输入 WiFi 名称">
    <label for="password">密码</label>
    <input id="password" type="password" autocomplete="current-password" placeholder="开放网络可留空">
    <button id="saveBtn" onclick="saveWifi()">保存并连接</button>
    <div id="saveStatus" class="status"></div>
  </section>

  <section>
    <h2>重置</h2>
    <p class="muted">清除已保存 WiFi 后，设备会回到配置热点模式。也可以在设备上三键同时长按 5 秒清除。</p>
    <button class="danger" onclick="forgetWifi()">清除 WiFi 配置</button>
  </section>
</main>

<script>
const $ = (id) => document.getElementById(id);

async function loadStatus() {
  const status = await (await fetch('/api/wifi/status')).json();
  $('mode').textContent = status.mode || '--';
  $('currentSsid').textContent = status.ssid || '--';
  $('ip').textContent = status.ip || '--';
  $('mdns').textContent = status.mdns || '--';
  $('ap').textContent = `${status.apSsid || '--'} / ${status.apPassword || '--'}`;
}

async function scanNetworks() {
  $('scanBtn').disabled = true;
  $('scanBtn').textContent = '扫描中';
  try {
    const data = await (await fetch('/api/wifi/scan')).json();
    const select = $('network');
    select.innerHTML = '<option value="">手动输入</option>';
    for (const item of data.networks || []) {
      const option = document.createElement('option');
      option.value = item.ssid;
      option.textContent = `${item.ssid} (${item.rssi} dBm)${item.secure ? '' : ' open'}`;
      select.appendChild(option);
    }
  } finally {
    $('scanBtn').disabled = false;
    $('scanBtn').textContent = '扫描';
  }
}

$('network').addEventListener('change', () => {
  if ($('network').value) $('ssid').value = $('network').value;
});

async function saveWifi() {
  $('saveBtn').disabled = true;
  $('saveStatus').className = 'status';
  $('saveStatus').textContent = '保存中...';
  try {
    const res = await fetch('/api/wifi', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ssid: $('ssid').value.trim(), password: $('password').value})
    });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error || 'failed');
    $('saveStatus').textContent = '已保存，设备正在连接。';
    setTimeout(loadStatus, 1500);
  } catch (err) {
    $('saveStatus').className = 'status error';
    $('saveStatus').textContent = '保存失败，请检查 SSID。';
  } finally {
    $('saveBtn').disabled = false;
  }
}

async function forgetWifi() {
  await fetch('/api/wifi/forget', {method: 'POST'});
  $('password').value = '';
  await loadStatus();
}

loadStatus();
scanNetworks();
setInterval(loadStatus, 3000);
</script>
</body>
</html>
)HTMLPAGE";

#endif // WEBPAGE_H
