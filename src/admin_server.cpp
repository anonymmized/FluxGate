#include "fluxgate/admin_server.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <string>

namespace fluxgate {
namespace {

using asio::ip::tcp;

bool check_bearer_token(const std::string& request, const std::string& token) {
    std::size_t pos = 0;
    while (pos < request.size()) {
        const auto eol = request.find('\n', pos);
        const auto line_end = (eol == std::string::npos) ? request.size() : eol;
        std::string line = request.substr(pos, line_end - pos);
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
        pos = line_end + 1;
    }
    return false;
}

// ── HTML dashboard ───────────────────────────────────────────────────────────

const char DASHBOARD_HTML[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>FluxGate Dashboard</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0f1117;color:#e2e8f0;min-height:100vh;padding:24px}
h1{font-size:1.5rem;font-weight:700;color:#7c3aed;margin-bottom:4px}
.sub{font-size:.8rem;color:#64748b;margin-bottom:28px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;margin-bottom:28px}
.card{background:#1e293b;border-radius:12px;padding:20px}
.card .label{font-size:.72rem;text-transform:uppercase;letter-spacing:.05em;color:#64748b;margin-bottom:6px}
.card .value{font-size:2rem;font-weight:700;color:#f1f5f9}
.card .value.green{color:#4ade80}
.card .value.purple{color:#a78bfa}
.card .value.yellow{color:#fbbf24}
table{width:100%;border-collapse:collapse;background:#1e293b;border-radius:12px;overflow:hidden}
th,td{padding:10px 16px;text-align:left;font-size:.85rem}
th{background:#0f172a;color:#94a3b8;font-weight:600;text-transform:uppercase;font-size:.72rem;letter-spacing:.05em}
tr:not(:last-child) td{border-bottom:1px solid #1e293b}
tr:hover td{background:#273344}
.badge{display:inline-block;padding:2px 8px;border-radius:999px;font-size:.72rem;font-weight:600}
.ok{background:#166534;color:#bbf7d0}.warn{background:#713f12;color:#fef3c7}
#ts{color:#475569;font-size:.75rem;margin-top:16px}
</style>
</head>
<body>
<h1>⚡ FluxGate</h1>
<div class="sub" id="ts">Refreshing every 3s…</div>
<div class="grid" id="cards"></div>
<table id="tbl"><thead><tr>
<th>Metric</th><th>Value</th>
</tr></thead><tbody id="tbody"></tbody></table>
<script>
function fmt(n){if(n>=1e9)return(n/1e9).toFixed(2)+'B';if(n>=1e6)return(n/1e6).toFixed(2)+'M';if(n>=1e3)return(n/1e3).toFixed(1)+'K';return n}
function fmtBytes(b){if(b>=1073741824)return(b/1073741824).toFixed(2)+' GB';if(b>=1048576)return(b/1048576).toFixed(2)+' MB';if(b>=1024)return(b/1024).toFixed(1)+' KB';return b+' B'}
async function refresh(){
  try{
    const r=await fetch('/stats');
    const d=await r.json();
    const hr=d.cache_hit_rate.toFixed(1);
    document.getElementById('cards').innerHTML=`
      <div class="card"><div class="label">Active Sessions</div><div class="value">${d.active_sessions}</div></div>
      <div class="card"><div class="label">Cache Hit Rate</div><div class="value green">${hr}%</div></div>
      <div class="card"><div class="label">Tokens Saved</div><div class="value purple">${fmt(d.estimated_tokens_saved)}</div></div>
      <div class="card"><div class="label">Cost Saved (est.)</div><div class="value yellow">$${d.estimated_cost_saved_usd.toFixed(4)}</div></div>
    `;
    const rows=[
      ['Sessions Accepted',fmt(d.accepted_sessions)],
      ['Sessions Active',d.active_sessions],
      ['Sessions Rejected',fmt(d.rejected_sessions)],
      ['Upstream Failures',d.upstream_failures],
      ['Bytes In (client→upstream)',fmtBytes(d.bytes_in)],
      ['Bytes Out (upstream→client)',fmtBytes(d.bytes_out)],
      ['Cache Hits',fmt(d.cache_hits)],
      ['Cache Misses',fmt(d.cache_misses)],
      ['Cache Hit Rate',hr+'%'],
      ['Filtered Requests',fmt(d.filtered_requests)],
      ['Tokens Saved (est.)',fmt(d.estimated_tokens_saved)],
      ['Cost Saved @ $5/1M (est.)','$'+d.estimated_cost_saved_usd.toFixed(4)],
    ];
    document.getElementById('tbody').innerHTML=rows.map(([k,v])=>`<tr><td>${k}</td><td>${v}</td></tr>`).join('');
    document.getElementById('ts').textContent='Last updated: '+new Date().toLocaleTimeString();
  }catch(e){document.getElementById('ts').textContent='Error: '+e.message;}
}
refresh();setInterval(refresh,3000);
</script>
</body>
</html>)html";

// ── Session ──────────────────────────────────────────────────────────────────

class AdminSession final : public std::enable_shared_from_this<AdminSession> {
public:
    AdminSession(tcp::socket socket, std::shared_ptr<Metrics> metrics, const std::string& token)
        : socket_(std::move(socket)),
          metrics_(std::move(metrics)),
          token_(token) {}

    void start() {
        asio::async_read_until(socket_, request_, "\r\n\r\n",
            [this, self = shared_from_this()](std::error_code ec, std::size_t bytes) {
                if (ec) return close();
                handle_request(bytes);
            });
    }

private:
    void handle_request(std::size_t bytes) {
        const auto data = request_.data();
        const auto begin = asio::buffers_begin(data);
        const std::string request(begin, begin + static_cast<std::ptrdiff_t>(bytes));

        if (!token_.empty() && !check_bearer_token(request, token_)) {
            return write_response("401 Unauthorized", "text/plain",
                                  "Authorization required\n", /*www_auth=*/true);
        }

        if (request.starts_with("GET / ") || request.starts_with("GET /\r")) {
            write_response("200 OK", "text/html; charset=utf-8", DASHBOARD_HTML);
        } else if (request.starts_with("GET /healthz ")) {
            write_response("200 OK", "text/plain", "ok\n");
        } else if (request.starts_with("GET /metrics ")) {
            write_response("200 OK", "text/plain; version=0.0.4",
                           to_prometheus_text(metrics_->snapshot()));
        } else if (request.starts_with("GET /stats ")) {
            write_response("200 OK", "application/json",
                           to_json(metrics_->snapshot()));
        } else {
            write_response("404 Not Found", "text/plain", "not found\n");
        }
    }

    void write_response(std::string_view status, std::string_view content_type,
                        std::string body, bool www_auth = false) {
        response_ = "HTTP/1.1 ";
        response_ += status;
        response_ += "\r\nContent-Type: ";
        response_ += content_type;
        if (www_auth)
            response_ += "\r\nWWW-Authenticate: Bearer realm=\"fluxgate\"";
        response_ += "\r\nAccess-Control-Allow-Origin: *";
        response_ += "\r\nConnection: close\r\nContent-Length: ";
        response_ += std::to_string(body.size());
        response_ += "\r\n\r\n";
        response_ += body;

        asio::async_write(socket_, asio::buffer(response_),
            [this, self = shared_from_this()](std::error_code, std::size_t) {
                close();
            });
    }

    void close() {
        std::error_code ignored;
        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

    tcp::socket socket_;
    asio::streambuf request_;
    std::shared_ptr<Metrics> metrics_;
    const std::string& token_;
    std::string response_;
};

} // namespace

AdminServer::AdminServer(asio::io_context& io_context, std::string host,
                         unsigned short port, std::shared_ptr<Metrics> metrics,
                         std::string token)
    : acceptor_(io_context),
      metrics_(std::move(metrics)),
      token_(std::move(token)) {
    tcp::resolver resolver(io_context);
    const auto endpoints = resolver.resolve(host, std::to_string(port));
    const auto endpoint = *endpoints.begin();

    acceptor_.open(endpoint.endpoint().protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint.endpoint());
    acceptor_.listen(asio::socket_base::max_listen_connections);
    do_accept();
}

void AdminServer::do_accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<AdminSession>(std::move(socket), metrics_, token_)->start();
        } else {
            std::cerr << "admin accept failed: " << ec.message() << '\n';
        }
        do_accept();
    });
}

} // namespace fluxgate
