#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Write dist/compat/index.html from the runs in dist/compat/runs.

usage: tests/compat_report.py

The newest run is the report; for each of its games the previous result
comes from the latest older run that included it, so filtered runs still
show what changed. Screenshots are embedded, so the page is one file.
"""

import base64
import json
from pathlib import Path

REPORT = Path("dist/compat")


def load_runs() -> list[dict]:
    files = sorted((REPORT / "runs").glob("*.json"))
    return [json.loads(f.read_text()) for f in files]


def embed(path: str) -> str:
    data = (REPORT / path).read_bytes() if (REPORT / path).exists() else b""
    return "data:image/png;base64," + base64.b64encode(data).decode() if data else ""


def main() -> int:
    runs = load_runs()
    if not runs:
        print("no runs in dist/compat/runs")
        return 1
    latest = runs[-1]
    previous = {}
    for run in runs[:-1]:
        for game in run["games"]:
            previous[game["key"]] = {
                "status": game["status"],
                "time": run["time"],
                "checks": game["checks"],
            }
    games = []
    for entry in latest["games"]:
        game = dict(entry)
        game["shots"] = [embed(s) for s in game["shots"]]
        game["previous"] = previous.get(game["key"])
        games.append(game)
    data = {
        "listed": latest.get("listed"),
        "left out": latest.get("left out", 0),
        "time": latest["time"],
        "kernel": latest["kernel"],
        "filters": latest["filters"],
        "runs": len(runs),
        "games": games,
    }
    page = TEMPLATE.replace("/*DATA*/null", json.dumps(data).replace("</", "<\\/"))
    (REPORT / "index.html").write_text(page)
    return 0


TEMPLATE = r"""<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Torii compatibility</title>
<style>
:root {
  --bg: #f7f7f5; --panel: #ffffff; --text: #1d1d1f; --muted: #6b6b70; --line: #e4e4e0;
  --pass: #1f8a4c; --partial: #b7791f; --fail: #c8372d; --skip: #8a8a90; --na: #b0b0b6;
  --pass-bg: #e3f4ea; --partial-bg: #fbf0dc; --fail-bg: #fbe5e3; --skip-bg: #ededef;
  --accent: #3a5bd9;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #141416; --panel: #1d1d20; --text: #ececee; --muted: #9a9aa2; --line: #2e2e33;
    --pass: #4cc781; --partial: #e2a948; --fail: #f0675c; --skip: #8e8e96; --na: #5d5d64;
    --pass-bg: #17321f; --partial-bg: #3a2c12; --fail-bg: #3d1a17; --skip-bg: #26262a;
    --accent: #7d97ff;
  }
}
* { box-sizing: border-box; }
body { margin: 0; font: 14px/1.45 system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); }
main { max-width: 1280px; margin: 0 auto; padding: 24px 16px 64px; }
h1 { font-size: 22px; margin: 0 0 4px; }
h2 { font-size: 15px; margin: 28px 0 10px; text-transform: uppercase; letter-spacing: .04em; color: var(--muted); }
.sub { color: var(--muted); }
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin-top: 18px; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; cursor: pointer; }
.card b { display: block; font-size: 26px; }
.card.active { outline: 2px solid var(--accent); }
.bar { display: flex; height: 10px; border-radius: 6px; overflow: hidden; margin-top: 14px; background: var(--line); }
.bar span { display: block; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 14px; overflow-x: auto; }
table { border-collapse: collapse; width: 100%; }
th, td { text-align: left; padding: 7px 8px; border-bottom: 1px solid var(--line); vertical-align: middle; }
th { font-size: 12px; color: var(--muted); font-weight: 600; white-space: nowrap; }
.matrix td { text-align: center; white-space: nowrap; }
.matrix td:first-child, .matrix th:first-child { text-align: left; }
.pill { display: inline-block; padding: 1px 8px; border-radius: 99px; font-size: 12px; font-weight: 600; }
.pill.pass { background: var(--pass-bg); color: var(--pass); } .pill.partial { background: var(--partial-bg); color: var(--partial); }
.pill.fail { background: var(--fail-bg); color: var(--fail); } .pill.skip { background: var(--skip-bg); color: var(--skip); }
.dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; }
.dot.pass { background: var(--pass); } .dot.fail { background: var(--fail); } .dot.untested { background: transparent; border: 2px solid var(--partial); }
.dot.n\/a { background: var(--na); opacity: .35; }
.filters { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 10px; }
.filters input, .filters select { font: inherit; padding: 6px 10px; border-radius: 8px; border: 1px solid var(--line); background: var(--panel); color: var(--text); }
.filters input { flex: 1 1 220px; }
.games tr.game { cursor: pointer; }
.games tr.game:hover { background: color-mix(in srgb, var(--accent) 6%, transparent); }
.title { font-weight: 600; } .file { color: var(--muted); font-size: 12px; }
.thumbs { display: flex; gap: 4px; }
.thumbs img { width: 72px; height: 48px; image-rendering: pixelated; border-radius: 3px; background: #000; }
.details td { background: color-mix(in srgb, var(--accent) 4%, transparent); }
.details img { width: 240px; height: 160px; image-rendering: pixelated; border-radius: 4px; background: #000; }
.details .shots { display: flex; flex-wrap: wrap; gap: 8px; margin: 6px 0 10px; }
.details pre { white-space: pre-wrap; font-size: 12px; max-height: 240px; overflow: auto; background: var(--bg); padding: 8px; border-radius: 6px; }
.change { display: flex; gap: 8px; align-items: center; padding: 4px 0; }
.empty { color: var(--muted); padding: 8px 0; }
.legend { display: flex; gap: 14px; flex-wrap: wrap; color: var(--muted); font-size: 12px; margin-top: 8px; }
.legend span { display: inline-flex; gap: 5px; align-items: center; }
@media (max-width: 900px) { .thumbs img { width: 54px; height: 36px; } .hide-sm { display: none; } }
@media (max-width: 600px) { .thumbs { display: none; } }
</style>
<main>
  <h1>Torii compatibility</h1>
  <div class="sub" id="meta"></div>
  <div class="cards" id="cards"></div>
  <div class="bar" id="bar"></div>

  <h2>Changes since the previous result</h2>
  <div class="panel" id="changes"></div>

  <h2>Coverage</h2>
  <div class="panel"><table class="matrix" id="matrix"></table>
    <div class="legend">Games passing / games tested, then how many listed games those stand for. Partly tested games ran fine but didn't reach every check.</div>
  </div>

  <h2>Games</h2>
  <div class="filters">
    <input id="q" type="search" placeholder="Search title, file or code">
    <select id="system"><option value="">All systems</option><option>gba</option><option>gbc</option><option>gb</option></select>
    <select id="save"><option value="">All save chips</option></select>
    <select id="check"><option value="">Any check</option></select>
  </div>
  <div class="panel"><table class="games">
    <thead><tr><th>Status</th><th>Game</th><th class="hide-sm">System</th><th class="hide-sm">Save</th><th class="hide-sm">Size</th>
      <th title="boots">Boot</th><th title="save type">Type</th><th title="clock">Clock</th><th title="save cycle">Save</th><th>Screens</th></tr></thead>
    <tbody id="rows"></tbody>
  </table>
  <div class="legend"><span><i class="dot pass"></i>pass</span><span><i class="dot fail"></i>fail</span><span><i class="dot untested"></i>untested</span><span><i class="dot n/a"></i>n/a</span></div>
  </div>
</main>
<script>
const DATA = /*DATA*/null;
const STATUSES = ["pass", "partial", "fail", "skip"];
const LABEL = {pass: "Passed", partial: "Partly tested", fail: "Failed", skip: "Skipped"};
const CHECKS = ["boots", "save type", "clock", "save cycle"];
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;"})[c]);
const mb = (n) => n ? (n / 1048576).toFixed(n < 1048576 ? 2 : 0) + " MB" : "";
const sizeClass = (n) => !n ? "?" : n <= 4 << 20 ? "≤ 4 MB" : n <= 8 << 20 ? "8 MB" : n <= 16 << 20 ? "16 MB" : "32 MB";
let statusFilter = "";

const t = DATA.time;
const covered = DATA.games.reduce((n, g) => n + 1 + (g.alike || []).length, 0);
$("meta").textContent = `Run ${t.slice(0, 4)}-${t.slice(4, 6)}-${t.slice(6, 8)} ${t.slice(9, 11)}:${t.slice(11, 13)} UTC · kernel ${DATA.kernel} · ${DATA.games.length} games tested, standing for ${covered}` +
  (DATA.listed ? ` of ${DATA.listed} listed (${DATA["left out"]} marked skip: ROM hacks, plugins, test ROMs)` : "") +
  (DATA.filters.length ? ` · filter: ${DATA.filters.join(", ")}` : "") + ` · ${DATA.runs} run(s) recorded`;

const counts = Object.fromEntries(STATUSES.map((s) => [s, DATA.games.filter((g) => g.status === s).length]));
$("cards").innerHTML = STATUSES.map((s) => `<div class="card" data-status="${s}"><span class="pill ${s}">${LABEL[s]}</span><b>${counts[s]}</b></div>`).join("");
$("bar").innerHTML = STATUSES.map((s) => `<span style="width:${100 * counts[s] / DATA.games.length}%;background:var(--${s})" title="${LABEL[s]}: ${counts[s]}"></span>`).join("");
document.querySelectorAll(".card").forEach((card) => card.onclick = () => {
  statusFilter = statusFilter === card.dataset.status ? "" : card.dataset.status;
  document.querySelectorAll(".card").forEach((c) => c.classList.toggle("active", c.dataset.status === statusFilter));
  render();
});

const rank = {fail: 0, partial: 1, skip: 2, pass: 3};
const changed = DATA.games.filter((g) => g.previous && g.previous.status !== g.status);
const fresh = DATA.games.filter((g) => !g.previous).length;
$("changes").innerHTML = (changed.length ? changed.sort((a, b) => rank[a.status] - rank[b.status]).map((g) =>
  `<div class="change">${rank[g.status] < rank[g.previous.status] ? "▼" : "▲"} <span class="pill ${g.previous.status}">${LABEL[g.previous.status]}</span> → <span class="pill ${g.status}">${LABEL[g.status]}</span> <span class="title">${esc(g.title || g.file)}</span> <span class="file">${esc(g.file)}</span></div>`).join("")
  : `<div class="empty">No game changed status since its previous result.</div>`) +
  (fresh ? `<div class="empty">${fresh} game(s) have no earlier result.</div>` : "");

function matrixRows(label, key, values) {
  const tested = DATA.games.filter((g) => g.status !== "skip");
  return `<tr><th colspan="99">${label}</th></tr>` + values.map((v) => {
    const set = tested.filter((g) => key(g) === v);
    if (!set.length) return "";
    const pass = set.filter((g) => g.status === "pass").length, fail = set.filter((g) => g.status === "fail").length;
    const pct = Math.round(100 * pass / set.length);
    const stands = set.reduce((n, g) => n + 1 + (g.alike || []).length, 0);
    return `<tr><td>${esc(v)}</td><td><b>${pass}</b> / ${set.length}</td><td class="file">${stands} games</td><td><div class="bar" style="margin:0;width:160px"><span style="width:${pct}%;background:var(--pass)"></span><span style="width:${100 * (set.length - pass - fail) / set.length}%;background:var(--partial)"></span><span style="width:${100 * fail / set.length}%;background:var(--fail)"></span></div></td><td>${fail ? `<span class="pill fail">${fail} failing</span>` : ""}</td></tr>`;
  }).join("");
}
const saves = [...new Set(DATA.games.map((g) => g.save))].sort();
$("matrix").innerHTML = matrixRows("Save chip", (g) => g.save, saves) +
  matrixRows("System", (g) => g.system, ["gba", "gbc", "gb"]) +
  matrixRows("ROM size", (g) => sizeClass(g.size), ["≤ 4 MB", "8 MB", "16 MB", "32 MB"]) +
  matrixRows("Clock and other hardware", (g) => g.hardware.length ? "other hardware (not emulated)" : g.rtc ? "uses the clock" : "neither", ["uses the clock", "other hardware (not emulated)", "neither"]);

$("save").innerHTML += saves.map((s) => `<option>${esc(s)}</option>`).join("");
$("check").innerHTML += CHECKS.map((c) => `<option value="${c}">${c} failed</option>`).join("");

function row(g) {
  const dots = CHECKS.map((c) => { const [s, d] = g.checks[c]; return `<td><i class="dot ${s}" title="${esc(c)}: ${s}${d ? " — " + esc(d) : ""}"></i></td>`; }).join("");
  return `<tr class="game" data-key="${g.key}"><td><span class="pill ${g.status}">${LABEL[g.status]}</span></td>
    <td><div class="title">${esc(g.title || g.file)}${(g.alike || []).length ? ` <span class="pill skip" title="${esc(g.alike.join("\n"))}">+${g.alike.length} alike</span>` : ""}</div><div class="file">${esc(g.file)} · ${esc(g.code)}</div></td>
    <td class="hide-sm">${g.system}</td><td class="hide-sm">${esc(g.save)}${g.rtc ? " · clock" : ""}</td><td class="hide-sm">${mb(g.size)}</td>
    ${dots}<td><div class="thumbs">${g.shots.map((s) => s ? `<img src="${s}" alt="">` : "").join("")}</div></td></tr>`;
}
function details(g) {
  const checks = CHECKS.map((c) => `<li><i class="dot ${g.checks[c][0]}"></i> <b>${c}</b>: ${g.checks[c][0]}${g.checks[c][1] ? " — " + esc(g.checks[c][1]) : ""}</li>`).join("");
  const metrics = Object.entries(g.metrics).map(([k, v]) => `${esc(k)}: ${esc(k === "size" ? mb(v) : v)}`).join(" · ");
  const prev = g.previous ? `Previous result (${g.previous.time}): <span class="pill ${g.previous.status}">${LABEL[g.previous.status]}</span>` : "No earlier result.";
  return `<tr class="details"><td colspan="10">
    ${g.skipped ? `<p>${esc(g.skipped)}</p>` : ""}
    <div class="shots">${g.shots.map((s) => s ? `<img src="${s}" alt="">` : "").join("")}</div>
    <ul>${checks}</ul>
    <p><b>Requirements:</b> ${esc(g.requirements || "")}${(g.alike || []).length ? `<br><b>Stands for:</b> ${g.alike.map(esc).join(", ")}` : ""}</p>
    <p class="file">${metrics}${g.notes.length ? "<br>" + g.notes.map(esc).join(" · ") : ""}${g.hardware.length ? "" : ""}<br>${prev}</p>
    ${g.trace ? `<pre>${esc(g.trace)}</pre>` : ""}</td></tr>`;
}
function render() {
  const q = $("q").value.toLowerCase(), sys = $("system").value, save = $("save").value, check = $("check").value;
  const shown = DATA.games.filter((g) => (!statusFilter || g.status === statusFilter) && (!sys || g.system === sys) && (!save || g.save === save) &&
    (!check || g.checks[check][0] === "fail") && (!q || `${g.title} ${g.file} ${g.code}`.toLowerCase().includes(q)))
    .sort((a, b) => rank[a.status] - rank[b.status] || (a.title || a.file).localeCompare(b.title || b.file));
  $("rows").innerHTML = shown.map(row).join("") || `<tr><td colspan="10" class="empty">No games match.</td></tr>`;
  document.querySelectorAll("tr.game").forEach((tr) => tr.onclick = () => {
    const next = tr.nextElementSibling;
    if (next && next.classList.contains("details")) return next.remove();
    tr.insertAdjacentHTML("afterend", details(DATA.games.find((g) => g.key === tr.dataset.key)));
  });
}
["q", "system", "save", "check"].forEach((id) => $(id).addEventListener("input", render));
render();
</script>
</html>
"""

if __name__ == "__main__":
    raise SystemExit(main())
