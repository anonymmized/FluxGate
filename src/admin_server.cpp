#include "fluxgate/admin_server.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

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

// ── HTML dashboard ────────────────────────────────────────────────────────────
const char DASHBOARD[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluxGate Dashboard</title>
<style>
:root{--bg:#080b12;--s1:#0e1220;--s2:#141828;--border:#1e2440;--purple:#7c3aed;--pl:#a78bfa;--green:#4ade80;--yellow:#fbbf24;--cyan:#22d3ee;--text:#e2e8f0;--muted:#64748b}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding:20px 24px}
.hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:24px}
.logo{font-size:1.3rem;font-weight:800;color:var(--pl)}
.logo span{color:var(--text);font-weight:400;font-size:.9rem;margin-left:8px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);display:inline-block;margin-right:6px;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.ts{font-size:.75rem;color:var(--muted)}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin-bottom:24px}
.card{background:var(--s1);border:1px solid var(--border);border-radius:12px;padding:18px 20px}
.card .l{font-size:.7rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin-bottom:8px}
.card .v{font-size:2rem;font-weight:800}
.green{color:var(--green)}.purple{color:var(--pl)}.yellow{color:var(--yellow)}.cyan{color:var(--cyan)}
.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-bottom:24px}
.chart-box{background:var(--s1);border:1px solid var(--border);border-radius:12px;padding:18px 20px}
.chart-box h3{font-size:.75rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin-bottom:12px}
canvas{width:100%;height:60px;display:block}
.tbl{background:var(--s1);border:1px solid var(--border);border-radius:12px;overflow:hidden}
table{width:100%;border-collapse:collapse}
th{background:var(--s2);color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.05em;padding:10px 16px;text-align:left;font-weight:600}
td{padding:9px 16px;font-size:.85rem;border-top:1px solid var(--border)}
tr:hover td{background:rgba(30,36,64,.5)}
.bar-wrap{background:var(--s2);border-radius:4px;height:6px;width:100%;overflow:hidden;margin-top:3px}
.bar-fill{height:100%;border-radius:4px;background:linear-gradient(90deg,var(--purple),var(--cyan));transition:width .4s ease}
</style>
</head>
<body>
<div class="hdr">
  <div class="logo"><span class="dot"></span>⚡ FluxGate<span>Live Dashboard</span></div>
  <div class="ts" id="ts">—</div>
</div>

<div class="cards" id="cards">
  <div class="card"><div class="l">Active Sessions</div><div class="v" id="c-active">—</div></div>
  <div class="card"><div class="l">Cache Hit Rate</div><div class="v green" id="c-hr">—</div></div>
  <div class="card"><div class="l">Tokens Saved</div><div class="v purple" id="c-tok">—</div></div>
  <div class="card"><div class="l">Cost Saved (est.)</div><div class="v yellow" id="c-cost">—</div></div>
</div>

<div class="charts">
  <div class="chart-box">
    <h3>Cache Hit Rate % (last 60s)</h3>
    <canvas id="ch-hr" height="60"></canvas>
  </div>
  <div class="chart-box">
    <h3>Sessions Accepted / s (last 60s)</h3>
    <canvas id="ch-sess" height="60"></canvas>
  </div>
</div>

<div class="tbl">
<table>
<thead><tr><th>Metric</th><th>Value</th><th style="width:180px">Bar</th></tr></thead>
<tbody id="tbody"></tbody>
</table>
</div>

<script>
const $ = id => document.getElementById(id);
function fmt(n){if(n>=1e9)return(n/1e9).toFixed(2)+'B';if(n>=1e6)return(n/1e6).toFixed(2)+'M';if(n>=1e3)return(n/1e3).toFixed(1)+'K';return''+n}
function fmtB(b){if(b>=1073741824)return(b/1073741824).toFixed(2)+' GB';if(b>=1048576)return(b/1048576).toFixed(2)+' MB';if(b>=1024)return(b/1024).toFixed(1)+' KB';return b+' B'}

// Sparkline
function sparkline(canvas, data, color){
  const dpr=window.devicePixelRatio||1;
  const rect=canvas.getBoundingClientRect();
  canvas.width=rect.width*dpr;
  canvas.height=rect.height*dpr;
  const ctx=canvas.getContext('2d');
  ctx.scale(dpr,dpr);
  const W=rect.width,H=rect.height;
  if(!data||data.length<2)return;
  const max=Math.max(...data,1);
  ctx.clearRect(0,0,W,H);
  // Fill
  ctx.beginPath();
  data.forEach((v,i)=>{
    const x=i/(data.length-1)*W;
    const y=H-(v/max)*(H-4)-2;
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.lineTo(W,H);ctx.lineTo(0,H);ctx.closePath();
  const grad=ctx.createLinearGradient(0,0,0,H);
  grad.addColorStop(0,color+'55');
  grad.addColorStop(1,color+'00');
  ctx.fillStyle=grad;ctx.fill();
  // Line
  ctx.beginPath();
  data.forEach((v,i)=>{
    const x=i/(data.length-1)*W;
    const y=H-(v/max)*(H-4)-2;
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.strokeStyle=color;ctx.lineWidth=2;ctx.stroke();
}

let hrHistory=[], sessHistory=[], prevAccepted=null;

async function refresh(){
  try{
    // Fetch current stats
    const r=await fetch('/stats');
    const d=await r.json();
    const hr=d.cache_hit_rate;

    $('c-active').textContent=d.active_sessions;
    $('c-hr').textContent=hr.toFixed(1)+'%';
    $('c-tok').textContent=fmt(d.estimated_tokens_saved);
    $('c-cost').textContent='$'+d.estimated_cost_saved_usd.toFixed(4);
    $('ts').textContent='Updated '+new Date().toLocaleTimeString();

    hrHistory.push(hr);
    if(hrHistory.length>60)hrHistory.shift();

    const newAcc=d.accepted_sessions;
    const delta=prevAccepted!==null?Math.max(0,newAcc-prevAccepted):0;
    prevAccepted=newAcc;
    sessHistory.push(delta);
    if(sessHistory.length>60)sessHistory.shift();

    sparkline($('ch-hr'),hrHistory,'#4ade80');
    sparkline($('ch-sess'),sessHistory,'#a78bfa');

    // Table
    const rows=[
      ['Sessions Accepted',fmt(d.accepted_sessions),d.accepted_sessions,null],
      ['Sessions Active',d.active_sessions,d.active_sessions,null],
      ['Upstream Failures',fmt(d.upstream_failures),0,null],
      ['Bytes In (→ upstream)',fmtB(d.bytes_in),0,null],
      ['Bytes Out (← upstream)',fmtB(d.bytes_out),0,null],
      ['Cache Hits',fmt(d.cache_hits),hr,100],
      ['Cache Misses',fmt(d.cache_misses),0,null],
      ['Cache Hit Rate',hr.toFixed(1)+'%',hr,100],
      ['Filtered Requests',fmt(d.filtered_requests),0,null],
      ['Tokens Saved (est.)',fmt(d.estimated_tokens_saved),0,null],
      ['Cost Saved @ $5/1M',   '$'+d.estimated_cost_saved_usd.toFixed(4),0,null],
    ];
    $('tbody').innerHTML=rows.map(([k,v,pct,max])=>`
      <tr><td>${k}</td><td>${v}</td><td>${max!=null?
        `<div class="bar-wrap"><div class="bar-fill" style="width:${Math.min(100,pct/max*100).toFixed(1)}%"></div></div>`
        :''}</td></tr>`).join('');
  }catch(e){$('ts').textContent='Error: '+e.message;}
}

refresh();
setInterval(refresh,1000);
</script>
</body>
</html>)html";

// ── Admin session ─────────────────────────────────────────────────────────────

class AdminSession final : public std::enable_shared_from_this<AdminSession> {
public:
    AdminSession(tcp::socket socket, std::shared_ptr<Metrics> metrics,
                 const std::string& token, const MetricsHistory& history)
        : socket_(std::move(socket)), metrics_(std::move(metrics)),
          token_(token), history_(history) {}

    void start() {
        asio::async_read_until(socket_, request_, "\r\n\r\n",
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                if (ec) return close();
                handle(bytes);
            });
    }

private:
    void handle(std::size_t bytes) {
        const auto data = request_.data();
        const std::string req(asio::buffers_begin(data),
                              asio::buffers_begin(data) + static_cast<std::ptrdiff_t>(bytes));

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
        else
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
    const std::string& token_;
    const MetricsHistory& history_;
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
                         std::string token)
    : acceptor_(io_context), metrics_(std::move(metrics)), token_(std::move(token)) {
    tcp::resolver resolver(io_context);
    const auto ep = *resolver.resolve(host, std::to_string(port)).begin();
    acceptor_.open(ep.endpoint().protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep.endpoint());
    acceptor_.listen(asio::socket_base::max_listen_connections);
    do_accept();
}

void AdminServer::tick() { history_.push(metrics_->snapshot()); }

void AdminServer::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec)
            std::make_shared<AdminSession>(std::move(socket), metrics_, token_, history_)->start();
        else
            std::cerr << "admin accept failed: " << ec.message() << '\n';
        do_accept();
    });
}

} // namespace fluxgate
