#include "fluxgate/admin_server.h"

#include <simdjson.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fluxgate {
namespace {

using asio::ip::tcp;

bool check_bearer_token(const std::string& request, const std::string& token) {
    std::size_t pos = 0;
    while (pos < request.size()) {
        const auto eol = request.find('\n', pos);
        const auto end = (eol == std::string::npos) ? request.size() : eol;
        std::string line = request.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (name == "authorization") {
                const auto vs = line.find_first_not_of(' ', colon + 1);
                if (vs != std::string::npos && line.substr(vs) == "Bearer " + token)
                    return true;
            }
        }
        pos = end + 1;
    }
    return false;
}

std::size_t content_length_of(const std::string& request) {
    std::size_t pos = 0;
    while (pos < request.size()) {
        const auto eol = request.find('\n', pos);
        const auto end = (eol == std::string::npos) ? request.size() : eol;
        std::string line = request.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (name == "content-length") {
                try { return std::stoull(line.substr(colon + 1)); } catch (...) { return 0; }
            }
        }
        pos = end + 1;
    }
    return 0;
}

// ── HTML dashboard ────────────────────────────────────────────────────────────
const char DASHBOARD[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluxGate Control Panel</title>
<style>
:root{--bg:#070a11;--s1:#0e1220;--s2:#141a2c;--s3:#1a2236;--border:#1e2440;--purple:#7c3aed;--pl:#a78bfa;--green:#4ade80;--yellow:#fbbf24;--cyan:#22d3ee;--red:#f87171;--text:#e2e8f0;--muted:#64748b}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding:20px 26px 60px;position:relative;overflow-x:hidden}
.glow{position:fixed;top:-280px;left:50%;transform:translateX(-50%);width:1100px;height:620px;background:radial-gradient(ellipse at center,rgba(124,58,237,.16),transparent 62%);filter:blur(60px);pointer-events:none;z-index:0;animation:drift 16s ease-in-out infinite}
@keyframes drift{0%,100%{transform:translateX(-50%) translateY(0)}50%{transform:translateX(-50%) translateY(26px)}}
.hdr,.tabs,.view,.banner{position:relative;z-index:1}
.hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px;gap:14px;flex-wrap:wrap}
.logo{font-size:1.3rem;font-weight:800;color:#fff;display:flex;align-items:center}
.logo b{color:var(--pl);margin:0 6px 0 2px}
.logo span{color:var(--muted);font-weight:400;font-size:.82rem;margin-left:10px;padding-left:10px;border-left:1px solid var(--border)}
.dot{width:9px;height:9px;border-radius:50%;background:var(--green);display:inline-block;margin-right:8px;box-shadow:0 0 8px var(--green);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1;box-shadow:0 0 0 0 rgba(74,222,128,.5)}50%{opacity:.5;box-shadow:0 0 0 5px rgba(74,222,128,0)}}
.hdr-right{display:flex;align-items:center;gap:10px}
.ts{font-size:.74rem;color:var(--muted)}
.tok-in{background:var(--s1);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:6px 10px;font-size:.78rem;width:140px;font-family:ui-monospace,monospace}
.tok-in::placeholder{color:var(--muted)}
.banner{background:linear-gradient(90deg,rgba(248,113,113,.16),rgba(248,113,113,.04));border:1px solid rgba(248,113,113,.4);color:#fecaca;border-radius:12px;padding:11px 18px;margin-bottom:16px;font-size:.85rem;display:none}
.tabs{display:flex;gap:6px;margin-bottom:20px;flex-wrap:wrap;border-bottom:1px solid var(--border);padding-bottom:0}
.tab{background:transparent;border:none;border-bottom:2px solid transparent;color:var(--muted);padding:9px 16px;font-size:.9rem;cursor:pointer;font-weight:600;font-family:inherit;transition:color .2s,border-color .2s}
.tab:hover{color:var(--text)}
.tab.active{color:var(--pl);border-bottom-color:var(--purple)}
.view{display:none;animation:fade .35s ease}
.view.active{display:block}
@keyframes fade{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:none}}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin-bottom:18px}
.card{position:relative;background:var(--s1);border:1px solid var(--border);border-radius:14px;padding:16px 18px;overflow:hidden;transition:transform .2s,border-color .2s}
.card:hover{transform:translateY(-3px);border-color:var(--purple)}
.card::before{content:"";position:absolute;left:0;top:0;bottom:0;width:3px;background:var(--accent,var(--pl))}
.card.act{--accent:var(--cyan)}.card.hr{--accent:var(--green)}.card.tok{--accent:var(--pl)}.card.cost{--accent:var(--yellow)}.card.rl{--accent:var(--red)}.card.ce{--accent:var(--cyan)}
.card .ic{font-size:1.05rem;margin-bottom:8px;opacity:.9}
.card .l{font-size:.68rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin-bottom:5px}
.card .v{font-size:1.7rem;font-weight:800;font-variant-numeric:tabular-nums}
.green{color:var(--green)}.purple{color:var(--pl)}.yellow{color:var(--yellow)}.cyan{color:var(--cyan)}.red{color:var(--red)}
.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-bottom:18px}
.chart-box{background:var(--s1);border:1px solid var(--border);border-radius:14px;padding:16px 18px}
.chart-box h3{font-size:.7rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin-bottom:12px}
canvas{width:100%;height:60px;display:block}
.tbl{background:var(--s1);border:1px solid var(--border);border-radius:14px;overflow:hidden;overflow-x:auto}
table{width:100%;border-collapse:collapse;min-width:560px}
th{background:var(--s2);color:var(--muted);font-size:.68rem;text-transform:uppercase;letter-spacing:.05em;padding:10px 16px;text-align:left;font-weight:600;position:sticky;top:0}
td{padding:9px 16px;font-size:.82rem;border-top:1px solid var(--border);font-variant-numeric:tabular-nums;white-space:nowrap}
tr:hover td{background:rgba(30,36,64,.5)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.78rem}
.path{max-width:280px;overflow:hidden;text-overflow:ellipsis;color:var(--muted)}
.badge{display:inline-block;padding:2px 9px;border-radius:999px;font-size:.7rem;font-weight:700;letter-spacing:.02em}
.b-hit{background:rgba(74,222,128,.16);color:var(--green)}
.b-miss{background:rgba(251,191,36,.16);color:var(--yellow)}
.b-bypass{background:rgba(100,116,139,.18);color:var(--muted)}
.st-2{color:var(--green)}.st-4{color:var(--yellow)}.st-5{color:var(--red)}
.bar-wrap{background:var(--s2);border-radius:4px;height:6px;width:100px;overflow:hidden;display:inline-block;vertical-align:middle}
.bar-fill{height:100%;border-radius:4px;background:linear-gradient(90deg,var(--purple),var(--cyan));transition:width .4s ease}
.empty{padding:36px;text-align:center;color:var(--muted);font-size:.85rem}
.settings{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;align-items:start}
.panel{background:var(--s1);border:1px solid var(--border);border-radius:14px;padding:20px 22px}
.panel h3{font-size:.78rem;text-transform:uppercase;letter-spacing:.06em;color:var(--pl);margin-bottom:16px}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:9px 0;border-bottom:1px solid var(--border)}
.row:last-of-type{border-bottom:none}
.row label{font-size:.86rem}
.row .hint{font-size:.72rem;color:var(--muted);margin-top:2px}
input[type=number],textarea{background:var(--bg);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:7px 10px;font-size:.84rem;font-family:inherit;width:120px}
textarea{width:100%;min-height:72px;font-family:ui-monospace,monospace;font-size:.78rem;margin-top:8px;resize:vertical}
.switch{position:relative;width:42px;height:23px;flex-shrink:0}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;inset:0;background:var(--s3);border:1px solid var(--border);border-radius:23px;transition:.25s}
.slider:before{content:"";position:absolute;height:15px;width:15px;left:3px;bottom:3px;background:var(--muted);border-radius:50%;transition:.25s}
input:checked+.slider{background:rgba(124,58,237,.4);border-color:var(--purple)}
input:checked+.slider:before{transform:translateX(19px);background:var(--pl)}
.btn{background:var(--purple);color:#fff;border:none;padding:10px 20px;border-radius:9px;font-weight:600;font-size:.88rem;cursor:pointer;font-family:inherit;transition:background .2s,transform .12s}
.btn:hover{background:#6d28d9;transform:translateY(-1px)}
.btn.ghost{background:var(--s2);border:1px solid var(--border);color:var(--text)}
.btn.danger{background:rgba(248,113,113,.16);border:1px solid rgba(248,113,113,.4);color:var(--red)}
.btn.danger:hover{background:rgba(248,113,113,.28)}
.actions{display:flex;gap:10px;margin-top:18px;flex-wrap:wrap}
.toast{position:fixed;bottom:22px;left:50%;transform:translateX(-50%) translateY(80px);background:var(--s3);border:1px solid var(--purple);color:var(--text);padding:11px 22px;border-radius:10px;font-size:.85rem;z-index:50;transition:transform .3s;box-shadow:0 12px 40px -12px rgba(0,0,0,.7)}
.toast.show{transform:translateX(-50%) translateY(0)}
</style>
</head>
<body>
<div class="glow"></div>

<div class="hdr">
  <div class="logo"><span class="dot"></span>⚡<b>FluxGate</b><span>Control Panel</span></div>
  <div class="hdr-right">
    <input class="tok-in" id="tok" type="password" placeholder="admin token (if set)">
    <div class="ts" id="ts">—</div>
  </div>
</div>

<div class="banner" id="banner"></div>

<div class="tabs">
  <button class="tab active" data-view="overview">Overview</button>
  <button class="tab" data-view="requests">Requests</button>
  <button class="tab" data-view="providers">Providers</button>
  <button class="tab" data-view="settings">Settings</button>
</div>

<!-- OVERVIEW -->
<div class="view active" data-pane="overview">
  <div class="cards">
    <div class="card act"><div class="ic">🔌</div><div class="l">Active Sessions</div><div class="v" id="c-active">—</div></div>
    <div class="card hr"><div class="ic">🔁</div><div class="l">Cache Hit Rate</div><div class="v green" id="c-hr">—</div></div>
    <div class="card tok"><div class="ic">✂️</div><div class="l">Tokens Saved</div><div class="v purple" id="c-tok">—</div></div>
    <div class="card cost"><div class="ic">💰</div><div class="l">Cost Saved (est.)</div><div class="v yellow" id="c-cost">—</div></div>
    <div class="card ce"><div class="ic">🗄️</div><div class="l">Cache Entries</div><div class="v cyan" id="c-ce">—</div></div>
    <div class="card rl"><div class="ic">🚦</div><div class="l">Rate Limited</div><div class="v red" id="c-rl">—</div></div>
  </div>
  <div class="charts">
    <div class="chart-box"><h3>Cache Hit Rate % · last 60s</h3><canvas id="ch-hr" height="60"></canvas></div>
    <div class="chart-box"><h3>Sessions Accepted / s · last 60s</h3><canvas id="ch-sess" height="60"></canvas></div>
  </div>
  <div class="tbl">
    <table><thead><tr><th>Metric</th><th>Value</th><th style="width:180px">Bar</th></tr></thead>
    <tbody id="tbody"></tbody></table>
  </div>
</div>

<!-- REQUESTS -->
<div class="view" data-pane="requests">
  <div class="tbl">
    <table><thead><tr>
      <th>Time</th><th>Provider</th><th>Method</th><th>Path</th><th>Model</th>
      <th>Cache</th><th>Status</th><th>Latency</th><th>Saved</th>
    </tr></thead><tbody id="req-body"></tbody></table>
    <div class="empty" id="req-empty">No requests yet. Send traffic through the proxy to see it here.</div>
  </div>
</div>

<!-- PROVIDERS -->
<div class="view" data-pane="providers">
  <div class="tbl">
    <table><thead><tr>
      <th>Provider</th><th>Requests</th><th>Hit Rate</th><th>Filtered</th>
      <th>Tokens Saved</th><th>Cost Saved</th><th>Avg Latency</th><th>Traffic</th>
    </tr></thead><tbody id="prov-body"></tbody></table>
    <div class="empty" id="prov-empty">No provider traffic yet.</div>
  </div>
</div>

<!-- SETTINGS -->
<div class="view" data-pane="settings">
  <div class="settings">
    <div class="panel">
      <h3>Filters</h3>
      <div class="row"><div><label>PII Redaction</label><div class="hint">Strip emails, cards, keys, IPs</div></div>
        <label class="switch"><input type="checkbox" id="s-pii"><span class="slider"></span></label></div>
      <div class="row"><div><label>Max Chat History</label><div class="hint">0 = unlimited messages kept</div></div>
        <input type="number" id="s-history" min="0"></div>
    </div>
    <div class="panel">
      <h3>Cache</h3>
      <div class="row"><div><label>Cache Enabled</label><div class="hint">Dedupe identical requests</div></div>
        <label class="switch"><input type="checkbox" id="s-cache"><span class="slider"></span></label></div>
      <div class="row"><div><label>TTL (seconds)</label><div class="hint">How long entries live</div></div>
        <input type="number" id="s-ttl" min="0"></div>
      <div class="actions"><button class="btn danger" id="btn-clear">Clear cache now</button></div>
    </div>
    <div class="panel">
      <h3>Rate Limiting & Budget</h3>
      <div class="row"><div><label>Requests / min / client</label><div class="hint">0 = unlimited</div></div>
        <input type="number" id="s-rpm" min="0"></div>
      <div class="row"><div><label>Burst</label><div class="hint">0 = defaults to RPM</div></div>
        <input type="number" id="s-burst" min="0"></div>
      <div class="row"><div><label>Monthly budget (USD)</label><div class="hint">0 = no alert</div></div>
        <input type="number" id="s-budget" min="0" step="1"></div>
    </div>
    <div class="panel">
      <h3>Provider Routing</h3>
      <div><label class="hint">Allowlist (one host per line, empty = intercept all)</label>
        <textarea id="s-allow"></textarea></div>
      <div><label class="hint">Denylist (never intercept)</label>
        <textarea id="s-deny"></textarea></div>
    </div>
  </div>
  <div class="actions"><button class="btn" id="btn-save">Save settings</button>
    <button class="btn ghost" id="btn-reload">Reload</button></div>
</div>

<div class="toast" id="toast"></div>

<script>
const $=id=>document.getElementById(id);
const tok=()=>$('tok').value.trim();
function hdrs(){const h={};const t=tok();if(t)h['Authorization']='Bearer '+t;return h}
async function api(path,opts){opts=opts||{};opts.headers=Object.assign(hdrs(),opts.headers||{});const r=await fetch(path,opts);return r}
function fmt(n){if(n>=1e9)return(n/1e9).toFixed(2)+'B';if(n>=1e6)return(n/1e6).toFixed(2)+'M';if(n>=1e3)return(n/1e3).toFixed(1)+'K';return''+n}
function fmtB(b){if(b>=1073741824)return(b/1073741824).toFixed(2)+' GB';if(b>=1048576)return(b/1048576).toFixed(2)+' MB';if(b>=1024)return(b/1024).toFixed(1)+' KB';return b+' B'}
function toast(m){const t=$('toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2200)}

// persist token
$('tok').value=localStorage.getItem('fg_tok')||'';
$('tok').addEventListener('change',()=>localStorage.setItem('fg_tok',tok()));

// tabs
let active='overview';
document.querySelectorAll('.tab').forEach(t=>t.addEventListener('click',()=>{
  active=t.dataset.view;
  document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('active',x===t));
  document.querySelectorAll('.view').forEach(v=>v.classList.toggle('active',v.dataset.pane===active));
  if(active==='requests')refreshRequests();
  if(active==='providers')refreshProviders();
  if(active==='settings')loadConfig();
}));

function sparkline(canvas,data,color){
  const dpr=window.devicePixelRatio||1,rect=canvas.getBoundingClientRect();
  canvas.width=rect.width*dpr;canvas.height=rect.height*dpr;
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);
  const W=rect.width,H=rect.height;if(!data||data.length<2)return;
  const max=Math.max(...data,1);ctx.clearRect(0,0,W,H);
  const pts=data.map((v,i)=>[i/(data.length-1)*W,H-(v/max)*(H-4)-2]);
  ctx.beginPath();pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));
  ctx.lineTo(W,H);ctx.lineTo(0,H);ctx.closePath();
  const g=ctx.createLinearGradient(0,0,0,H);g.addColorStop(0,color+'55');g.addColorStop(1,color+'00');
  ctx.fillStyle=g;ctx.fill();
  ctx.beginPath();pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));
  ctx.strokeStyle=color;ctx.lineWidth=2;ctx.stroke();
}

let hrHistory=[],sessHistory=[],prevAccepted=null,budget=0;
async function refresh(){
  try{
    const d=await (await api('/stats')).json();
    const hr=d.cache_hit_rate;
    $('c-active').textContent=d.active_sessions;
    $('c-hr').textContent=hr.toFixed(1)+'%';
    $('c-tok').textContent=fmt(d.estimated_tokens_saved);
    $('c-cost').textContent='$'+d.estimated_cost_saved_usd.toFixed(4);
    $('c-ce').textContent=fmt(d.cache_entries);
    $('c-rl').textContent=fmt(d.rate_limited);
    $('ts').textContent='Updated '+new Date().toLocaleTimeString();
    hrHistory.push(hr);if(hrHistory.length>60)hrHistory.shift();
    const acc=d.accepted_sessions,delta=prevAccepted!==null?Math.max(0,acc-prevAccepted):0;
    prevAccepted=acc;sessHistory.push(delta);if(sessHistory.length>60)sessHistory.shift();
    sparkline($('ch-hr'),hrHistory,'#4ade80');sparkline($('ch-sess'),sessHistory,'#a78bfa');
    const rows=[
      ['Sessions Accepted',fmt(d.accepted_sessions),null,null],
      ['Sessions Active',d.active_sessions,null,null],
      ['Upstream Failures',fmt(d.upstream_failures),null,null],
      ['Bytes In (→ upstream)',fmtB(d.bytes_in),null,null],
      ['Bytes Out (← upstream)',fmtB(d.bytes_out),null,null],
      ['Cache Hits',fmt(d.cache_hits),hr,100],
      ['Cache Misses',fmt(d.cache_misses),null,null],
      ['Cache Hit Rate',hr.toFixed(1)+'%',hr,100],
      ['Cache Entries',fmt(d.cache_entries),null,null],
      ['Filtered Requests',fmt(d.filtered_requests),null,null],
      ['Rate Limited',fmt(d.rate_limited),null,null],
      ['Tokens Saved (est.)',fmt(d.estimated_tokens_saved),null,null],
      ['Cost Saved (est.)','$'+d.estimated_cost_saved_usd.toFixed(4),null,null],
    ];
    $('tbody').innerHTML=rows.map(([k,v,pct,max])=>`<tr><td>${k}</td><td>${v}</td><td>${max!=null?
      `<div class="bar-wrap"><div class="bar-fill" style="width:${Math.min(100,pct/max*100).toFixed(1)}%"></div></div>`:''}</td></tr>`).join('');
    // budget banner
    if(budget>0){
      const spend=d.estimated_cost_saved_usd; // proxy uses savings as a stand-in signal
      const b=$('banner');
      if(spend>=budget){b.style.display='block';b.textContent=`⚠ Estimated savings $${spend.toFixed(2)} have crossed your $${budget} monthly reference — review traffic in the Providers tab.`;}
      else b.style.display='none';
    }
  }catch(e){$('ts').textContent='Error: '+e.message;}
}

async function refreshRequests(){
  try{
    const d=await (await api('/requests')).json();
    $('req-empty').style.display=d.length?'none':'block';
    $('req-body').innerHTML=d.map(r=>{
      const cls=r.cache==='hit'?'b-hit':r.cache==='miss'?'b-miss':'b-bypass';
      const sc=r.status>=500?'st-5':r.status>=400?'st-4':r.status>=200?'st-2':'';
      const tm=new Date(r.ts).toLocaleTimeString();
      const saved=r.cost_saved_usd>0?'$'+r.cost_saved_usd.toFixed(4):(r.tokens_saved?fmt(r.tokens_saved)+' tok':'—');
      return `<tr><td class="mono">${tm}</td><td>${r.host}</td><td class="mono">${r.method}</td>
        <td class="path mono" title="${r.path}">${r.path}</td><td class="mono">${r.model||'—'}</td>
        <td><span class="badge ${cls}">${r.cache}</span>${r.filtered?' <span class="badge b-miss">filt</span>':''}</td>
        <td class="${sc}">${r.status||'—'}</td><td>${r.latency_ms?r.latency_ms+' ms':'—'}</td><td>${saved}</td></tr>`;
    }).join('');
  }catch(e){}
}

async function refreshProviders(){
  try{
    const d=await (await api('/providers')).json();
    d.sort((a,b)=>b.requests-a.requests);
    $('prov-empty').style.display=d.length?'none':'block';
    $('prov-body').innerHTML=d.map(p=>`<tr>
      <td>${p.host}</td><td>${fmt(p.requests)}</td>
      <td><div class="bar-wrap"><div class="bar-fill" style="width:${p.hit_rate.toFixed(0)}%"></div></div> ${p.hit_rate.toFixed(0)}%</td>
      <td>${fmt(p.filtered)}</td><td class="purple">${fmt(p.tokens_saved)}</td>
      <td class="yellow">$${p.cost_saved_usd.toFixed(4)}</td><td>${p.avg_latency_ms} ms</td>
      <td class="mono">${fmtB(p.bytes_in)} ↑ / ${fmtB(p.bytes_out)} ↓</td></tr>`).join('');
  }catch(e){}
}

async function loadConfig(){
  try{
    const c=await (await api('/api/config')).json();
    $('s-pii').checked=c.pii_redaction;
    $('s-cache').checked=c.cache_enabled;
    $('s-history').value=c.max_chat_history;
    $('s-ttl').value=c.cache_ttl_seconds;
    $('s-rpm').value=c.rate_limit_rpm;
    $('s-burst').value=c.rate_limit_burst;
    $('s-budget').value=c.monthly_budget_usd;
    $('s-allow').value=(c.allowlist||[]).join('\n');
    $('s-deny').value=(c.denylist||[]).join('\n');
    budget=c.monthly_budget_usd||0;
  }catch(e){toast('Failed to load config: '+e.message)}
}

async function saveConfig(){
  const body={
    pii_redaction:$('s-pii').checked,
    cache_enabled:$('s-cache').checked,
    max_chat_history:+$('s-history').value||0,
    cache_ttl_seconds:+$('s-ttl').value||0,
    rate_limit_rpm:+$('s-rpm').value||0,
    rate_limit_burst:+$('s-burst').value||0,
    monthly_budget_usd:+$('s-budget').value||0,
    allowlist:$('s-allow').value.split('\n').map(s=>s.trim()).filter(Boolean),
    denylist:$('s-deny').value.split('\n').map(s=>s.trim()).filter(Boolean)
  };
  try{
    const r=await api('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    if(r.ok){toast('Settings applied ✓');budget=body.monthly_budget_usd;}
    else toast('Save failed ('+r.status+')');
  }catch(e){toast('Save failed: '+e.message)}
}

$('btn-save').addEventListener('click',saveConfig);
$('btn-reload').addEventListener('click',loadConfig);
$('btn-clear').addEventListener('click',async()=>{
  try{const r=await api('/api/cache/clear',{method:'POST'});toast(r.ok?'Cache cleared ✓':'Failed ('+r.status+')');}
  catch(e){toast('Failed: '+e.message)}
});

refresh();loadConfig();
setInterval(()=>{refresh();if(active==='requests')refreshRequests();if(active==='providers')refreshProviders();},1500);
</script>
</body>
</html>)html";

// Apply a control POST body (JSON) onto the live RuntimeControls / RateLimiter.
void apply_control(const std::string& body, RuntimeControls& rc, RateLimiter& rl) {
    simdjson::dom::parser parser;
    simdjson::padded_string padded(body);
    simdjson::dom::element root;
    if (parser.parse(padded).get(root) != simdjson::SUCCESS) return;

    bool b;
    int64_t i;
    double d;
    if (root["pii_redaction"].get(b) == simdjson::SUCCESS) rc.pii_redaction.store(b);
    if (root["cache_enabled"].get(b) == simdjson::SUCCESS) rc.cache_enabled.store(b);
    if (root["max_chat_history"].get(i) == simdjson::SUCCESS && i >= 0)
        rc.max_chat_history.store(static_cast<std::size_t>(i));
    if (root["cache_ttl_seconds"].get(i) == simdjson::SUCCESS && i >= 0)
        rc.cache_ttl_seconds.store(static_cast<std::size_t>(i));
    if (root["monthly_budget_usd"].get(d) == simdjson::SUCCESS && d >= 0)
        rc.monthly_budget_usd.store(d);

    std::size_t rpm = rc.rate_limit_rpm.load(), burst = rc.rate_limit_burst.load();
    if (root["rate_limit_rpm"].get(i) == simdjson::SUCCESS && i >= 0) rpm = static_cast<std::size_t>(i);
    if (root["rate_limit_burst"].get(i) == simdjson::SUCCESS && i >= 0) burst = static_cast<std::size_t>(i);
    rc.rate_limit_rpm.store(rpm);
    rc.rate_limit_burst.store(burst);
    rl.configure(rpm, burst);

    const auto load_list = [&](const char* key, auto setter) {
        simdjson::dom::array arr;
        if (root[key].get(arr) != simdjson::SUCCESS) return;
        std::vector<std::string> hosts;
        for (auto el : arr) {
            std::string_view sv;
            if (el.get(sv) == simdjson::SUCCESS) hosts.emplace_back(sv);
        }
        setter(std::move(hosts));
    };
    load_list("allowlist", [&](std::vector<std::string> h){ rc.set_allowlist(std::move(h)); });
    load_list("denylist",  [&](std::vector<std::string> h){ rc.set_denylist(std::move(h)); });
}

std::string config_json(const RuntimeControls& rc) {
    std::ostringstream o;
    o << "{\"pii_redaction\":" << (rc.pii_redaction.load() ? "true" : "false")
      << ",\"cache_enabled\":" << (rc.cache_enabled.load() ? "true" : "false")
      << ",\"max_chat_history\":" << rc.max_chat_history.load()
      << ",\"cache_ttl_seconds\":" << rc.cache_ttl_seconds.load()
      << ",\"rate_limit_rpm\":" << rc.rate_limit_rpm.load()
      << ",\"rate_limit_burst\":" << rc.rate_limit_burst.load()
      << ",\"monthly_budget_usd\":" << rc.monthly_budget_usd.load();
    const auto emit = [&](const char* key, const std::vector<std::string>& hosts) {
        o << ",\"" << key << "\":[";
        for (std::size_t k = 0; k < hosts.size(); ++k) {
            if (k) o << ',';
            o << '"' << hosts[k] << '"';
        }
        o << ']';
    };
    emit("allowlist", rc.allowlist());
    emit("denylist", rc.denylist());
    o << '}';
    return o.str();
}

// ── Admin session ─────────────────────────────────────────────────────────────

class AdminSession final : public std::enable_shared_from_this<AdminSession> {
public:
    AdminSession(tcp::socket socket, std::shared_ptr<Metrics> metrics,
                 std::shared_ptr<RuntimeControls> controls, std::shared_ptr<ICache> cache,
                 std::shared_ptr<RateLimiter> rate_limiter,
                 const std::string& token, const MetricsHistory& history)
        : socket_(std::move(socket)), metrics_(std::move(metrics)),
          controls_(std::move(controls)), cache_(std::move(cache)),
          rate_limiter_(std::move(rate_limiter)), token_(token), history_(history) {}

    void start() {
        asio::async_read_until(socket_, request_, "\r\n\r\n",
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                if (ec) return close();
                header_bytes_ = bytes;
                const auto data = request_.data();
                head_.assign(asio::buffers_begin(data),
                             asio::buffers_begin(data) + static_cast<std::ptrdiff_t>(bytes));
                const std::size_t len = content_length_of(head_);
                const std::size_t have = request_.size() - bytes;  // body already buffered
                if (len > have) {
                    asio::async_read(socket_, request_, asio::transfer_exactly(len - have),
                        [this, self, len](std::error_code ec2, std::size_t) {
                            if (ec2) return close();
                            capture_body(len);
                            handle();
                        });
                } else {
                    capture_body(len);
                    handle();
                }
            });
    }

private:
    void capture_body(std::size_t len) {
        if (len == 0) return;
        request_.consume(header_bytes_);
        const auto data = request_.data();
        const std::size_t take = std::min(len, request_.size());
        body_.assign(asio::buffers_begin(data),
                     asio::buffers_begin(data) + static_cast<std::ptrdiff_t>(take));
    }

    void handle() {
        const std::string& req = head_;

        if (!token_.empty() && !check_bearer_token(req, token_))
            return respond("401 Unauthorized", "text/plain", "Authorization required\n", true);

        if (req.starts_with("GET / ") || req.starts_with("GET /\r"))
            respond("200 OK", "text/html; charset=utf-8", DASHBOARD);
        else if (req.starts_with("GET /healthz "))
            respond("200 OK", "text/plain", "ok\n");
        else if (req.starts_with("GET /metrics "))
            respond("200 OK", "text/plain; version=0.0.4", to_prometheus_text(metrics_->snapshot()));
        else if (req.starts_with("GET /stats "))
            respond("200 OK", "application/json", to_json(metrics_->snapshot()));
        else if (req.starts_with("GET /history "))
            respond("200 OK", "application/json", history_.to_json());
        else if (req.starts_with("GET /providers "))
            respond("200 OK", "application/json", metrics_->providers_json());
        else if (req.starts_with("GET /requests "))
            respond("200 OK", "application/json", metrics_->requests_json());
        else if (req.starts_with("GET /api/config "))
            respond("200 OK", "application/json",
                    controls_ ? config_json(*controls_) : "{}");
        else if (req.starts_with("POST /api/control ")) {
            if (controls_ && rate_limiter_) apply_control(body_, *controls_, *rate_limiter_);
            respond("200 OK", "application/json", "{\"ok\":true}");
        } else if (req.starts_with("POST /api/cache/clear ")) {
            if (cache_) cache_->clear();
            respond("200 OK", "application/json", "{\"ok\":true}");
        } else
            respond("404 Not Found", "text/plain", "not found\n");
    }

    void respond(std::string_view status, std::string_view ct,
                 std::string body, bool auth_err = false) {
        response_  = "HTTP/1.1 "; response_ += status;
        response_ += "\r\nContent-Type: "; response_ += ct;
        if (auth_err) response_ += "\r\nWWW-Authenticate: Bearer realm=\"fluxgate\"";
        response_ += "\r\nAccess-Control-Allow-Origin: *";
        response_ += "\r\nConnection: close\r\nContent-Length: ";
        response_ += std::to_string(body.size());
        response_ += "\r\n\r\n";
        response_ += body;

        asio::async_write(socket_, asio::buffer(response_),
            [this, self = shared_from_this()](std::error_code, std::size_t) { close(); });
    }

    void close() {
        std::error_code e;
        socket_.shutdown(tcp::socket::shutdown_both, e);
        socket_.close(e);
    }

    tcp::socket socket_;
    asio::streambuf request_;
    std::shared_ptr<Metrics> metrics_;
    std::shared_ptr<RuntimeControls> controls_;
    std::shared_ptr<ICache> cache_;
    std::shared_ptr<RateLimiter> rate_limiter_;
    const std::string& token_;
    const MetricsHistory& history_;
    std::size_t header_bytes_ = 0;
    std::string head_;
    std::string body_;
    std::string response_;
};

} // namespace

// ── MetricsHistory ────────────────────────────────────────────────────────────

MetricsHistory::MetricsHistory(std::size_t capacity) : capacity_(capacity) {}

void MetricsHistory::push(const MetricsSnapshot& s) {
    std::lock_guard lock(mutex_);
    buf_.push_back(s);
    while (buf_.size() > capacity_) buf_.pop_front();
}

std::string MetricsHistory::to_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream o;
    o << '[';
    for (std::size_t i = 0; i < buf_.size(); ++i) {
        if (i) o << ',';
        const auto& s = buf_[i];
        const double hr = (s.cache_hits + s.cache_misses > 0)
            ? 100.0 * s.cache_hits / (s.cache_hits + s.cache_misses) : 0.0;
        o << "{\"hr\":" << hr
          << ",\"acc\":" << s.accepted_sessions
          << ",\"act\":" << s.active_sessions
          << "}";
    }
    o << ']';
    return o.str();
}

// ── AdminServer ───────────────────────────────────────────────────────────────

AdminServer::AdminServer(asio::io_context& io_context, std::string host,
                         unsigned short port, std::shared_ptr<Metrics> metrics,
                         std::shared_ptr<RuntimeControls> controls,
                         std::shared_ptr<ICache> cache,
                         std::shared_ptr<RateLimiter> rate_limiter,
                         std::string token)
    : acceptor_(io_context), metrics_(std::move(metrics)), controls_(std::move(controls)),
      cache_(std::move(cache)), rate_limiter_(std::move(rate_limiter)), token_(std::move(token)) {
    tcp::resolver resolver(io_context);
    const auto ep = *resolver.resolve(host, std::to_string(port)).begin();
    acceptor_.open(ep.endpoint().protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep.endpoint());
    acceptor_.listen(asio::socket_base::max_listen_connections);
    do_accept();
}

void AdminServer::tick() {
    if (cache_) metrics_->set_cache_entries(cache_->size());
    history_.push(metrics_->snapshot());
}

void AdminServer::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec)
            std::make_shared<AdminSession>(std::move(socket), metrics_, controls_, cache_,
                                           rate_limiter_, token_, history_)->start();
        else
            std::cerr << "admin accept failed: " << ec.message() << '\n';
        do_accept();
    });
}

} // namespace fluxgate
