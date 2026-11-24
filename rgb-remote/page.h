// Pages the board serves itself: control from any browser, and Wi-Fi setup.
#pragma once
static const char PAGE_SETUP[] = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>RGB Remote</title>
<style>body{font:17px system-ui;max-width:26em;margin:2em auto;padding:0 16px}input,select,button{font:inherit;width:100%;padding:.6em;margin:.3em 0 1em;box-sizing:border-box}</style></head><body>
<h2>Wi-Fi</h2><label>Network<select id=s></select></label><label>Password<input id=p type=password autocomplete=off></label><button onclick=go()>Connect</button><p id=m></p>
<script>fetch('/api/scan').then(r=>r.json()).then(a=>{for(const n of a){const o=document.createElement('option');o.textContent=n;s.append(o)}});
function go(){m.textContent='...';fetch('/api/wifi',{method:'POST',body:new URLSearchParams({ssid:s.value,pass:p.value})}).then(r=>r.text()).then(t=>m.textContent=t)}</script></body></html>)HTML";

static const char PAGE_HOME[] = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>RGB Remote</title>
<style>body{font:17px system-ui;max-width:30em;margin:1.5em auto;padding:0 16px;background:#14161a;color:#eee}button{font:inherit;padding:.55em .8em;margin:.2em;border:0;border-radius:10px;background:#2b3038;color:#eee}#c button{width:2.6em;height:2.6em}input{width:100%}</style></head><body>
<h2 id=n>RGB Remote</h2><p><button onclick="q('on=1')">On</button><button onclick="q('on=0')">Off</button></p><div id=c></div>
<p>Brightness<input type=range min=0 max=15 id=b onchange="q('bright='+this.value)"></p><div id=e></div>
<p>Speed<input type=range min=1 max=100 id=s onchange="q('speed='+this.value)"></p>
<p><button onclick="q('mode=-')">Mode -</button><button onclick="q('mode=%2B')">Mode +</button><button onclick="q('spd=-')">Speed -</button><button onclick="q('spd=%2B')">Speed +</button></p>
<script>const q=x=>fetch('/api/set',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:x}).then(r=>r.json()).then(show);
function show(t){n.textContent=t.name;if(t.bright>=0)b.value=t.bright;s.value=t.speed}
fetch('/api/info').then(r=>r.json()).then(i=>{i.colors.forEach((k,j)=>{const x=document.createElement('button');x.style.background=`rgb(${k[1]*2.55},${k[2]*2.55},${k[3]*2.55})`;x.onclick=()=>q('color='+j);c.append(x)});
i.effects.forEach(f=>{const x=document.createElement('button');x.textContent=f;x.onclick=()=>q('effect='+f);e.append(x)})});
fetch('/api/state').then(r=>r.json()).then(show)</script></body></html>)HTML";
