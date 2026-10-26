// Torii's GameHacking.org harvester: `just cheats CARD --gamehacking` fills
// in CONFIG and writes this as dist/cheats/gamehacking/harvest.js, for the
// user to paste into the browser console on a gamehacking.org page. It runs
// in the user's own browser session, as the user browsing: one request at a
// time, CONFIG.delay seconds apart (robots.txt's Crawl-delay), never past a
// challenge page. It crawls the GBA index pages it still needs
// (/system/gba/all/N), exports the mGBA cheats of every dump whose CRC32 is
// wanted, and downloads what it got as torii-gamehacking-<time>.json bundles
// (every BUNDLE_EXPORTS exports and at the end) for the tool to import.
//
// parseIndexPage and the helpers above it are pure, so tests/cheats_test.py
// runs them under node; tools/cheats.py has their Python twins.
(function () {
  "use strict";

  const CONFIG = /*TORII_CONFIG*/ null;
  const BUNDLE_EXPORTS = 25;
  const BUNDLE_PAGES = 20;
  const EXPORT_URL = "/inc/sub.exportCodes.php";
  const GBA_SYSTEM = "5";

  const ENTITIES = { amp: "&", lt: "<", gt: ">", quot: '"', apos: "'", nbsp: " " };

  function decodeEntities(s) {
    return s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (all, e) => {
      if (e[0] === "#") {
        const n = e[1] === "x" || e[1] === "X" ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10);
        return Number.isFinite(n) ? String.fromCodePoint(n) : all;
      }
      const v = ENTITIES[e.toLowerCase()];
      return v === undefined ? all : v;
    });
  }

  // A cell's text: tags dropped, entities decoded, spaces collapsed
  function cellText(html) {
    return decodeEntities(html.replace(/<[^>]*>/g, " "))
      .replace(/\s+/g, " ")
      .trim();
  }

  // An index page's dumps and the highest page its pagination links to:
  // {lastPage, rows: [{crc, id, name, title, serial, size, codes}]}, or null
  // when the HTML isn't an index page (a challenge, an error page)
  function parseIndexPage(html) {
    if (!/Number of Codes/i.test(html) || !/<table\b/i.test(html)) return null;
    let lastPage = -1;
    for (const m of html.matchAll(/\/system\/gba\/all\/(\d+)/g)) lastPage = Math.max(lastPage, parseInt(m[1], 10));
    const rows = [];
    let title = "";
    for (const tr of html.matchAll(/<tr\b[^>]*>([\s\S]*?)<\/tr>/gi)) {
      const th = /<th\b[^>]*\bcolspan=["']?8["']?[^>]*>([\s\S]*?)<\/th>/i.exec(tr[1]);
      if (th) {
        title = cellText(th[1]);
        continue;
      }
      const cells = [...tr[1].matchAll(/<td\b[^>]*>([\s\S]*?)<\/td>/gi)].map((c) => c[1]);
      if (cells.length < 5) continue;
      const link = /<a\b[^>]*\bhref=["']\/game\/(\d+)["'][^>]*>([\s\S]*?)<\/a>/i.exec(cells[0]);
      const crc = cellText(cells[3]).toUpperCase();
      if (!link || !/^[0-9A-F]{8}$/.test(crc)) continue;
      rows.push({
        crc,
        id: parseInt(link[1], 10),
        name: cellText(link[2]),
        title,
        serial: cellText(cells[1]),
        size: cellText(cells[2]),
        codes: parseInt(cellText(cells[4]), 10) || 0,
      });
    }
    return { lastPage, rows };
  }

  // The revision a dump's name gives: "(Rev 1)" is 1, none 0 (the ROM
  // header's version byte, 0xBC)
  function revision(name) {
    const m = /\(Rev (\d+)\)/i.exec(name || "");
    return m ? parseInt(m[1], 10) : 0;
  }

  // The index entries a romhack's header (game code at 0xAC, version at
  // 0xBC) points to: serial holds the code, the name's revision is the
  // version. index: {crc: entry}. Sorted CRCs.
  function baseCandidates(index, code, version) {
    const out = [];
    for (const [crc, e] of Object.entries(index)) {
      const serials = (e.serial || "").toUpperCase().split(/[^A-Z0-9]+/);
      if (serials.includes(code.toUpperCase()) && revision(e.name) === version) out.push(crc);
    }
    return out.sort();
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = { decodeEntities, cellText, parseIndexPage, revision, baseCandidates };
  }
  if (typeof document === "undefined" || !CONFIG) return;

  // --- In the browser --------------------------------------------------------------------

  if (location.hostname !== "gamehacking.org") {
    alert("Torii: open https://gamehacking.org/system/gba first, then paste this there.");
    return;
  }
  if (window.__toriiHarvest) {
    alert("Torii: the harvester is already running on this page.");
    return;
  }
  window.__toriiHarvest = true;

  const known = Object.assign({}, CONFIG.index); // crc -> entry, cached and found
  const cached = new Set(CONFIG.cached); // exports the tool has
  const wanted = new Map(CONFIG.wanted.map((w) => [w.crc, w]));
  const pagesDone = new Set(CONFIG.pagesDone);
  let lastPage = CONFIG.lastPage;
  let stopped = null;
  let requests = 0;
  let lastRequest = 0;
  let seq = 0;
  let pending = { index: {}, exports: {}, pages: [] };
  let pendingExports = 0;
  let pendingPages = 0;
  let exported = 0;
  let failed = 0;

  const box = document.createElement("div");
  box.style.cssText =
    "position:fixed;top:12px;right:12px;z-index:2147483647;width:340px;padding:12px;" +
    "background:#111;color:#eee;font:13px/1.4 system-ui,sans-serif;border-radius:8px;" +
    "box-shadow:0 4px 20px rgba(0,0,0,.4)";
  const head = document.createElement("div");
  head.style.cssText = "font-weight:600;margin-bottom:6px";
  head.textContent = "Torii cheats harvester";
  const status = document.createElement("div");
  const detail = document.createElement("div");
  detail.style.cssText = "opacity:.75;margin-top:4px;word-break:break-word";
  const stop = document.createElement("button");
  stop.textContent = "Stop";
  stop.style.cssText = "margin-top:8px";
  stop.onclick = () => {
    stopped = stopped || "stopped by you";
    stop.disabled = true;
  };
  box.append(head, status, detail, stop);
  document.body.append(box);

  function show(s, d) {
    status.textContent = s;
    if (d !== undefined) detail.textContent = d;
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  // Waits out the delay since the last request, a second at a time so Stop
  // answers at once
  async function pace(what) {
    const due = lastRequest + CONFIG.delay * 1000;
    while (!stopped && Date.now() < due) {
      show(`${what} in ${Math.ceil((due - Date.now()) / 1000)} s`);
      await sleep(Math.min(1000, due - Date.now()));
    }
    lastRequest = Date.now();
    requests++;
  }

  function bundleName() {
    const t = new Date().toISOString().replace(/[:.]/g, "-");
    return `torii-gamehacking-${t}-${++seq}.json`;
  }

  // Downloads what was got since the last bundle; done marks the last one
  function flush(done) {
    if (!done && !pendingExports && !pendingPages) return;
    const bundle = {
      format: "torii-gamehacking",
      version: 1,
      run: CONFIG.run,
      id: `${CONFIG.run}-${seq + 1}`,
      done: !!done,
      stopped: done ? stopped : null,
      lastPage,
      pages: pending.pages,
      index: pending.index,
      exports: pending.exports,
    };
    const blob = new Blob([JSON.stringify(bundle)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = bundleName();
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 60000);
    pending = { index: {}, exports: {}, pages: [] };
    pendingExports = 0;
    pendingPages = 0;
  }

  async function crawl(n) {
    await pace(`Index page ${n}`);
    if (stopped) return;
    show(`Index page ${n}${lastPage >= 0 ? ` of ${lastPage}` : ""}`);
    let html;
    try {
      const r = await fetch(`/system/gba/all/${n}`, { credentials: "same-origin" });
      html = await r.text();
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
    } catch (e) {
      stopped = `index page ${n}: ${e.message}`;
      return;
    }
    const page = parseIndexPage(html);
    if (!page) {
      stopped = `index page ${n} isn't the GBA list (a Cloudflare check?): reload the page, pass any check, and run again`;
      return;
    }
    if (page.lastPage > lastPage) lastPage = page.lastPage;
    for (const row of page.rows) {
      if (!known[row.crc]) known[row.crc] = row;
      pending.index[row.crc] = row;
    }
    pagesDone.add(n);
    pending.pages.push(n);
    pendingPages++;
    for (const row of page.rows) if (wanted.has(row.crc)) await exportDump(row.crc);
    if (pendingPages >= BUNDLE_PAGES) flush(false);
  }

  async function exportDump(crc) {
    const e = known[crc];
    if (stopped || !e || cached.has(crc)) return;
    cached.add(crc);
    if (!e.codes) return; // nothing to export
    await pace(`Export ${e.title} ${e.name}`);
    if (stopped) return;
    show(`Exporting ${crc}`, `${e.title} ${e.name}`);
    const body = new URLSearchParams({
      format: "MGBA",
      filename: crc,
      sysID: GBA_SYSTEM,
      gamID: String(e.id),
      download: "true",
    });
    let text, r;
    try {
      r = await fetch(EXPORT_URL, { method: "POST", body, credentials: "same-origin" });
      text = await r.text();
    } catch (err) {
      stopped = `export of ${crc}: ${err.message}`;
      return;
    }
    const disposition = r.headers.get("Content-Disposition") || "";
    if (r.status === 403 || r.status === 503 || /^\s*</.test(text)) {
      stopped = `export of ${crc} answered a page, not cheats (a Cloudflare check?): reload, pass any check, and run again`;
      return;
    }
    if (!r.ok || !/attachment/i.test(disposition)) {
      failed++;
      show(`Export of ${crc} failed (HTTP ${r.status}): skipped`);
      return;
    }
    pending.exports[crc] = text;
    pendingExports++;
    exported++;
    if (pendingExports >= BUNDLE_EXPORTS) flush(false);
  }

  function missing() {
    return [...wanted.keys()].filter((crc) => !known[crc]);
  }

  async function run() {
    // Dumps the cached index already lists
    for (const crc of wanted.keys()) if (known[crc]) await exportDump(crc);
    // The pages still needed: all of them while a wanted CRC is unknown
    if (missing().length && lastPage < 0) await crawl(0);
    for (let n = 0; !stopped && missing().length && n <= lastPage; n++) if (!pagesDone.has(n)) await crawl(n);
    // Romhacks: a CRC no page lists, exported by its base dump when one fits
    const complete = lastPage >= 0 && [...Array(lastPage + 1).keys()].every((n) => pagesDone.has(n));
    if (complete) {
      for (const crc of missing()) {
        const w = wanted.get(crc);
        if (!w.code) continue;
        const bases = baseCandidates(known, w.code, w.version);
        if (bases.length === 1) await exportDump(bases[0]);
      }
    }
    flush(true);
    const how = stopped ? `Stopped: ${stopped}` : "Done.";
    show(
      how,
      `${exported} exported, ${failed} failed, ${requests} requests. The tool imports the downloaded ` +
        "torii-gamehacking-*.json files; run `just cheats` again to go on.",
    );
    stop.textContent = "Close";
    stop.disabled = false;
    stop.onclick = () => box.remove();
    window.__toriiHarvest = false;
  }

  show(`${wanted.size} dumps wanted`, "Keep this tab open. Stop downloads what was got so far.");
  run();
})();
