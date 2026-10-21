#pragma once

// Strona sterowania radiem (tryb normalny).
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--muted:#6b7280;--accent:#2563eb;--line:#e5e7eb;--danger:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--fg:#e8eaf0;--muted:#9aa1ad;--accent:#60a5fa;--line:#2b2f38;--danger:#f87171}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
h1{font-size:20px;margin:0 0 4px}
.muted{color:var(--muted);font-size:14px}
.now{font-size:18px;font-weight:600;margin:8px 0 2px}
.title{min-height:1.4em}
.row{display:flex;gap:8px;align-items:center}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:8px 12px;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.del{color:var(--danger);border-color:transparent;background:none;padding:8px}
input[type=range]{flex:1}
input[type=text],input[type=url]{font:inherit;width:100%;padding:8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:8px;border-top:1px solid var(--line);padding:4px 0}
li:first-child{border-top:none}
li .name{flex:1;padding:8px 0;cursor:pointer;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li.on .name{color:var(--accent);font-weight:600}
form{display:grid;gap:8px}
</style></head><body><main>
<div class="card">
  <h1>Radio</h1>
  <div class="muted" id="net"></div>
  <div class="now" id="station">—</div>
  <div class="muted title" id="title"></div>
  <div class="row" style="margin-top:12px">
    <button id="toggle" class="primary">Graj</button>
    <span class="muted">Głośność</span>
    <input type="range" id="vol" min="0" max="21">
    <span id="volv" class="muted" style="width:2ch"></span>
  </div>
</div>
<div class="card"><ul id="list"></ul></div>
<div class="card">
  <form id="add">
    <strong>Dodaj stację</strong>
    <input type="text" name="name" placeholder="Nazwa" required maxlength="40">
    <input type="url" name="url" placeholder="Adres strumienia (http/https)" required>
    <button class="primary">Dodaj</button>
  </form>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
let st={},stations=[];
const post=(u,d)=>fetch(u,{method:'POST',body:new URLSearchParams(d||{})});
async function load(){stations=await (await fetch('/api/stations')).json();render()}
async function status(){try{st=await (await fetch('/api/status')).json();render()}catch(e){}}
function render(){
  const cur=stations[st.station];
  $('station').textContent=cur?cur.name:'—';
  $('title').textContent=st.playing?(st.title||''):'Zatrzymane';
  $('toggle').textContent=st.playing?'Stop':'Graj';
  if(document.activeElement!==$('vol')){$('vol').value=st.volume;$('volv').textContent=st.volume}
  $('net').textContent=st.host?`${st.host} · ${st.ip} · Wi-Fi ${st.rssi} dBm`:'';
  const ul=$('list');ul.innerHTML='';
  stations.forEach((s,i)=>{
    const li=document.createElement('li');if(st.playing&&i===st.station)li.className='on';
    const n=document.createElement('span');n.className='name';n.textContent=s.name;n.title=s.url;
    n.onclick=()=>post('/api/play',{i}).then(status);
    const d=document.createElement('button');d.className='del';d.textContent='✕';d.title='Usuń';
    d.onclick=()=>{if(confirm('Usunąć '+s.name+'?'))post('/api/stations/delete',{i}).then(load).then(status)};
    li.append(n,d);ul.append(li)});
}
$('toggle').onclick=()=>post(st.playing?'/api/stop':'/api/play',st.playing?{}:{i:Math.max(0,st.station)}).then(status);
$('vol').oninput=e=>$('volv').textContent=e.target.value;
$('vol').onchange=e=>post('/api/volume',{v:e.target.value}).then(status);
$('add').onsubmit=e=>{e.preventDefault();post('/api/stations/add',new FormData(e.target)).then(()=>{e.target.reset();load()})};
load().then(status);setInterval(status,3000);
</script></body></html>)HTML";

// Strona konfiguracji Wi-Fi (tryb punktu dostępowego).
static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio – Wi-Fi</title>
<style>
body{margin:0;background:#f4f5f7;color:#1d2330;font:16px/1.4 system-ui,sans-serif}
main{max-width:420px;margin:0 auto;padding:16px}
form{background:#fff;border:1px solid #e5e7eb;border-radius:12px;padding:16px;display:grid;gap:10px}
input,select,button{font:inherit;padding:8px;border:1px solid #d1d5db;border-radius:8px;width:100%}
button{background:#2563eb;color:#fff;border-color:#2563eb;cursor:pointer}
</style></head><body><main>
<h1>Radio – konfiguracja Wi-Fi</h1>
<form method="post" action="/api/wifi">
  <label>Sieć<select name="ssid" id="ssid"><option>Szukam sieci…</option></select></label>
  <label>Hasło<input type="password" name="pass" autocomplete="current-password"></label>
  <button>Zapisz i uruchom ponownie</button>
</form>
<p>Po restarcie radio będzie pod adresem <b>http://radio.local</b>.</p>
</main>
<script>
fetch('/api/scan').then(r=>r.json()).then(list=>{
  const s=document.getElementById('ssid');s.innerHTML='';
  list.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm)`;s.append(o)});
});
</script></body></html>)HTML";
