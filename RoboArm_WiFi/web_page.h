#pragma once
#include <pgmspace.h>

// Slider page lives on the ESP32. No internet needed.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<meta name="theme-color" content="#1b1f24">
<title>RoboArm</title>
<style>
  :root{--bg:#1b1f24;--card:#262b32;--txt:#eef1f4;--mut:#9aa4af;--acc:#f0a23b;--ok:#3ecf6e;--bad:#ef5350;}
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
  body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
       -webkit-user-select:none;user-select:none;}
  header{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;position:sticky;top:0;
         background:var(--bg);z-index:2;border-bottom:1px solid #333a42;}
  h1{font-size:18px;margin:0;letter-spacing:.5px;}
  #status{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--mut);}
  #dot{width:10px;height:10px;border-radius:50%;background:var(--bad);}
  #dot.on{background:var(--ok);}
  .hint{margin:0 16px 8px;font-size:12px;color:var(--mut);max-width:520px;}
  main{padding:8px 16px 120px;max-width:520px;margin:0 auto;}
  .card{background:var(--card);border-radius:14px;padding:14px;margin-bottom:12px;}
  .top{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px;}
  .name{font-weight:600;font-size:16px;}
  .ang{font-variant-numeric:tabular-nums;font-size:22px;font-weight:700;color:var(--acc);}
  .ang small{font-size:13px;color:var(--mut);font-weight:400;}
  .row{display:flex;align-items:center;gap:10px;}
  .hold{flex:0 0 58px;height:58px;border-radius:12px;border:none;background:#39414b;color:var(--txt);
        font-size:30px;font-weight:700;touch-action:none;}
  .hold.active{background:var(--acc);color:#1b1f24;}
  input[type=range]{flex:1;height:44px;margin:0;accent-color:var(--acc);}
  .lim{display:flex;justify-content:space-between;font-size:11px;color:var(--mut);margin-top:4px;padding:0 68px;}
  footer{position:fixed;left:0;right:0;bottom:0;display:flex;gap:12px;padding:12px 16px calc(12px + env(safe-area-inset-bottom));
         background:var(--bg);border-top:1px solid #333a42;}
  footer button{flex:1;height:56px;border:none;border-radius:14px;font-size:18px;font-weight:700;}
  #home{background:#3a6ff0;color:#fff;}
  #stop{background:var(--bad);color:#fff;}
  button:active{filter:brightness(1.2);}
</style>
</head>
<body>
<header>
  <h1>RoboArm sliders</h1>
  <div id="status"><span id="dot"></span><span id="stxt">Connecting...</span></div>
</header>
<p class="hint">If sliders do nothing: turn OFF mobile data, stay on WiFi <b>RoboArm</b>, then refresh.</p>
<main id="joints"></main>
<footer>
  <button id="home">HOME</button>
  <button id="stop">STOP</button>
</footer>
<script>
const NAMES = ['Base','Shoulder','Elbow','Gripper'];
let mins = [5,10,8,10], maxs = [145,80,82,50];
let sliders = [], labels = [];
let dragUntil = [0,0,0,0];
let lastSlide = [0,0,0,0];
let holdTimers = [null,null,null,null];

function send(m){
  fetch('/c?m=' + encodeURIComponent(m)).catch(function(){});
}
function setStatus(on, txt){
  document.getElementById('dot').className = on ? 'on' : '';
  document.getElementById('stxt').textContent = txt;
}
function apply(t){
  const p = t.split(',');
  if (p[0] === 'L' && p.length >= 9){
    for (let j=0;j<4;j++){
      mins[j] = +p[1+j]; maxs[j] = +p[5+j];
      sliders[j].min = mins[j]; sliders[j].max = maxs[j];
      document.getElementById('mn'+j).innerHTML = mins[j]+'&deg;';
      document.getElementById('mx'+j).innerHTML = maxs[j]+'&deg;';
    }
  } else if (p[0] === 'A' && p.length >= 5){
    const now = Date.now();
    for (let j=0;j<4;j++){
      if (now < dragUntil[j]) continue;
      sliders[j].value = p[1+j];
      labels[j].textContent = p[1+j];
    }
  }
}

const box = document.getElementById('joints');
NAMES.forEach((n,j)=>{
  const c = document.createElement('div');
  c.className = 'card';
  c.innerHTML =
    '<div class="top"><span class="name">'+n+'</span><span class="ang"><span id="a'+j+'">--</span><small>&deg;</small></span></div>'+
    '<div class="row"><button class="hold" data-j="'+j+'" data-d="-1">&minus;</button>'+
    '<input type="range" id="s'+j+'" min="'+mins[j]+'" max="'+maxs[j]+'" step="1">'+
    '<button class="hold" data-j="'+j+'" data-d="1">+</button></div>'+
    '<div class="lim"><span id="mn'+j+'">'+mins[j]+'&deg;</span><span id="mx'+j+'">'+maxs[j]+'&deg;</span></div>';
  box.appendChild(c);
  sliders[j] = c.querySelector('#s'+j);
  labels[j]  = c.querySelector('#a'+j);
  sliders[j].addEventListener('input', ()=>{
    dragUntil[j] = Date.now() + 600;
    labels[j].textContent = sliders[j].value;
    const now = Date.now();
    if (now - lastSlide[j] > 50){ lastSlide[j] = now; send('S,'+j+','+sliders[j].value); }
  });
  sliders[j].addEventListener('change', ()=>{
    dragUntil[j] = Date.now() + 600;
    send('S,'+j+','+sliders[j].value);
  });
});

function startHold(btn){
  const j = +btn.dataset.j, d = +btn.dataset.d;
  stopHold(j);
  btn.classList.add('active');
  const t0 = Date.now();
  const tick = ()=>{
    const k = Math.min(1, 0.35 + (Date.now()-t0)/1500);
    send('V,'+j+','+(d*k).toFixed(2));
  };
  tick();
  holdTimers[j] = setInterval(tick, 100);
}
function stopHold(j){
  if (holdTimers[j]){ clearInterval(holdTimers[j]); holdTimers[j] = null; send('V,'+j+',0'); }
  document.querySelectorAll('.hold[data-j="'+j+'"]').forEach(b=>b.classList.remove('active'));
}
document.querySelectorAll('.hold').forEach(b=>{
  b.addEventListener('pointerdown', e=>{ e.preventDefault(); b.setPointerCapture(e.pointerId); startHold(b); });
  ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>
    b.addEventListener(ev, ()=>stopHold(+b.dataset.j)));
  b.addEventListener('contextmenu', e=>e.preventDefault());
});
document.getElementById('home').onclick = ()=>{ for(let j=0;j<4;j++) stopHold(j); send('H'); };
document.getElementById('stop').onclick = ()=>{ for(let j=0;j<4;j++) stopHold(j); send('X'); };
document.addEventListener('visibilitychange', ()=>{ if (document.hidden) for(let j=0;j<4;j++) stopHold(j); });

function poll(){
  fetch('/s').then(r=>r.text()).then(t=>{ setStatus(true,'Connected'); apply(t); })
    .catch(()=> setStatus(false,'Waiting for arm...'));
}
fetch('/l').then(r=>r.text()).then(apply).catch(function(){});
poll();
setInterval(poll, 150);
</script>
</body>
</html>
)rawliteral";
