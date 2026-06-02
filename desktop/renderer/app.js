const $ = (id) => document.getElementById(id);

// ── persisted settings ──────────────────────────────────────────────────────
const store = {
  get url()   { return localStorage.getItem('fg_url')   || 'http://127.0.0.1:9090'; },
  get token() { return localStorage.getItem('fg_token') || ''; },
  get bin()   { return localStorage.getItem('fg_bin')   || ''; },
  get args()  { return localStorage.getItem('fg_args')  || '--admin 127.0.0.1:9090'; },
  set(k, v)   { localStorage.setItem('fg_' + k, v); },
};

const api = (p, opts = {}) => {
  opts.headers = Object.assign(opts.headers || {}, store.token ? { Authorization: 'Bearer ' + store.token } : {});
  return fetch(store.url.replace(/\/$/, '') + p, opts);
};

const fmt = (n) => n >= 1e9 ? (n/1e9).toFixed(2)+'B' : n >= 1e6 ? (n/1e6).toFixed(2)+'M' : n >= 1e3 ? (n/1e3).toFixed(1)+'K' : ''+n;
const fmtB = (b) => b >= 1073741824 ? (b/1073741824).toFixed(2)+' GB' : b >= 1048576 ? (b/1048576).toFixed(2)+' MB' : b >= 1024 ? (b/1024).toFixed(1)+' KB' : b+' B';
function toast(m){ const t=$('toast'); t.textContent=m; t.classList.add('show'); setTimeout(()=>t.classList.remove('show'),2200); }

// ── navigation ───────────────────────────────────────────────────────────────
let active = 'overview';
document.querySelectorAll('.nav[data-view]').forEach(n => n.addEventListener('click', () => {
  active = n.dataset.view;
  document.querySelectorAll('.nav[data-view]').forEach(x => x.classList.toggle('active', x === n));
  document.querySelectorAll('.view').forEach(v => v.classList.toggle('active', v.dataset.pane === active));
  if (active === 'requests') refreshRequests();
  if (active === 'providers') refreshProviders();
  if (active === 'settings') loadConfig();
}));
$('open-dash').addEventListener('click', () => window.open(store.url, '_blank'));

// ── sparkline ─────────────────────────────────────────────────────────────────
function sparkline(canvas, data, color){
  const dpr=window.devicePixelRatio||1, rect=canvas.getBoundingClientRect();
  canvas.width=rect.width*dpr; canvas.height=rect.height*dpr;
  const ctx=canvas.getContext('2d'); ctx.scale(dpr,dpr);
  const W=rect.width,H=rect.height; if(!data||data.length<2)return;
  const max=Math.max(...data,1); ctx.clearRect(0,0,W,H);
  const pts=data.map((v,i)=>[i/(data.length-1)*W, H-(v/max)*(H-4)-2]);
  ctx.beginPath(); pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));
  ctx.lineTo(W,H); ctx.lineTo(0,H); ctx.closePath();
  const g=ctx.createLinearGradient(0,0,0,H); g.addColorStop(0,color+'55'); g.addColorStop(1,color+'00');
  ctx.fillStyle=g; ctx.fill();
  ctx.beginPath(); pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));
  ctx.strokeStyle=color; ctx.lineWidth=2; ctx.stroke();
}

// ── connection + data polling ──────────────────────────────────────────────────
let hrHist=[], sessHist=[], prevAcc=null;
function setConn(ok){
  const c=$('conn');
  c.className='conn '+(ok?'ok':'bad');
  $('conn-text').textContent = ok ? 'Connected' : 'Disconnected';
}

async function refresh(){
  let d;
  try { d = await (await api('/stats')).json(); setConn(true); }
  catch(e){ setConn(false); $('ts').textContent='Cannot reach '+store.url; return; }

  const hr=d.cache_hit_rate;
  $('c-active').textContent=d.active_sessions;
  $('c-hr').textContent=hr.toFixed(1)+'%';
  $('c-tok').textContent=fmt(d.estimated_tokens_saved);
  $('c-cost').textContent='$'+d.estimated_cost_saved_usd.toFixed(4);
  $('c-ce').textContent=fmt(d.cache_entries);
  $('c-rl').textContent=fmt(d.rate_limited);
  $('ts').textContent='Updated '+new Date().toLocaleTimeString();

  hrHist.push(hr); if(hrHist.length>60)hrHist.shift();
  const acc=d.accepted_sessions, delta=prevAcc!==null?Math.max(0,acc-prevAcc):0;
  prevAcc=acc; sessHist.push(delta); if(sessHist.length>60)sessHist.shift();
  sparkline($('ch-hr'),hrHist,'#4ade80'); sparkline($('ch-sess'),sessHist,'#a78bfa');

  const rows=[
    ['Sessions Accepted',fmt(d.accepted_sessions),null,null],
    ['Sessions Active',d.active_sessions,null,null],
    ['Upstream Failures',fmt(d.upstream_failures),null,null],
    ['Bytes In',fmtB(d.bytes_in),null,null],
    ['Bytes Out',fmtB(d.bytes_out),null,null],
    ['Cache Hits',fmt(d.cache_hits),hr,100],
    ['Cache Misses',fmt(d.cache_misses),null,null],
    ['Cache Entries',fmt(d.cache_entries),null,null],
    ['Filtered Requests',fmt(d.filtered_requests),null,null],
    ['Rate Limited',fmt(d.rate_limited),null,null],
    ['Tokens Saved (est.)',fmt(d.estimated_tokens_saved),null,null],
    ['Cost Saved (est.)','$'+d.estimated_cost_saved_usd.toFixed(4),null,null],
  ];
  $('ov-body').innerHTML=rows.map(([k,v,pct,max])=>`<tr><td>${k}</td><td>${v}</td><td>${max!=null?
    `<div class="bar-wrap"><div class="bar-fill" style="width:${Math.min(100,pct/max*100).toFixed(1)}%"></div></div>`:''}</td></tr>`).join('');
}

async function refreshRequests(){
  try{
    const d=await (await api('/requests')).json();
    $('req-empty').style.display=d.length?'none':'block';
    $('req-body').innerHTML=d.map(r=>{
      const cls=r.cache==='hit'?'b-hit':r.cache==='miss'?'b-miss':'b-bypass';
      const sc=r.status>=500?'st-5':r.status>=400?'st-4':r.status>=200?'st-2':'';
      const saved=r.cost_saved_usd>0?'$'+r.cost_saved_usd.toFixed(4):(r.tokens_saved?fmt(r.tokens_saved)+' tok':'—');
      return `<tr><td class="mono">${new Date(r.ts).toLocaleTimeString()}</td><td>${r.host}</td>
        <td class="mono">${r.method}</td><td class="path mono" title="${r.path}">${r.path}</td>
        <td class="mono">${r.model||'—'}</td>
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

// ── settings ───────────────────────────────────────────────────────────────────
async function loadConfig(){
  $('s-url').value=store.url; $('s-token').value=store.token;
  $('s-args').value=store.args;
  if(!$('s-bin').value){ $('s-bin').value = store.bin || await window.fluxgate.findBinary(); }
  try{
    const c=await (await api('/api/config')).json();
    $('s-pii').checked=c.pii_redaction; $('s-cache').checked=c.cache_enabled;
    $('s-history').value=c.max_chat_history; $('s-ttl').value=c.cache_ttl_seconds;
    $('s-rpm').value=c.rate_limit_rpm; $('s-burst').value=c.rate_limit_burst;
    $('s-budget').value=c.monthly_budget_usd;
    $('s-allow').value=(c.allowlist||[]).join('\n'); $('s-deny').value=(c.denylist||[]).join('\n');
  }catch(e){/* proxy not running yet — leave fields */}
}

function saveLocal(){
  store.set('url',$('s-url').value.trim()||'http://127.0.0.1:9090');
  store.set('token',$('s-token').value.trim());
  store.set('bin',$('s-bin').value.trim());
  store.set('args',$('s-args').value.trim());
}

async function saveConfig(){
  saveLocal();
  const body={
    pii_redaction:$('s-pii').checked, cache_enabled:$('s-cache').checked,
    max_chat_history:+$('s-history').value||0, cache_ttl_seconds:+$('s-ttl').value||0,
    rate_limit_rpm:+$('s-rpm').value||0, rate_limit_burst:+$('s-burst').value||0,
    monthly_budget_usd:+$('s-budget').value||0,
    allowlist:$('s-allow').value.split('\n').map(s=>s.trim()).filter(Boolean),
    denylist:$('s-deny').value.split('\n').map(s=>s.trim()).filter(Boolean),
  };
  try{
    const r=await api('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    toast(r.ok?'Settings applied ✓':'Could not reach proxy ('+r.status+')');
  }catch(e){ toast('Saved locally — proxy not reachable'); }
}

$('btn-save').addEventListener('click',saveConfig);
$('btn-reload').addEventListener('click',loadConfig);
$('btn-clear').addEventListener('click',async()=>{
  try{ const r=await api('/api/cache/clear',{method:'POST'}); toast(r.ok?'Cache cleared ✓':'Failed'); }
  catch(e){ toast('Proxy not reachable'); }
});

// ── proxy process control ───────────────────────────────────────────────────────
const runBtn=$('run-btn');
function setRunState(running){
  runBtn.dataset.state = running ? 'running' : 'stopped';
  runBtn.textContent   = running ? 'Stop proxy' : 'Start proxy';
}
runBtn.addEventListener('click',async()=>{
  saveLocal();
  if(runBtn.dataset.state==='running'){ await window.fluxgate.stopProxy(); return; }
  const args=(store.args||'').split(/\s+/).filter(Boolean);
  const res=await window.fluxgate.startProxy({ binary: store.bin || undefined, args });
  if(!res.ok){ toast('Start failed: '+res.error); appendLog('start failed: '+res.error+'\n','err'); }
  else { appendLog('[started] '+res.binary+' '+args.join(' ')+'  (pid '+res.pid+')\n','out'); }
});

function appendLog(line,stream){
  const el=$('logs');
  const span=document.createElement('span');
  if(stream==='err')span.className='e';
  span.textContent=line;
  el.appendChild(span);
  el.scrollTop=el.scrollHeight;
}
$('btn-clearlog').addEventListener('click',()=>{$('logs').textContent='';});
window.fluxgate.onLog(({line,stream})=>appendLog(line,stream));
window.fluxgate.onState(({running})=>setRunState(running));

// ── boot ────────────────────────────────────────────────────────────────────────
window.fluxgate.proxyState().then(s=>setRunState(s.running));
loadConfig();
refresh();
setInterval(()=>{
  refresh();
  if(active==='requests')refreshRequests();
  if(active==='providers')refreshProviders();
}, 1500);
