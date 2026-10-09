#pragma once
// The web page: live video (/stream) and one LED button (/led).
static const char PAGE[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AiPi-Cam-D200</title>
<style>
body{margin:0;background:#111;color:#eee;font-family:system-ui,sans-serif;text-align:center}
h1{font-size:18px;font-weight:600;margin:14px}
img{width:100%;max-width:800px;aspect-ratio:4/3;background:#000;border-radius:10px}
button{margin:16px;padding:14px 34px;font-size:18px;border:0;border-radius:12px;background:#333;color:#fff}
button.on{background:#f5b301;color:#111}
</style></head><body>
<h1>AiPi-Cam-D200</h1>
<img src="/stream" alt="camera">
<div><button id="led">LED: OFF</button></div>
<script>
const b=document.getElementById('led');
function show(on){b.classList.toggle('on',on);b.textContent='LED: '+(on?'ON':'OFF');}
b.onclick=async()=>{const on=!b.classList.contains('on');
 try{show((await (await fetch('/led?on='+(on?1:0))).text()).trim()==='1');}catch(e){}};
fetch('/led').then(r=>r.text()).then(t=>show(t.trim()==='1')).catch(()=>{});
</script></body></html>)HTML";
