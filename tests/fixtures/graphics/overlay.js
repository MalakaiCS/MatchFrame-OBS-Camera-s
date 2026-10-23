// Per-page-load identifier: a new value only when the document is actually (re)loaded.
const loadId = Math.random().toString(36).slice(2, 10) + "-" + Date.now().toString(36);
const started = performance.now();
document.getElementById("lid").textContent = loadId;
fetch("/load?id=" + encodeURIComponent(loadId), { method: "POST", cache: "no-store" }).catch(() => {});
function tick() {
  const s = (performance.now() - started) / 1000;
  const m = Math.floor(s / 60), r = (s % 60).toFixed(1).padStart(4, "0");
  document.getElementById("clock").textContent = String(m).padStart(2, "0") + ":" + r;
  document.getElementById("up").textContent = Math.floor(s);
  requestAnimationFrame(tick);
}
tick();
// Heartbeat lets the server show the page is alive without counting it as a load.
setInterval(() => fetch("/beat?id=" + encodeURIComponent(loadId), { method: "POST", cache: "no-store" }).catch(() => {}), 5000);
