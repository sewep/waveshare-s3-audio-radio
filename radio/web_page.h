#pragma once

// Ikona strony: stare radio z anteną w kolorze akcentu panelu. Serwowana jako /favicon.svg
// i /favicon.ico (przeglądarki pytają o ten adres same z siebie).
static const char FAVICON_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#2563eb"/><path d="M22 23 44 11" stroke="#fff" stroke-width="4" stroke-linecap="round"/><rect x="10" y="23" width="44" height="31" rx="7" fill="#fff"/><circle cx="24" cy="38.5" r="8" fill="#2563eb"/><circle cx="24" cy="38.5" r="3" fill="#fff"/><rect x="37" y="31" width="11" height="4" rx="2" fill="#2563eb"/><rect x="37" y="37" width="11" height="4" rx="2" fill="#2563eb"/><rect x="37" y="43" width="11" height="4" rx="2" fill="#2563eb"/></svg>)SVG";

// Strona sterowania radiem (tryb normalny). Zakładki: Radio, Stacje, Podświetlenie, Pomoc;
// aktywna zakładka jest w adresie (#radio, #stacje, #swiatlo, #pomoc). Sekcje mają data-tab,
// a nie id, żeby przeglądarka nie przewijała do nich pod przyklejony pasek zakładek.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio</title>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<meta name="theme-color" content="#2563eb">
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--muted:#6b7280;--accent:#2563eb;--line:#e5e7eb;--danger:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--fg:#e8eaf0;--muted:#9aa1ad;--accent:#60a5fa;--line:#2b2f38;--danger:#f87171}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px 16px 32px}
nav{position:sticky;top:0;z-index:1;background:var(--bg);display:flex;gap:4px;max-width:560px;margin:0 auto;padding:8px 16px;border-bottom:1px solid var(--line)}
nav a{flex:1;text-align:center;padding:8px 4px;border-radius:8px;color:var(--muted);text-decoration:none;font-size:14px;font-weight:600}
nav a.on{background:var(--card);color:var(--accent);box-shadow:0 0 0 1px var(--line)}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
h2{font-size:17px;margin:0 0 8px}
p{margin:6px 0}
.muted{color:var(--muted);font-size:14px}
.now{font-size:20px;font-weight:600;margin:4px 0 2px}
.title{min-height:1.4em}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:8px 12px;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.del{color:var(--danger);border-color:transparent;background:none;padding:8px}
input[type=range]{flex:1;min-width:120px}
input[type=text],input[type=url]{font:inherit;width:100%;padding:8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
input[type=color]{width:44px;height:36px;padding:0;border:1px solid var(--line);border-radius:8px;background:none}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:8px;border-top:1px solid var(--line);padding:4px 0}
li:first-child{border-top:none}
li .name{flex:1;padding:8px 0;cursor:pointer;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li.on .name{color:var(--accent);font-weight:600}
li .url{flex:1;min-width:0}
li .url small{display:block;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
form{display:grid;gap:8px}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0}
.chips button.on{background:var(--accent);border-color:var(--accent);color:#fff}
table{width:100%;border-collapse:collapse;font-size:15px}
th,td{text-align:left;vertical-align:top;padding:8px 6px;border-top:1px solid var(--line)}
th{font-size:13px;color:var(--muted);font-weight:600;border-top:none}
td:first-child{font-weight:600;white-space:nowrap}
ol,ul.dots{margin:6px 0;padding-left:20px}
ul.dots{list-style:disc}ol li,ul.dots li{display:list-item;border:none;padding:2px 0}
code{background:var(--bg);border:1px solid var(--line);border-radius:4px;padding:0 4px;font-size:14px}
</style></head><body>
<nav>
  <a href="#radio">Radio</a><a href="#stacje">Stacje</a><a href="#swiatlo">Podświetlenie</a><a href="#pomoc">Pomoc</a>
</nav>
<main>

<section data-tab="radio">
  <div class="card">
    <div class="muted">Teraz</div>
    <div class="now" id="station">—</div>
    <div class="muted title" id="title"></div>
    <div class="row" style="margin-top:12px">
      <button id="prev" title="Poprzednia stacja">⏮</button>
      <button id="toggle" class="primary">Graj</button>
      <button id="next" title="Następna stacja">⏭</button>
    </div>
    <div class="row" style="margin-top:12px">
      <span class="muted">Głośność</span>
      <input type="range" id="vol" min="0" max="21">
      <span id="volv" class="muted" style="width:2ch"></span>
    </div>
  </div>
  <div class="card">
    <h2>Stacje</h2>
    <ul id="quick"></ul>
  </div>
</section>

<section data-tab="stacje">
  <div class="card">
    <h2>Lista stacji</h2>
    <p class="muted">Numer stacji to jej miejsce na liście; w tej kolejności przełączają przytrzymane Key1 i Key3.</p>
    <ul id="manage"></ul>
  </div>
  <div class="card">
    <h2>Zapowiedź numeru</h2>
    <label class="row"><input type="checkbox" id="announce"> Wypowiadaj numer stacji przy starcie i przy zmianie stacji</label>
    <p class="muted">Nagrania liczebników od 1 do 40 są zapisane w radiu i działają bez internetu.</p>
  </div>
  <div class="card">
    <form id="add">
      <h2>Dodaj stację</h2>
      <input type="text" name="name" placeholder="Nazwa, np. Radio Kraków" required maxlength="40">
      <input type="url" name="url" placeholder="Adres strumienia, np. https://…/stream.mp3" required>
      <button class="primary">Dodaj</button>
      <p class="muted">Radio odtwarza strumienie MP3, AAC, FLAC, Opus i Vorbis po http lub https. Adresy stacji znajdziesz np. na radio-browser.info (pole „URL”) albo w playlistach <code>.m3u</code> / <code>.pls</code> ze stron rozgłośni.</p>
    </form>
  </div>
  <div class="card">
    <h2>Lista domyślna</h2>
    <p class="muted">Przywraca listę stacji wbudowaną w firmware. Stacje dodane ręcznie zostaną usunięte.</p>
    <button id="reset">Przywróć domyślne stacje</button>
  </div>
</section>

<section data-tab="swiatlo">
  <div class="card">
    <h2>Tryb</h2>
    <div class="chips" id="modes"></div>
    <p class="muted" id="modeDesc"></p>
    <label class="row"><input type="checkbox" id="ledDark"> Wygaszaj diody, gdy gra muzyka</label>
    <p class="muted">Ładowanie stacji, Wi-Fi, bateria i stop dalej są sygnalizowane. To samo przełącza przytrzymanie Key2.</p>
  </div>
  <div class="card">
    <h2>Kolor i jasność</h2>
    <div class="row">
      <span class="muted">Kolor</span><input type="color" id="ledColor">
      <span class="muted">Jasność</span><input type="range" id="ledBright" min="5" max="255">
    </div>
    <p class="muted">Kolor dotyczy trybów Stały kolor, Oddech, Kometa, Fala i Iskry. Jasność dotyczy wszystkich trybów.</p>
  </div>
  <div class="card">
    <h2>Sygnały specjalne</h2>
    <table>
      <tr><th>Światło</th><th>Znaczenie</th></tr>
      <tr><td>Tęcza i błyski R-G-B</td><td>Radio właśnie dostało zasilanie.</td></tr>
      <tr><td>Niebieska kometa</td><td>Łączenie z Wi-Fi albo ładowanie stacji (od zapowiedzi numeru do pierwszych dźwięków). Potem wraca wybrany tryb.</td></tr>
      <tr><td>Pomarańczowa kometa</td><td>Kłopot ze strumieniem: ładowanie trwa ponad 8 s, strumień się urwał i radio łączy ponownie albo bufor się opróżnił i muzyka się przerywa.</td></tr>
      <tr><td>Niebieski oddech</td><td>Tryb konfiguracji Wi-Fi (hotspot <code>Radio-XXXX</code>).</td></tr>
      <tr><td>Czerwone pulsowanie</td><td>Bateria poniżej 10% (tylko gdy pomiar baterii jest podłączony).</td></tr>
    </table>
    <p class="muted">W trybie Wyłączone gasną także te sygnały, poza animacją po włączeniu zasilania.</p>
  </div>
</section>

<section data-tab="pomoc">
  <div class="card">
    <h2>Przyciski na płytce</h2>
    <table>
      <tr><th>Przycisk</th><th>Krótko</th><th>Przytrzymanie</th></tr>
      <tr><td>Key1</td><td>Ciszej</td><td>Poprzednia stacja</td></tr>
      <tr><td>Key2</td><td>Graj / stop</td><td>Wygaś / przywróć diody na czas grania</td></tr>
      <tr><td>Key3</td><td>Głośniej</td><td>Następna stacja</td></tr>
      <tr><td>BOOT</td><td>Graj / stop</td><td>3 s: zapomnij Wi-Fi i uruchom konfigurację</td></tr>
      <tr><td>RESET</td><td colspan="2">Restart radia (ustawienia zostają)</td></tr>
    </table>
    <p class="muted">Układ jak w słuchawkach Bluetooth: środkowy przycisk graj/stop, zewnętrzne krótko zmieniają głośność, a przytrzymane (ok. 1 s) zmieniają stację.</p>
  </div>
  <div class="card">
    <h2>Jak korzystać</h2>
    <ul class="dots">
      <li>Kliknij stację na liście, żeby jej słuchać. Radio pamięta ostatnią stację i głośność, a po włączeniu zasilania samo zaczyna grać.</li>
      <li>Przy starcie i każdej zmianie stacji radio mówi jej numer z listy, więc przełączając przyciskami wiesz, gdzie jesteś. Zapowiedź wyłączysz w zakładce Stacje.</li>
      <li>Strona działa na każdym urządzeniu w tej samej sieci pod adresem <code>http://radio.local</code>. Jeśli adres się nie otwiera, użyj numeru IP z sekcji poniżej albo rozłącz VPN.</li>
      <li>Gdy strumień się urwie, radio po kilku sekundach samo połączy się ponownie.</li>
      <li>Zmiany zrobione przyciskami pojawiają się na stronie po kilku sekundach.</li>
    </ul>
  </div>
  <div class="card">
    <h2>Zmiana sieci Wi-Fi</h2>
    <ol>
      <li>Przytrzymaj BOOT przez 3 sekundy — radio uruchomi się ponownie.</li>
      <li>Połącz telefon z siecią <code>Radio-XXXX</code> (bez hasła).</li>
      <li>Strona konfiguracji otworzy się sama, a jeśli nie, wejdź na <code>http://192.168.4.1</code>.</li>
      <li>Wybierz sieć, wpisz hasło i zapisz. Lista stacji i ustawienia zostają.</li>
    </ol>
  </div>
  <div class="card">
    <h2>Urządzenie</h2>
    <table id="device"></table>
  </div>
</section>

</main>
<script>
const $=id=>document.getElementById(id);
let st={},stations=[];
const post=(u,d)=>fetch(u,{method:'POST',body:new URLSearchParams(d||{})});
const MODES=[
  ['off','Wyłączone','Wszystkie diody zgaszone.'],
  ['status','Status','Przygaszona zieleń, gdy radio gra; jedna bursztynowa dioda, gdy stoi.'],
  ['color','Stały kolor','Cały pierścień świeci wybranym kolorem.'],
  ['breathe','Oddech','Wybrany kolor powoli rozjaśnia się i przygasa.'],
  ['rainbow','Tęcza','Kolory tęczy krążą po pierścieniu.'],
  ['comet','Kometa','Świetlny punkt z ogonem krąży w wybranym kolorze.'],
  ['vu','Wizualizer','Diody zapalają się w rytm muzyki, od zielonej do czerwonej.'],
  ['pulse','Puls','Cały pierścień błyska na każde uderzenie, za każdym razem innym kolorem.'],
  ['disco','Disco','Na każde uderzenie diody losują nowe kolory; jasność idzie za głośnością.'],
  ['rcomet','Tęczowa kometa','Kometa z długim ogonem, która płynnie zmienia kolor.'],
  ['wave','Fala','Fala światła w wybranym kolorze krąży po pierścieniu.'],
  ['sparkle','Iskry','Losowe diody rozbłyskują wybranym kolorem na przygaszonym tle.'],
  ['fire','Ogień','Migoczące płomienie, od czerwieni do żółci.'],
  ['police','Policja','Naprzemienne podwójne błyski: czerwone z jednej strony, niebieskie z drugiej.']];

function showTab(){
  let id=(location.hash||'#radio').slice(1);
  if(!document.querySelector(`section[data-tab="${id}"]`))id='radio';
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.dataset.tab===id));
  document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('on',a.hash==='#'+id));
  window.scrollTo(0,0);
}
window.onhashchange=showTab;showTab();

async function load(){stations=await (await fetch('/api/stations')).json();render()}
async function status(){try{st=await (await fetch('/api/status')).json();render()}catch(e){}}
const play=i=>post('/api/play',{i}).then(status);

function render(){
  const cur=stations[st.station];
  $('station').textContent=cur?`${st.station+1}. ${cur.name}`:'—';
  $('title').textContent=!st.playing?'Zatrzymane':st.loading?'Łączenie…':(st.title||'');
  $('announce').checked=!!st.announce;
  $('toggle').textContent=st.playing?'Stop':'Graj';
  if(document.activeElement!==$('vol')){$('vol').value=st.volume;$('volv').textContent=st.volume}

  const q=$('quick');q.innerHTML='';
  const m=$('manage');m.innerHTML='';
  stations.forEach((s,i)=>{
    const li=document.createElement('li');if(st.playing&&i===st.station)li.className='on';
    const n=document.createElement('span');n.className='name';n.textContent=`${i+1}. ${s.name}`;n.onclick=()=>play(i);
    li.append(n);q.append(li);

    const li2=document.createElement('li');
    const u=document.createElement('div');u.className='url';u.textContent=`${i+1}. ${s.name}`;
    const sm=document.createElement('small');sm.textContent=s.url;u.append(sm);
    const d=document.createElement('button');d.className='del';d.textContent='✕';d.title='Usuń';
    d.onclick=()=>{if(confirm('Usunąć '+s.name+'?'))post('/api/stations/delete',{i}).then(load).then(status)};
    li2.append(u,d);m.append(li2);
  });

  if(st.led){
    const md=MODES.find(x=>x[0]===st.led.mode);
    document.querySelectorAll('#modes button').forEach(b=>b.classList.toggle('on',b.dataset.m===st.led.mode));
    $('modeDesc').textContent=md?md[2]:'';
    if(document.activeElement!==$('ledColor'))$('ledColor').value=st.led.color;
    if(document.activeElement!==$('ledBright'))$('ledBright').value=st.led.brightness;
    $('ledDark').checked=!!st.led.dark;
  }

  const rows=[['Adres',st.host?`http://${st.host}`:'—'],['IP',st.ip||'—'],['Wi-Fi',st.rssi!==undefined?`${st.rssi} dBm`+(st.rssi>-67?' (dobry)':st.rssi>-75?' (średni)':' (słaby)'):'—'],
    ['Bateria',st.battery>=0?`${st.battery}%`:'pomiar niepodłączony']];
  $('device').innerHTML='';
  rows.forEach(([k,v])=>{const tr=document.createElement('tr');const a=document.createElement('td');a.textContent=k;const b=document.createElement('td');b.textContent=v;tr.append(a,b);$('device').append(tr)});
}

$('toggle').onclick=()=>post(st.playing?'/api/stop':'/api/play',st.playing?{}:{i:Math.max(0,st.station)}).then(status);
$('prev').onclick=()=>stations.length&&play((st.station-1+stations.length)%stations.length);
$('next').onclick=()=>stations.length&&play((st.station+1)%stations.length);
$('announce').onchange=e=>post('/api/announce',{on:e.target.checked?1:0}).then(status);
$('vol').oninput=e=>$('volv').textContent=e.target.value;
$('vol').onchange=e=>post('/api/volume',{v:e.target.value}).then(status);
$('add').onsubmit=e=>{e.preventDefault();post('/api/stations/add',new FormData(e.target)).then(r=>r.json()).then(r=>{
  if(r.error){alert('Nie dodano: '+r.error);return}e.target.reset();load()})};
$('reset').onclick=()=>{if(confirm('Zastąpić listę stacji domyślną? Stacje dodane ręcznie zostaną usunięte.'))post('/api/stations/reset',{}).then(load).then(status)};

MODES.forEach(([m,label])=>{const b=document.createElement('button');b.dataset.m=m;b.textContent=label;
  b.onclick=()=>post('/api/led',{mode:m}).then(status);$('modes').append(b)});
$('ledColor').onchange=e=>post('/api/led',{color:e.target.value}).then(status);
$('ledBright').onchange=e=>post('/api/led',{brightness:e.target.value}).then(status);
$('ledDark').onchange=e=>post('/api/led',{dark:e.target.checked?1:0}).then(status);
load().then(status);setInterval(status,3000);
</script></body></html>)HTML";

// Strona konfiguracji Wi-Fi (tryb punktu dostępowego).
static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio – Wi-Fi</title>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<meta name="theme-color" content="#2563eb">
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
