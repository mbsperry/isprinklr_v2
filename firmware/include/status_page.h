// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The device's fallback UI at GET /: status, the current run, and the RAM log,
// refreshed every 5 s, with Skip and Cancel for a run in progress. For when
// the server is down; the real frontend lives on the server.
//
// Compiled in rather than served from LittleFS, as the migration plan first
// said: one page does not justify a filesystem image that would need its own
// upload path, and this way it can never be out of step with the firmware's
// API. It talks only to this device's own endpoints.
namespace status_page {

constexpr char kHtml[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>iSprinklr device</title>
<style>
:root{--bg:#fff;--fg:#1d1d1f;--mute:#6e6e73;--line:#d2d2d7;--warn:#a05a00;--err:#c62828;--ok:#2e7d32}
@media (prefers-color-scheme:dark){:root{--bg:#161618;--fg:#f2f2f7;--mute:#98989d;--line:#3a3a3c;--warn:#ffb340;--err:#ff6b6b;--ok:#66bb6a}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:860px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:24px 0 8px}
.mute{color:var(--mute)}table{border-collapse:collapse;width:100%}
td,th{text-align:left;padding:4px 8px 4px 0;border-bottom:1px solid var(--line);vertical-align:top}
th{font-weight:600}.kv td:first-child{color:var(--mute);width:40%}
.error{color:var(--err)}.warn{color:var(--warn)}.completed{color:var(--ok)}
.log{font:13px/1.4 ui-monospace,monospace;overflow-wrap:anywhere}
button{font:inherit;padding:6px 14px;margin-right:8px;border-radius:6px;border:1px solid var(--line);background:transparent;color:var(--fg);cursor:pointer}
select{font:inherit}
</style></head><body><main>
<h1>iSprinklr device</h1>
<div class="mute" id="sub">loading...</div>
<h2>Status</h2><table class="kv" id="status"></table>
<h2>Run</h2>
<div id="runhead" class="mute"></div>
<div id="controls" style="margin:8px 0;display:none">
<button onclick="post('/api/run/skip')">Skip zone</button>
<button onclick="post('/api/zone/stop','{}')">Cancel run</button></div>
<table id="run"></table>
<h2>Log <select id="level" onchange="logs()">
<option value="debug">all</option><option value="info" selected>info</option>
<option value="warn">warn</option><option value="error">error</option></select></h2>
<table class="log" id="log"></table>
</main><script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const row=(...c)=>'<tr>'+c.map(x=>'<td>'+x+'</td>').join('')+'</tr>';
function dur(s){s=Math.round(s);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+'d '+h+'h':h?h+'h '+m+'m':m?m+'m '+s%60+'s':s+'s'}
async function post(url,body){
 try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:body||'{}'});
 const j=await r.json();if(!r.ok)alert(j.message||r.status)}catch(e){alert(e)}
 refresh()}
async function status(){
 const s=await (await fetch('/api/status')).json();
 $('sub').textContent=s.firmware_version+' · up '+dur(s.uptime_ms/1000)+' · '+(s.time_valid?s.time.local:'clock not set');
 const st=s.settings||{};
 $('status').innerHTML=[
  ['Scheduler',st.enabled?'on':'off'],
  ['Active schedule',esc(st.active_schedule??'none')+' at '+esc(st.start_time)],
  ['Today',st.today?st.today.map(t=>'zone '+t.zone+' '+t.duration_minutes+' min').join(', ')||'nothing':'unknown'],
  ['Last trigger decision',esc(s.trigger&&s.trigger.last_decision)],
  ['Recovery',esc(s.recovery&&s.recovery.result)],
  ['Network',esc(s.network.transport+' '+s.network.ip)],
  ['Log export',s.logs.sink?esc(s.logs.sink)+' ('+s.logs.sent+' sent, '+s.logs.send_errors+' errors, '+s.logs.lost+' lost)':'off'],
  ['Reset reason',esc(s.reset_reason)],
  ['Free heap',s.memory.free_heap+' bytes']].map(r=>row(r[0],r[1])).join('');
 const run=s.run||{},busy=run.state&&run.state!=='idle';
 $('controls').style.display=busy?'block':'none';
 $('runhead').textContent=(busy?run.state:'idle')+(run.schedule?' · '+run.schedule:'')+(run.remaining_seconds?' · '+dur(run.remaining_seconds)+' left':'');
 $('run').innerHTML=(run.entries||[]).length?'<tr><th>Zone</th><th>Minutes</th><th>Origin</th><th>Outcome</th></tr>'+run.entries.map(e=>
  row(e.zone,e.duration_minutes,e.origin,'<span class="'+e.outcome+'">'+e.outcome+'</span>')).join(''):'';
}
async function logs(){
 const j=await (await fetch('/api/logs?level='+$('level').value)).json();
 $('log').innerHTML=j.entries.slice().reverse().map(e=>
  row(esc(e.time?new Date(e.time).toLocaleTimeString():dur(e.uptime_ms/1000)),
  '<span class="'+e.level+'">'+e.level+'</span>',esc(e.module),'<span class="'+e.level+'">'+esc(e.message)+'</span>')).join('');
}
async function refresh(){try{await status();await logs()}catch(e){$('sub').textContent='device not answering: '+e}}
refresh();setInterval(refresh,5000);
</script></body></html>)html";

}  // namespace status_page
