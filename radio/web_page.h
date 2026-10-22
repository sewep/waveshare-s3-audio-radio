#pragma once

// Ikona strony: stare radio z anteną w kolorze akcentu panelu. Serwowana jako /favicon.svg
// i /favicon.ico (przeglądarki pytają o ten adres same z siebie).
static const char FAVICON_SVG[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#2563eb"/><path d="M22 23 44 11" stroke="#fff" stroke-width="4" stroke-linecap="round"/><rect x="10" y="23" width="44" height="31" rx="7" fill="#fff"/><circle cx="24" cy="38.5" r="8" fill="#2563eb"/><circle cx="24" cy="38.5" r="3" fill="#fff"/><rect x="37" y="31" width="11" height="4" rx="2" fill="#2563eb"/><rect x="37" y="37" width="11" height="4" rx="2" fill="#2563eb"/><rect x="37" y="43" width="11" height="4" rx="2" fill="#2563eb"/></svg>)SVG";

// Strona radia, ta sama w trybie normalnym i w trybie konfiguracji (hotspot). Zakładki: Radio,
// Stacje, Światło, Wi-Fi, Stan, Pomoc; aktywna zakładka jest w adresie (#radio, #stacje, #swiatlo,
// #wifi, #stan, #pomoc). Zakładka Stan pyta o poziomy dźwięku co 150 ms i o resztę co 2 s, tylko
// gdy jest otwarta. Sekcje mają data-tab, a nie id, żeby przeglądarka nie przewijała do nich pod
// przyklejony pasek zakładek. W trybie konfiguracji (/api/wifi zwraca "ap":true) widać tylko Wi-Fi.
//
// Nagłówek to rysunek radia na tle nocnego nieba: wyświetlacz pokazuje numer i nazwę stacji,
// wskazówka skali i gałki idą za stacją i głośnością, a pierścień 7 diod wokół głośnika
// w przybliżeniu naśladuje wybrany efekt diod na płytce.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Radio</title>
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<meta name="theme-color" content="#0b1026">
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--muted:#6b7280;--accent:#2563eb;--line:#e5e7eb;--danger:#dc2626;--warn:#b45309}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--fg:#e8eaf0;--muted:#9aa1ad;--accent:#60a5fa;--line:#2b2f38;--danger:#f87171;--warn:#fbbf24}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px 16px 32px}
nav{position:sticky;top:0;z-index:1;background:var(--bg);display:flex;gap:4px;max-width:560px;margin:0 auto;padding:8px 16px;border-bottom:1px solid var(--line)}
nav a{flex:1;text-align:center;padding:8px 2px;border-radius:8px;color:var(--muted);text-decoration:none;font-size:14px;font-weight:600}
nav a.on{background:var(--card);color:var(--accent);box-shadow:0 0 0 1px var(--line)}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:16px}
h2{font-size:17px;margin:0 0 8px}
p{margin:6px 0}
.muted{color:var(--muted);font-size:14px}
.warn{color:var(--warn);font-size:14px}
.now{font-size:20px;font-weight:600;margin:4px 0 2px}
.title{min-height:1.4em}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
button{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:8px 12px;cursor:pointer}
button:disabled{opacity:.5;cursor:default}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.small{padding:4px 8px;font-size:13px}
button.del{color:var(--danger);border-color:transparent;background:none;padding:8px}
input[type=range]{flex:1;min-width:120px}
input[type=text],input[type=url],input[type=password]{font:inherit;width:100%;padding:8px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
input[type=color]{width:44px;height:36px;padding:0;border:1px solid var(--line);border-radius:8px;background:none}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:8px;border-top:1px solid var(--line);padding:4px 0}
li:first-child{border-top:none}
li .name{flex:1;padding:8px 0;cursor:pointer;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li.on .name{color:var(--accent);font-weight:600}
li .url{flex:1;min-width:0}
li .url small{display:block;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#nets li{cursor:pointer}
.badge{font-size:12px;color:var(--muted);border:1px solid var(--line);border-radius:999px;padding:1px 8px;white-space:nowrap}
.badge.on{color:var(--accent);border-color:var(--accent)}
.bars{fill:var(--accent);flex:none;vertical-align:-2px}
.lock{fill:none;stroke:var(--muted);stroke-width:1.6;vertical-align:-1px}
.pw{display:flex;gap:8px}
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
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.grid2 label{font-size:13px;color:var(--muted);display:grid;gap:2px}
.grid2 label input{color:var(--fg)}
fieldset{border:none;margin:0;padding:0;display:grid;gap:4px}
.meter{display:grid;grid-template-columns:6.2em 1fr 5.4em;gap:8px;align-items:center;margin:8px 0;font-size:14px}
.meter .db{text-align:right;color:var(--muted);font-variant-numeric:tabular-nums;font-size:13px}
.bar{position:relative;height:14px;border-radius:7px;background:var(--bg);border:1px solid var(--line);overflow:hidden}
.bar i{position:absolute;inset:0;background:linear-gradient(90deg,#22c55e 0,#22c55e 62%,#eab308 82%,#ef4444 100%);clip-path:inset(0 100% 0 0);transition:clip-path .12s linear}
.bar b{position:absolute;top:0;bottom:0;width:2px;left:0;background:var(--fg);opacity:.7;transition:left .12s linear}
.bar.buf i{background:var(--accent);opacity:.75}
.scale{display:grid;grid-template-columns:6.2em 1fr 5.4em;gap:8px;font-size:11px;color:var(--muted)}
.scale div{display:flex;justify-content:space-between}
#apInfo{display:none}
body.ap #apInfo{display:block}
body.ap nav,body.ap .sta{display:none}

/* nagłówek: radio pod nocnym niebem */
.hero{background:linear-gradient(180deg,#070b1f 0%,#131c46 62%,#24306b 100%)}
.hero svg{display:block;width:100%;max-width:560px;height:auto;margin:0 auto}
.hero .led{transition:fill .5s;animation-delay:calc(var(--i) * var(--d,.143s) * -1)}
.hero .tw{animation:tw 4s ease-in-out infinite}
@keyframes tw{0%,100%{opacity:.25}50%{opacity:1}}
.hero .wv{opacity:.18}
.hero.air .wv{animation:wv 2.4s ease-out infinite}
.hero.air .wv:nth-of-type(2){animation-delay:.4s}.hero.air .wv:nth-of-type(3){animation-delay:.8s}
@keyframes wv{0%{opacity:0}25%{opacity:.95}100%{opacity:0}}
.hero .note{opacity:0}
.hero.playing .note{animation:note 3.2s ease-out infinite}
.hero.playing .note+.note{animation-delay:1.6s}
@keyframes note{0%{opacity:0;transform:translate(0,6px)}20%{opacity:1}100%{opacity:0;transform:translate(10px,-34px) rotate(12deg)}}
.hero .cone{transform-box:fill-box;transform-origin:center}
.hero.playing .cone{animation:thump .42s ease-in-out infinite alternate}
@keyframes thump{to{transform:scale(1.3)}}
#ringHue.hue{animation:hue 6s linear infinite}
@keyframes hue{to{filter:hue-rotate(360deg)}}
#ring.spin .led{animation:spin 1s linear infinite}
#ring.wave .led{--d:.343s;animation:spin 2.4s ease-in-out infinite}
#ring.bounce .led{animation:spin 1.2s linear infinite alternate}
@keyframes spin{0%{opacity:1}65%,100%{opacity:.08}}
#ring.breathe .led{animation:breathe 3s ease-in-out infinite}
@keyframes breathe{0%,100%{opacity:.12}50%{opacity:1}}
#ring.beat .led{--d:.21s;animation:breathe calc(.45s + var(--r) * .5s) ease-in-out infinite}
#ring.pulse .led{animation:pulse .5s ease-out infinite}
@keyframes pulse{0%{opacity:1}100%{opacity:.1}}
#ring.twinkle .led{animation:twinkle 1.8s ease-out infinite;animation-delay:calc(var(--r) * -1.8s)}
@keyframes twinkle{0%{opacity:1}35%,100%{opacity:.12}}
#ring.flicker .led{animation:flicker calc(.3s + var(--r) * .4s) steps(4) infinite alternate}
@keyframes flicker{0%{opacity:.45}100%{opacity:1}}
#ring.police .led{animation:police .7s steps(1) infinite}
#ring.police .led:nth-child(-n+3){animation-delay:-.35s}
@keyframes police{0%{opacity:1}50%{opacity:.06}}
#ring.aurora .led{--d:.5s;animation:breathe 3.5s ease-in-out infinite}
.hero:not(.playing) #ring:is(.beat,.pulse) .led{animation:none;opacity:.3}
#hName.marq{animation:marq 8s ease-in-out infinite alternate}
@keyframes marq{0%,20%{transform:translateX(0)}80%,100%{transform:translateX(var(--dx))}}
@media (prefers-reduced-motion:reduce){.hero *{animation:none!important}}
</style></head><body>
<header class="hero" id="hero">
<svg id="heroSvg" viewBox="0 0 560 172" role="img" aria-label="Rysunek radia Smart spiker">
  <defs>
    <linearGradient id="wood" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#c2531a"/><stop offset=".55" stop-color="#93360f"/><stop offset="1" stop-color="#5f2109"/></linearGradient>
    <linearGradient id="brass" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fde68a"/><stop offset="1" stop-color="#a16207"/></linearGradient>
    <radialGradient id="grille"><stop offset="0" stop-color="#2b2420"/><stop offset="1" stop-color="#0c0a09"/></radialGradient>
    <filter id="glow" x="-80%" y="-80%" width="260%" height="260%"><feGaussianBlur stdDeviation="2.6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <filter id="soft" x="-20%" y="-50%" width="140%" height="200%"><feGaussianBlur stdDeviation="1.1" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <mask id="moonCut"><rect width="560" height="172" fill="#fff"/><circle cx="80" cy="33" r="15" fill="#000"/></mask>
    <clipPath id="dispClip"><rect x="288" y="79" width="104" height="31"/></clipPath>
  </defs>
  <g fill="#fff">
    <circle cx="28" cy="22" r="1.1" class="tw"/><circle cx="118" cy="16" r="1.3" class="tw" style="animation-delay:-1.3s"/>
    <circle cx="40" cy="78" r=".9"/><circle cx="104" cy="66" r="1.2" class="tw" style="animation-delay:-2.1s"/>
    <circle cx="16" cy="128" r="1"/><circle cx="62" cy="112" r="1.4" class="tw" style="animation-delay:-.6s"/>
    <circle cx="186" cy="18" r="1"/><circle cx="246" cy="30" r="1.2" class="tw" style="animation-delay:-3s"/>
    <circle cx="318" cy="14" r="1.1" class="tw" style="animation-delay:-1.8s"/><circle cx="372" cy="34" r=".9"/>
    <circle cx="502" cy="20" r="1.3" class="tw" style="animation-delay:-2.6s"/><circle cx="540" cy="58" r="1"/>
    <circle cx="512" cy="102" r="1.2" class="tw" style="animation-delay:-.9s"/><circle cx="546" cy="140" r=".9"/>
    <circle cx="478" cy="132" r="1.1"/>
  </g>
  <circle cx="70" cy="40" r="16" fill="#fef3c7" mask="url(#moonCut)" filter="url(#soft)"/>
  <ellipse cx="280" cy="165" rx="150" ry="5" fill="#000" opacity=".35"/>

  <!-- fale radiowe z anteny -->
  <g fill="none" stroke="#93c5fd" stroke-width="2.5" stroke-linecap="round">
    <path class="wv" d="M453.7 12.8A12 12 0 0 1 455.2 29.7"/>
    <path class="wv" d="M460.1 5.2A22 22 0 0 1 462.9 36.1"/>
    <path class="wv" d="M466.6 -2.5A32 32 0 0 1 470.5 42.6"/>
  </g>
  <rect x="385" y="49" width="14" height="7" rx="2" fill="#44403c"/>
  <path d="M392 52 446 22" stroke="#a8a29e" stroke-width="3" stroke-linecap="round"/>
  <circle cx="446" cy="22" r="4" fill="#fb7185" filter="url(#glow)"/>

  <!-- obudowa -->
  <rect x="170" y="156" width="26" height="10" rx="3" fill="#3b1606"/>
  <rect x="364" y="156" width="26" height="10" rx="3" fill="#3b1606"/>
  <rect x="140" y="54" width="280" height="106" rx="20" fill="url(#wood)"/>
  <g fill="none" stroke="#fff" stroke-opacity=".07" stroke-width="1.2">
    <path d="M150 60q70-5 130 0t130 0"/><path d="M146 154q80 4 140 0t126 0"/><path d="M144 104q4 20 2 44"/><path d="M416 70q3 30 0 60"/>
  </g>
  <rect x="140.5" y="54.5" width="279" height="105" rx="19.5" fill="none" stroke="#fff" stroke-opacity=".14"/>
  <rect x="152" y="66" width="256" height="84" rx="12" fill="#17110d"/>

  <!-- głośnik z pierścieniem diod -->
  <circle cx="200" cy="108" r="35" fill="none" stroke="#241b16" stroke-width="11"/>
  <circle cx="200" cy="108" r="28" fill="url(#grille)"/>
  <g fill="none" stroke="#3f3631" stroke-width="1.2"><circle cx="200" cy="108" r="22"/><circle cx="200" cy="108" r="16"/><circle cx="200" cy="108" r="10.5"/></g>
  <circle class="cone" cx="200" cy="108" r="6" fill="#4a403a"/>
  <g id="ringHue"><g filter="url(#glow)"><g id="ring">
    <circle class="led" style="--i:0;--r:0" cx="200" cy="73" r="4.2"/>
    <circle class="led" style="--i:1;--r:.57" cx="227.4" cy="86.2" r="4.2"/>
    <circle class="led" style="--i:2;--r:.29" cx="234.1" cy="115.8" r="4.2"/>
    <circle class="led" style="--i:3;--r:.86" cx="215.2" cy="139.5" r="4.2"/>
    <circle class="led" style="--i:4;--r:.14" cx="184.8" cy="139.5" r="4.2"/>
    <circle class="led" style="--i:5;--r:.71" cx="165.9" cy="115.8" r="4.2"/>
    <circle class="led" style="--i:6;--r:.43" cx="172.6" cy="86.2" r="4.2"/>
  </g></g></g>

  <!-- wyświetlacz ze skalą -->
  <rect x="250" y="74" width="146" height="46" rx="6" fill="#03171b" stroke="#0f3d44"/>
  <g fill="#5eead4" font-family="ui-monospace,Menlo,Consolas,monospace" filter="url(#soft)">
    <text id="hNum" x="270" y="103" text-anchor="middle" font-size="22" font-weight="700">--</text>
    <g clip-path="url(#dispClip)"><text id="hName" x="290" y="93" font-size="11">RADIO</text></g>
    <text id="hState" x="290" y="106" font-size="8" fill-opacity=".75"></text>
  </g>
  <path d="M256 117v-4M264 117v-2M272 117v-2M280 117v-2M288 117v-4M296 117v-2M304 117v-2M312 117v-2M320 117v-4M328 117v-2M336 117v-2M344 117v-2M352 117v-4M360 117v-2M368 117v-2M376 117v-2M384 117v-4M390 117v-2" stroke="#2dd4bf" stroke-opacity=".45"/>
  <rect id="needle" x="255" y="110" width="2" height="9" rx="1" fill="#fb7185" filter="url(#soft)"/>

  <!-- gałki i tabliczka -->
  <circle cx="266" cy="135" r="9.5" fill="url(#brass)" stroke="#713f12"/>
  <path id="knobSt" d="M266 135v-7" stroke="#451a03" stroke-width="2" stroke-linecap="round"/>
  <circle cx="380" cy="135" r="9.5" fill="url(#brass)" stroke="#713f12"/>
  <path id="knobVol" d="M380 135v-7" stroke="#451a03" stroke-width="2" stroke-linecap="round"/>
  <text x="323" y="138" text-anchor="middle" font-family="system-ui,sans-serif" font-size="8" font-weight="700" letter-spacing="1.6" fill="#e7b35a">SMART SPIKER</text>

  <!-- nutki, gdy gra -->
  <g fill="#fcd34d">
    <g class="note"><g transform="translate(176 50)"><ellipse rx="4.2" ry="3.2" transform="rotate(-20)"/><path d="M3.6-1V-17q5 2 6.5 7" fill="none" stroke="#fcd34d" stroke-width="1.6"/></g></g>
    <g class="note"><g transform="translate(226 46)"><ellipse rx="4.2" ry="3.2" transform="rotate(-20)"/><path d="M3.6-1V-17q5 2 6.5 7" fill="none" stroke="#fcd34d" stroke-width="1.6"/></g></g>
  </g>
</svg>
</header>
<nav>
  <a href="#radio">Radio</a><a href="#stacje">Stacje</a><a href="#swiatlo">Światło</a><a href="#wifi">Wi-Fi</a><a href="#stan">Stan</a><a href="#pomoc">Pomoc</a>
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
    <p class="muted">Nagrania liczebników od 1 do 40 są zapisane w radiu i działają bez internetu. Stacje od 41. (lista mieści 99) grają bez zapowiedzi.</p>
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
    <div class="row" id="autoRow"><span class="muted">Zmiana co</span><input type="range" id="ledAuto" min="5" max="60"><span id="ledAutov"></span></div>
    <label class="row"><input type="checkbox" id="ledDark"> Wygaszaj diody, gdy gra muzyka</label>
    <p class="muted">Ładowanie stacji, Wi-Fi, bateria i stop dalej są sygnalizowane. To samo przełącza przytrzymanie Key2.</p>
    <p class="muted">Krótkie naciśnięcie BOOT na płytce włącza następny tryb z listy (i zdejmuje wygaszanie, żeby zmianę było widać).</p>
  </div>
  <div class="card">
    <h2>Kolor i jasność</h2>
    <div class="row">
      <span class="muted">Kolor</span><input type="color" id="ledColor">
      <span class="muted">Jasność</span><input type="range" id="ledBright" min="5" max="255">
    </div>
    <p class="muted">Kolor dotyczy trybów Stały kolor, Oddech, Kometa, Fala i Iskry (także w pokazie). Jasność dotyczy wszystkich trybów.</p>
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

<section data-tab="wifi">
  <div class="card" id="apInfo">
    <h2>Konfiguracja Wi-Fi</h2>
    <p id="apReason"></p>
    <button id="apCancel">Wróć bez zmian</button>
  </div>
  <div class="card sta">
    <h2>Połączenie</h2>
    <table id="wifiNow"></table>
    <p class="warn" id="wifiWarn" hidden></p>
  </div>
  <div class="card sta">
    <h2>Adres IP</h2>
    <fieldset>
      <label class="row"><input type="radio" name="ipmode" value="dhcp"> Automatycznie (DHCP)</label>
      <label class="row"><input type="radio" name="ipmode" value="static"> Stały adres</label>
    </fieldset>
    <div class="grid2" id="ipFields" style="margin:8px 0">
      <label>Adres IP<input type="text" id="ipAddr" inputmode="decimal" placeholder="192.168.1.50"></label>
      <label>Maska<input type="text" id="ipMask" inputmode="decimal" placeholder="255.255.255.0"></label>
      <label>Brama<input type="text" id="ipGw" inputmode="decimal" placeholder="192.168.1.1"></label>
      <label>DNS<input type="text" id="ipDns" inputmode="decimal" placeholder="jak brama"></label>
    </div>
    <button class="primary" id="ipSave">Zapisz i połącz ponownie</button>
    <p class="muted" id="ipNote"></p>
  </div>
  <div class="card">
    <h2>Zapamiętane sieci</h2>
    <ul id="saved"></ul>
    <p class="muted">Radio pamięta dwie sieci. Przy starcie łączy się z ostatnio używaną, a gdy ta nie odpowiada, z drugą. Dzięki temu pomyłka w haśle nowej sieci nie odcina radia od internetu.</p>
  </div>
  <div class="card">
    <h2>Połącz z siecią</h2>
    <div class="row"><button id="scan">Szukaj sieci</button><span class="muted" id="scanMsg"></span></div>
    <ul id="nets" style="margin:8px 0"></ul>
    <form id="wifiForm">
      <input type="text" id="ssidIn" placeholder="Nazwa sieci (SSID)" maxlength="32" required autocapitalize="off" autocorrect="off" spellcheck="false">
      <div class="pw"><input type="password" id="passIn" placeholder="Hasło" maxlength="64" autocomplete="current-password"><button type="button" id="showPw">Pokaż</button></div>
      <p class="muted" id="passHint" hidden></p>
      <button class="primary">Zapisz i połącz</button>
      <p class="muted">Radio uruchomi się ponownie i połączy z tą siecią. Jeśli nie odpowie, wróci do poprzedniej. Sieć ukrytą wpisz ręcznie.</p>
    </form>
  </div>
  <div class="card" id="wifiDone" hidden>
    <h2>Zapisano</h2>
    <p id="doneMsg"></p>
  </div>
</section>

<section data-tab="stan">
  <div class="card">
    <h2>Poziomy dźwięku</h2>
    <div class="meter"><span>Głośnik</span><div class="bar" id="lvOut"><i></i><b></b></div><span class="db" id="lvOutV">—</span></div>
    <div class="meter"><span>Mikrofon 1</span><div class="bar" id="lvMic0"><i></i><b></b></div><span class="db" id="lvMic0V">—</span></div>
    <div class="meter"><span>Mikrofon 2</span><div class="bar" id="lvMic1"><i></i><b></b></div><span class="db" id="lvMic1V">—</span></div>
    <div class="scale"><span></span><div><span>−60</span><span>−40</span><span>−20</span><span>0 dBFS</span></div><span></span></div>
    <p class="muted">Pasek to średni poziom z ostatnich 100 ms, kreska to szczyt (wolno opada); liczby: średnio / szczyt. Głośnik to sygnał po regulacji głośności, czyli to, co dostaje wzmacniacz. Mikrofony mają wzmocnienie 30 dB i słyszą także sam głośnik.</p>
  </div>
  <div class="card">
    <h2>Strumień</h2>
    <table id="dStream"></table>
    <div class="meter" style="margin-top:4px"><span>Bufor</span><div class="bar buf" id="dBuf"><i></i></div><span class="db" id="dBufV">—</span></div>
  </div>
  <div class="card">
    <h2>Wi-Fi</h2>
    <table id="dWifi"></table>
  </div>
  <div class="card">
    <h2>System</h2>
    <table id="dSys"></table>
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
      <tr><td>BOOT</td><td>Następny tryb diod (jak na liście w zakładce Światło)</td><td>3 s: tryb konfiguracji Wi-Fi (zapisane sieci zostają); w nim 3 s: powrót bez zmian</td></tr>
      <tr><td>RESET</td><td colspan="2">Restart radia (ustawienia zostają)</td></tr>
    </table>
    <p class="muted">Układ jak w słuchawkach Bluetooth: środkowy przycisk graj/stop, zewnętrzne krótko zmieniają głośność, a przytrzymane (ok. 1 s) zmieniają stację.</p>
  </div>
  <div class="card">
    <h2>Jak korzystać</h2>
    <ul class="dots">
      <li>Kliknij stację na liście, żeby jej słuchać. Radio pamięta ostatnią stację i głośność, a po włączeniu zasilania samo zaczyna grać.</li>
      <li>Przy starcie i każdej zmianie stacji radio mówi jej numer z listy, więc przełączając przyciskami wiesz, gdzie jesteś. Zapowiedź wyłączysz w zakładce Stacje.</li>
      <li>Strona działa na każdym urządzeniu w tej samej sieci pod adresem <code>http://radio.local</code>. Jeśli adres się nie otwiera, użyj numeru IP z zakładki Stan albo rozłącz VPN.</li>
      <li>Gdy strumień się urwie, radio po kilku sekundach samo połączy się ponownie.</li>
      <li>Zmiany zrobione przyciskami pojawiają się na stronie po kilku sekundach.</li>
    </ul>
  </div>
  <div class="card">
    <h2>Zmiana sieci Wi-Fi</h2>
    <p>Najprościej w zakładce Wi-Fi. Gdy radio nie ma dostępu do żadnej zapamiętanej sieci (np. w nowym miejscu), samo wystawia hotspot. Można go też włączyć ręcznie:</p>
    <ol>
      <li>Przytrzymaj BOOT przez 3 sekundy — radio uruchomi się ponownie w trybie konfiguracji (niebieski oddech diod).</li>
      <li>Połącz telefon z siecią <code>Radio-XXXX</code> (bez hasła).</li>
      <li>Strona konfiguracji otworzy się sama, a jeśli nie, wejdź na <code>http://192.168.4.1</code>.</li>
      <li>Wybierz sieć, wpisz hasło i zapisz. Zapisane sieci, lista stacji i ustawienia zostają.</li>
    </ol>
    <p class="muted">Gdy przez 5 minut nikt nie jest połączony z hotspotem, radio samo wraca do zapamiętanych sieci.</p>
  </div>
</section>

</main>
<script>
const $=id=>document.getElementById(id);
let st={},stations=[],apMode=false,wifi={saved:[]},openNets=new Set();
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
  ['police','Policja','Naprzemienne podwójne błyski: czerwone z jednej strony, niebieskie z drugiej.'],
  ['colororgan','Kolorofon','Jak dawny kolorofon: czerwone diody grają bas, zielone średnie tony, niebieskie wysokie.'],
  ['spectrum','Widmo','Analizator widma: każda dioda to inne pasmo, od basu (czerwona) do wysokich (fioletowa).'],
  ['confetti','Konfetti','Losowe diody rozbłyskują losowymi kolorami i powoli gasną.'],
  ['aurora','Zorza','Płynące zielenie, błękity i fiolety jak zorza polarna.'],
  ['bounce','Piłka','Świetlna kula odbija się tam i z powrotem, zmieniając kolor.'],
  ['twins','Dwie komety','Dwie komety w dopełniających się kolorach krążą w przeciwne strony.'],
  ['auto','Pokaz','Efekty zmieniają się same, kolejno, z płynnym przejściem.']];

// Pierścień na rysunku: [kolory 7 diod, animacja]; „hue” kręci barwami całego pierścienia.
const DIM='#3a2f28',hue=h=>`hsl(${h},100%,55%)`,fill7=c=>Array(7).fill(c);
const LOOKS={
  off:()=>[fill7(DIM),''],
  status:()=>[st.playing?fill7('#1f8f45'):['#ffb000',...fill7(DIM).slice(1)],''],
  color:c=>[fill7(c),''],
  breathe:c=>[fill7(c),'breathe'],
  rainbow:()=>[[0,1,2,3,4,5,6].map(i=>hue(i*51)),'hue'],
  comet:c=>[fill7(c),'spin'],
  vu:()=>[['#22c55e','#22c55e','#84cc16','#eab308','#f59e0b','#f97316','#ef4444'],'beat'],
  pulse:()=>[fill7('#ff3d7f'),'pulse hue'],
  disco:()=>[['#ff0040','#00e0ff','#ffd000','#8000ff','#00ff60','#ff6a00','#ff00c8'],'beat hue'],
  rcomet:()=>[fill7('#ff2020'),'spin hue'],
  wave:c=>[fill7(c),'wave'],
  sparkle:c=>[fill7(c),'twinkle'],
  fire:()=>[['#ff2a00','#ff6a00','#ffa000','#ff4500','#ffb300','#ff3000','#ff8000'],'flicker'],
  police:()=>[['#ff1a1a','#ff1a1a','#ff1a1a','#1a4dff','#1a4dff','#1a4dff','#1a4dff'],'police'],
  colororgan:()=>[['#ff2020','#ff2020','#20ff40','#20ff40','#2050ff','#2050ff','#2050ff'],'beat'],
  spectrum:()=>[[0,40,80,140,200,250,285].map(hue),'beat'],
  confetti:()=>[[0,200,50,300,120,30,260].map(hue),'twinkle hue'],
  aurora:()=>[['#22c55e','#10b981','#06b6d4','#3b82f6','#8b5cf6','#a855f7','#14b8a6'],'aurora'],
  bounce:()=>[fill7('#00d0ff'),'bounce hue'],
  twins:()=>[['#ff3080','#ff3080','#ff3080',DIM,'#30ffd0','#30ffd0','#30ffd0'],'spin']};

function ringLook(){
  if(apMode)return[fill7('#0040ff'),'breathe'];
  const L=st.led;if(!L||L.mode==='off')return[fill7(DIM),''];
  if(st.trouble)return[fill7('#ff5000'),'spin'];
  if(st.playing&&st.loading)return[fill7('#0040ff'),'spin'];
  if(st.playing&&L.dark)return[fill7(DIM),''];
  return(LOOKS[L.mode==='auto'?L.now:L.mode]||LOOKS.color)(L.color);
}

let heroName='';
function setHeroName(t){
  if(t===heroName)return;heroName=t;
  const e=$('hName');e.textContent=t;e.classList.remove('marq');
  const over=e.getComputedTextLength()-100;  // przewijanie jak w radiu samochodowym
  if(over>0){e.style.setProperty('--dx',-(over+4)+'px');e.classList.add('marq')}
}

function renderHero(){
  const cur=stations[st.station],n=stations.length,frac=n>1&&!apMode&&st.station>=0?st.station/(n-1):0;
  $('hero').classList.toggle('playing',!apMode&&!!st.playing&&!st.loading);
  $('hero').classList.toggle('air',apMode||!!st.playing);
  $('hNum').textContent=!apMode&&cur?String(st.station+1).padStart(2,'0'):'--';
  setHeroName(apMode?'USTAW WI-FI':cur?cur.name:'RADIO');
  $('hState').textContent=apMode?(wifi.hotspot||'HOTSPOT').toUpperCase():!st.playing?'■ STOP':st.trouble?'… SŁABY STRUMIEŃ':st.loading?'ŁĄCZENIE…':`▶ GRA · VOL ${st.volume}`;
  $('needle').setAttribute('x',255+frac*134);
  $('knobSt').setAttribute('transform',`rotate(${-135+270*frac} 266 135)`);
  $('knobVol').setAttribute('transform',`rotate(${-135+270*(apMode?0:(st.volume||0)/21)} 380 135)`);
  const[cols,anim]=ringLook();
  document.querySelectorAll('#ring .led').forEach((c,i)=>c.setAttribute('fill',cols[i]));
  $('ring').setAttribute('class',anim.replace('hue','').trim());
  $('ringHue').setAttribute('class',anim.includes('hue')?'hue':'');
  $('ring').style.opacity=st.led&&!apMode?.35+.65*st.led.brightness/255:1;
}

function showTab(){
  let id=apMode?'wifi':(location.hash||'#radio').slice(1);
  if(!document.querySelector(`section[data-tab="${id}"]`))id='radio';
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.dataset.tab===id));
  document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('on',a.hash==='#'+id));
  window.scrollTo(0,0);
  if(id==='wifi'&&!apMode)wifiInfo();
  if(id==='stan')diag();
}
window.onhashchange=showTab;showTab();

// Na wąskim ekranie rysunek bez księżyca i części gwiazd, żeby radio było większe.
const narrow=matchMedia('(max-width:480px)');
const fitHero=()=>$('heroSvg').setAttribute('viewBox',narrow.matches?'128 0 352 172':'0 0 560 172');
narrow.onchange=fitHero;fitHero();

async function load(){stations=await (await fetch('/api/stations')).json();render()}
async function status(){try{st=await (await fetch('/api/status')).json();render()}catch(e){}}
const play=i=>post('/api/play',{i}).then(status);
const signal=r=>r>-55?4:r>-67?3:r>-75?2:1;
const bars=r=>'<svg class="bars" viewBox="0 0 16 14" width="16" height="14">'+[0,1,2,3].map(i=>`<rect x="${i*4}" y="${9-i*3}" width="3" height="${5+i*3}" rx="1" opacity="${i<signal(r)?1:.25}"/>`).join('')+'</svg>';
const LOCK='<svg class="lock" viewBox="0 0 12 14" width="11" height="13"><rect x="1.5" y="6" width="9" height="7" rx="1.5"/><path d="M3.5 6V4a2.5 2.5 0 0 1 5 0v2"/></svg>';
function tableRows(t,rows){$(t).innerHTML='';rows.forEach(([k,v,html])=>{const tr=document.createElement('tr');const a=document.createElement('td');a.textContent=k;const b=document.createElement('td');b.textContent=v;if(html)b.insertAdjacentHTML('afterbegin',html+' ');tr.append(a,b);$(t).append(tr)})}

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
    const now=MODES.find(x=>x[0]===st.led.now);
    $('modeDesc').textContent=(md?md[2]:'')+(st.led.mode==='auto'&&now?` Teraz: ${now[1]}.`:'');
    $('autoRow').style.display=st.led.mode==='auto'?'':'none';
    if(document.activeElement!==$('ledAuto')){$('ledAuto').value=st.led.auto;$('ledAutov').textContent=st.led.auto+' s'}
    if(document.activeElement!==$('ledColor'))$('ledColor').value=st.led.color;
    if(document.activeElement!==$('ledBright'))$('ledBright').value=st.led.brightness;
    $('ledDark').checked=!!st.led.dark;
  }

  const rssi=st.rssi!==undefined?`${st.rssi} dBm`+['',' (słaby)',' (średni)',' (dobry)',' (bardzo dobry)'][signal(st.rssi)]:'—';
  tableRows('wifiNow',[['Sieć',st.ssid||'—'],['Sygnał',rssi,st.rssi!==undefined?bars(st.rssi):''],['IP',st.ip||'—'],['Adres',st.host?`http://${st.host}`:'—']]);
  renderHero();
}

// ---------- Wi-Fi ----------

async function wifiInfo(){try{wifi=await (await fetch('/api/wifi',{cache:'no-store'})).json();renderWifi()}catch(e){}}

const savedNames=()=>wifi.saved.map(s=>s.ssid);
let ipTouched=false;

function renderWifi(){
  const ul=$('saved');ul.innerHTML='';
  if(!wifi.saved.length)ul.innerHTML='<li class="muted">Brak zapamiętanych sieci.</li>';
  wifi.saved.forEach(({ssid:s,ip},i)=>{
    const li=document.createElement('li');
    const n=document.createElement('div');n.className='url';n.textContent=s;n.style.cursor='pointer';n.onclick=()=>pick(s);
    const sm=document.createElement('small');sm.textContent=ip?`stały adres ${ip.split(' ')[0]}`:'adres z DHCP';n.append(sm);
    const b=document.createElement('span');b.className='badge'+(s===wifi.connected?' on':'');
    b.textContent=s===wifi.connected?'połączona':i===0?'główna':'zapasowa';
    li.append(n);
    if(ip&&s!==wifi.connected){  // np. w hotspocie po nieudanej zmianie adresu
      const r=document.createElement('button');r.className='small';r.textContent='DHCP';r.title='Przywróć automatyczny adres';
      r.onclick=()=>post('/api/wifi/ip',{ssid:s,mode:'dhcp'}).then(wifiInfo);li.append(r);
    }
    li.append(b);
    if(s!==wifi.connected){
      const d=document.createElement('button');d.className='del';d.textContent='✕';d.title='Zapomnij';
      d.onclick=()=>{if(confirm(`Zapomnieć sieć „${s}”?`))post('/api/wifi/forget',{ssid:s}).then(wifiInfo)};
      li.append(d);
    }
    ul.append(li);
  });
  renderIp();
  $('wifiWarn').hidden=!wifi.failed;
  $('wifiWarn').textContent=`Nie udało się połączyć z „${wifi.failed}”, więc radio wróciło do „${wifi.connected}”. Sprawdź nazwę sieci i hasło.`;
  const list=savedNames().map(s=>`„${s}”`).join(' ani ');
  $('apReason').textContent={
    button:'Tryb konfiguracji włączyło przytrzymanie BOOT. Zapamiętane sieci zostały: możesz dodać nową albo wrócić bez zmian (to samo robi ponowne przytrzymanie BOOT).',
    failed:`Radio nie połączyło się z ${list}. Sprawdź router albo wybierz inną sieć.`,
    none:'Wybierz sieć Wi-Fi, z którą radio ma się łączyć.'}[wifi.reason]+
    (wifi.saved.length?' Gdy przez 5 minut nikt nie będzie połączony z hotspotem, radio samo wróci do zapamiętanych sieci.':'');
  $('apCancel').hidden=!wifi.saved.length;
  passHint();
}

function passHint(){
  const s=$('ssidIn').value,h=$('passHint');
  h.textContent=openNets.has(s)?'Sieć otwarta, bez hasła.':savedNames().includes(s)?'Zostaw hasło puste, żeby zachować zapamiętane.':'';
  h.hidden=!h.textContent;
}
function pick(ssid){$('ssidIn').value=ssid;$('passIn').value='';passHint();
  document.querySelectorAll('#nets li').forEach(li=>li.classList.toggle('on',li.dataset.ssid===ssid));$('passIn').focus()}

async function scan(){
  $('scan').disabled=true;$('scanMsg').textContent='Szukam sieci…';
  try{
    for(let k=0;k<25;k++){
      const r=await (await fetch('/api/scan',{cache:'no-store'})).json();
      if(Array.isArray(r)){showNets(r);break}
      await new Promise(f=>setTimeout(f,700));
    }
  }catch(e){}
  $('scan').disabled=false;$('scanMsg').textContent='';
}

function showNets(list){
  const best={};list.forEach(n=>{if(n.ssid&&(!best[n.ssid]||best[n.ssid].rssi<n.rssi))best[n.ssid]=n});
  const nets=Object.values(best).sort((a,b)=>b.rssi-a.rssi);
  openNets=new Set(nets.filter(n=>!n.lock).map(n=>n.ssid));
  const ul=$('nets');ul.innerHTML='';
  if(!nets.length)ul.innerHTML='<li class="muted">Nie znaleziono sieci.</li>';
  nets.forEach(n=>{
    const li=document.createElement('li');li.dataset.ssid=n.ssid;li.innerHTML=bars(n.rssi);
    const s=document.createElement('span');s.className='name';s.textContent=n.ssid;
    const m=document.createElement('span');m.className='muted';m.textContent=`${n.rssi} dBm`;
    if(n.lock)m.insertAdjacentHTML('afterbegin',LOCK+' ');
    li.append(s,m);
    if(savedNames().includes(n.ssid)){const b=document.createElement('span');b.className='badge';b.textContent='zapamiętana';li.insertBefore(b,m)}
    li.onclick=()=>pick(n.ssid);ul.append(li);
  });
  passHint();
}

// reload: po restarcie radia czekaj, aż odpowie, i otwórz stronę (pod nowym adresem, jeśli strona
// była otwarta po numerze IP, który się zmienia).
function wifiDone(msg,reload,newIp){
  $('doneMsg').textContent=msg;$('wifiDone').hidden=false;$('wifiDone').scrollIntoView({behavior:'smooth'});
  if(!reload)return;
  const byIp=/^[0-9.]+$/.test(location.hostname),base=byIp&&newIp?`http://${newIp}`:'';
  setTimeout(()=>{const t=setInterval(async()=>{try{
    await fetch(base+'/api/wifi',{cache:'no-store',mode:base?'no-cors':'cors'});
    clearInterval(t);if(base)location.href=base+'/#wifi';else location.reload();
  }catch(e){}},2000)},6000);
}

function renderIp(){
  const cur=wifi.saved.find(s=>s.ssid===wifi.connected);
  if(!cur)return;
  if(!ipTouched){
    const[ip,mask,gw,dns]=cur.ip?cur.ip.split(' '):[wifi.ip,wifi.mask,wifi.gw,wifi.dns];
    $('ipAddr').value=ip||'';$('ipMask').value=mask||'';$('ipGw').value=gw||'';$('ipDns').value=dns||'';
    document.querySelector(`input[name=ipmode][value=${cur.ip?'static':'dhcp'}]`).checked=true;
  }
  const st=document.querySelector('input[name=ipmode]:checked').value==='static';
  $('ipFields').style.display=st?'':'none';
  $('ipNote').textContent=`Dotyczy sieci „${cur.ssid}”. `+(st?'Adres musi być w tej samej sieci co brama i poza pulą DHCP routera (albo zarezerwuj go w routerze). ':
    (wifi.static?'':'Pola podpowiadają adres przydzielony teraz przez router. '))+
    'Gdy po zmianie strona się nie otworzy, przytrzymaj BOOT: w trybie konfiguracji przywrócisz DHCP przyciskiem przy sieci.';
}
document.querySelectorAll('input[name=ipmode]').forEach(r=>r.onchange=()=>{ipTouched=true;renderIp()});
['ipAddr','ipMask','ipGw','ipDns'].forEach(id=>$(id).oninput=()=>ipTouched=true);
$('ipSave').onclick=async()=>{
  const st=document.querySelector('input[name=ipmode]:checked').value==='static';
  const d={ssid:wifi.connected,mode:st?'static':'dhcp',ip:$('ipAddr').value.trim(),mask:$('ipMask').value.trim(),gw:$('ipGw').value.trim(),dns:$('ipDns').value.trim()};
  const r=await (await post('/api/wifi/ip',d)).json();
  if(r.error){alert('Nie zapisano: '+r.error);return}
  wifiDone(st?`Radio łączy się ponownie z adresem ${d.ip}. Strona otworzy się sama, gdy radio wróci (ok. 20 s); radio.local też zadziała.`:
    'Radio łączy się ponownie i pobierze adres z routera. Strona odświeży się sama; jeśli była otwarta po numerze IP, użyj http://radio.local.',true,st?d.ip:'');
};

$('scan').onclick=scan;
$('ssidIn').oninput=passHint;
$('showPw').onclick=()=>{const p=$('passIn'),show=p.type==='password';p.type=show?'text':'password';$('showPw').textContent=show?'Ukryj':'Pokaż'};
$('wifiForm').onsubmit=async e=>{
  e.preventDefault();
  const ssid=$('ssidIn').value,pass=$('passIn').value,d={ssid,pass};
  if(!pass&&!openNets.has(ssid)){
    if(savedNames().includes(ssid))d.keep=1;
    else if(!confirm(`Połączyć z siecią „${ssid}” bez hasła?`))return;
  }
  const r=await (await post('/api/wifi',d)).json();
  if(r.error){alert('Nie zapisano: '+r.error);return}
  if(apMode)wifiDone(`Radio łączy się z siecią „${ssid}”. Połącz telefon z tą samą siecią i otwórz http://radio.local.`);
  else wifiDone(`Radio uruchamia się ponownie i łączy z siecią „${ssid}”. Strona odświeży się sama, gdy radio wróci (do ok. 40 s). Jeśli telefon jest w innej sieci, przełącz go i otwórz http://radio.local.`,true);
};
$('apCancel').onclick=()=>post('/api/wifi/cancel').then(()=>wifiDone('Radio wraca do zapamiętanych sieci. Połącz telefon z domową siecią i za chwilę otwórz http://radio.local.'));

// ---------- stan ----------

const stanOpen=()=>!document.hidden&&document.querySelector('section[data-tab="stan"]').classList.contains('on');
const peaks={};
function meter(id,[rms,pk]){
  const pct=db=>Math.max(0,Math.min(100,(db+60)/60*100));
  peaks[id]=Math.max(pk,(peaks[id]??-120)-1.5);  // szczyt opada 1,5 dB na odczyt
  $(id).querySelector('i').style.clipPath=`inset(0 ${100-pct(rms)}% 0 0)`;
  $(id).querySelector('b').style.left=`calc(${pct(peaks[id])}% - 2px)`;
  $(id+'V').textContent=rms<=-119?'cisza':`${rms.toFixed(0)} / ${pk.toFixed(0)}`;
}
async function levels(){
  if(stanOpen()){try{const l=await (await fetch('/api/levels',{cache:'no-store'})).json();
    meter('lvOut',l.out);meter('lvMic0',l.mic[0]);meter('lvMic1',l.mic[1])}catch(e){}}
  setTimeout(levels,stanOpen()?150:1000);
}

const kb=n=>n>=1048576?(n/1048576).toFixed(1).replace('.',',')+' MB':Math.round(n/1024)+' KB';
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;
  return(d?d+' d ':'')+(d||h?h+' h ':'')+m+' min'+(d||h?'':` ${s%60} s`)};
async function diag(){
  if(!stanOpen())return;
  let d;try{d=await (await fetch('/api/diag',{cache:'no-store'})).json()}catch(e){return}
  const S=d.stream,W=d.wifi,Y=d.sys,cur=stations[st.station];
  const on=S.playing&&S.codec&&S.codec!=='NONE';
  tableRows('dStream',[
    ['Stacja',(cur?`${st.station+1}. ${cur.name}`:'—')+(S.name&&cur&&S.name!==cur.name?` („${S.name}”)`:'')],
    ...(S.genre?[['Gatunek',S.genre]]:[]),...(S.desc?[['Opis',S.desc]]:[]),
    ['Format',on?`${S.codec}${S.bitrate?`, ${Math.round(S.bitrate/1000)} kb/s`:''}`:'—'],
    ['Dźwięk',on&&S.rate?`${(S.rate/1000).toString().replace('.',',')} kHz, ${S.channels===1?'mono':'stereo'}, ${S.bits} bit`:'—'],
    ['Gra od',S.since>=0?dur(S.since):'—'],['Zerwania',String(S.losses)],
    ['Adres',S.url||'—'],...(S.web?[['Strona',S.web]]:[])]);
  const sec=S.bitrate?S.buf/(S.bitrate/8):0;
  $('dBuf').querySelector('i').style.clipPath=`inset(0 ${100-(S.bufSize?S.buf/S.bufSize*100:0)}% 0 0)`;
  $('dBufV').textContent=S.bufSize?`${kb(S.buf)}`+(on&&sec?`, ${sec.toFixed(0)} s`:''):'—';
  const q=['',' (słaby)',' (średni)',' (dobry)',' (bardzo dobry)'][signal(W.rssi)];
  tableRows('dWifi',[['Sieć',W.ssid||'—'],['Sygnał',`${W.rssi} dBm${q}`,bars(W.rssi)],['Kanał',String(W.channel)],['Punkt dostępu',W.bssid],
    ['IP',`${W.ip} (${W.static?'stały':'DHCP'})`],['Maska',W.mask],['Brama',W.gw],['DNS',W.dns],['MAC',W.mac],['Adres',`http://${W.host}`]]);
  tableRows('dSys',[['Czas pracy',dur(Y.uptime)],['Ostatni restart',Y.reset],['Temperatura układu',`${Y.temp.toFixed(0)} °C`],
    ['RAM wolny',`${kb(Y.heap)} (najmniej ${kb(Y.heapMin)})`],['PSRAM wolny',`${kb(Y.psram)} z ${kb(Y.psramSize)}`],
    ['FFat',Y.fs>=0?`${kb(Y.fs)} z ${kb(Y.fsSize)}`:'niezamontowany'],['Bateria',st.battery>=0?`${st.battery}%`:'pomiar niepodłączony'],
    ['Procesor',`ESP32-S3, ${Y.cpu} MHz, flash ${kb(Y.flash)}`],['Oprogramowanie',`Arduino ${Y.core}, ESP-IDF ${Y.sdk}`],['Zbudowano',Y.build]]);
}

// ---------- sterowanie ----------

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
$('ledAuto').oninput=e=>$('ledAutov').textContent=e.target.value+' s';
$('ledAuto').onchange=e=>post('/api/led',{auto:e.target.value}).then(status);
$('ledDark').onchange=e=>post('/api/led',{dark:e.target.checked?1:0}).then(status);

(async()=>{
  await wifiInfo();
  apMode=!!wifi.ap;
  if(apMode){document.body.classList.add('ap');showTab();renderHero();scan()}
  else{load().then(status);setInterval(status,3000);setInterval(diag,2000);levels()}
})();
</script></body></html>)HTML";
