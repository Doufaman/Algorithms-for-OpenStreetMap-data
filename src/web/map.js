// ============================================================
//  OSM Geocoder – Leaflet frontend
//  Queries the C++ backend on every map moveend.
// ============================================================

const API = '';   // same origin – backend serves this file

// ── Layer style config ───────────────────────────────────────
const STYLE = {
    point: {
        radius:      4,
        fillColor:   '#4af0b0',
        color:       '#0d3d2a',
        weight:      1,
        opacity:     1,
        fillOpacity: 0.85,
    },
    line: {
        color:   '#f0b44a',
        weight:  1.5,
        opacity: 0.75,
    },
    admin: {
        color:       '#7a9ef0',
        weight:      1.5,
        opacity:     0.8,
        fillOpacity: 0.05,
        fillColor:   '#7a9ef0',
        dashArray:   '5 4',
    },
};

// ── Initialise map ───────────────────────────────────────────
const map = L.map('map', {
    center:    [48.78, 9.18],   // Stuttgart
    zoom:      14,
    zoomControl: false,
    attributionControl: false,
});

L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '© OpenStreetMap',
}).addTo(map);

L.control.zoom({ position: 'bottomright' }).addTo(map);

// ── Data layers ──────────────────────────────────────────────
const pointLayer = L.geoJSON(null, {
    pointToLayer: (f, latlng) => L.circleMarker(latlng, STYLE.point),
    onEachFeature: (f, layer) => layer.on('click', () => showInfo(f)),
}).addTo(map);

const lineLayer = L.geoJSON(null, {
    style: () => STYLE.line,
    onEachFeature: (f, layer) => layer.on('click', () => showInfo(f)),
}).addTo(map);

const adminLayer = L.geoJSON(null, {
    style: () => STYLE.admin,
    onEachFeature: (f, layer) => layer.on('click', () => showInfo(f)),
});

// ── Toggle checkboxes ────────────────────────────────────────
document.getElementById('cb-points').addEventListener('change', e => {
    e.target.checked ? pointLayer.addTo(map) : map.removeLayer(pointLayer);
});
document.getElementById('cb-lines').addEventListener('change', e => {
    e.target.checked ? lineLayer.addTo(map) : map.removeLayer(lineLayer);
});
document.getElementById('cb-admin').addEventListener('change', e => {
    e.target.checked ? adminLayer.addTo(map) : map.removeLayer(adminLayer);
    if (e.target.checked) refresh();
});

// ── Status helpers ───────────────────────────────────────────
const statusEl = document.getElementById('status-text');
const zoomEl   = document.getElementById('zoom-display');

function setStatus(msg) { statusEl.textContent = msg; }

map.on('zoomend moveend', () => {
    zoomEl.textContent = `zoom ${map.getZoom()}`;
});

// ── Build bbox string ────────────────────────────────────────
function bboxParam() {
    const b = map.getBounds();
    return `${b.getWest().toFixed(6)},${b.getSouth().toFixed(6)},` +
           `${b.getEast().toFixed(6)},${b.getNorth().toFixed(6)}`;
}

// ── Main refresh – fires on every moveend ────────────────────
let refreshTimer = null;

async function refresh() {
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(_doRefresh, 120);   // debounce 120 ms
}

async function _doRefresh() {
    const zoom  = map.getZoom();
    const bbox  = bboxParam();
    const limit = document.getElementById('sel-limit').value;
    const showPoints = document.getElementById('cb-points').checked;
    const showLines  = document.getElementById('cb-lines').checked;
    const showAdmin  = document.getElementById('cb-admin').checked;

    setStatus('Loading …');

    const fetches = [];

    // Points: only at zoom ≥ 13
    if (showPoints && zoom >= 13) {
        fetches.push(
            fetch(`${API}/api/points?bbox=${bbox}&limit=${limit}`)
                .then(r => r.json())
                .then(gj => { pointLayer.clearLayers(); pointLayer.addData(gj); })
                .catch(() => {})
        );
    } else if (showPoints) {
        pointLayer.clearLayers();
    }

    // Lines: zoom ≥ 11
    if (showLines && zoom >= 11) {
        fetches.push(
            fetch(`${API}/api/lines?bbox=${bbox}&limit=${limit}`)
                .then(r => r.json())
                .then(gj => { lineLayer.clearLayers(); lineLayer.addData(gj); })
                .catch(() => {})
        );
    } else if (showLines) {
        lineLayer.clearLayers();
    }

    // Admin: always when toggled on
    if (showAdmin) {
        fetches.push(
            fetch(`${API}/api/admin?bbox=${bbox}`)
                .then(r => r.json())
                .then(gj => { adminLayer.clearLayers(); adminLayer.addData(gj); })
                .catch(() => {})
        );
    }

    await Promise.all(fetches);

    const pc = pointLayer.getLayers().length;
    const lc = lineLayer.getLayers().length;
    const ac = adminLayer.getLayers().length;
    setStatus(`Showing ${pc} points · ${lc} lines · ${ac} admin areas`);
}

// ── Info popup ───────────────────────────────────────────────
function showInfo(feature) {
    const p   = feature.properties || {};
    const pop = document.getElementById('info-popup');
    const con = document.getElementById('info-content');

    const geomType = feature.geometry?.type || '';
    const rows = [
        ['id',          p.id],
        ['name',        p.name],
        ['type',        p.type],
        ['street',      p.street],
        ['housenumber', p.housenumber],
        ['postcode',    p.postcode],
        ['admin level', p.admin_level],
        ['ref',         p.ref],
    ].filter(([, v]) => v !== undefined && v !== null && v !== '');

    con.innerHTML =
        `<div class="info-type">${geomType.toUpperCase()}</div>` +
        rows.map(([k, v]) =>
            `<div class="info-row">
               <span class="info-key">${k}</span>
               <span class="info-val">${v}</span>
             </div>`
        ).join('');

    pop.classList.remove('hidden');
}

document.getElementById('info-close').addEventListener('click', () => {
    document.getElementById('info-popup').classList.add('hidden');
});

// ── Load global stats from /api/stats ───────────────────────
async function loadStats() {
    try {
        const s = await fetch(`${API}/api/stats`).then(r => r.json());
        document.getElementById('val-points').textContent =
            s.points.toLocaleString();
        document.getElementById('val-lines').textContent =
            s.lines.toLocaleString();
        document.getElementById('val-admin').textContent =
            s.admin.toLocaleString();
    } catch(_) {}
}

// ── Boot ─────────────────────────────────────────────────────
(async () => {
    // Show loading overlay
    const overlay = document.createElement('div');
    overlay.id = 'loading-overlay';
    overlay.innerHTML = '<div class="spinner"></div><span>Connecting to backend …</span>';
    document.body.appendChild(overlay);

    await loadStats();

    // Fade out overlay
    overlay.classList.add('fade');
    setTimeout(() => overlay.remove(), 400);

    // Initial data load
    zoomEl.textContent = `zoom ${map.getZoom()}`;
    await refresh();

    // Attach moveend
    map.on('moveend', refresh);
    document.getElementById('sel-limit').addEventListener('change', refresh);
})();
