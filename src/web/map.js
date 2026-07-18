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

// ── Multi-dataset: current name + available list ─────────────
async function loadDatasetInfo() {
    try {
        const info = await fetch(`${API}/api/info`).then(r => r.json());
        document.getElementById('dataset-val').textContent = info.dataset || '—';
        document.title = `OSM Geocoder · ${info.dataset}`;
    } catch(_) {}
    try {
        const d = await fetch(`${API}/api/datasets`).then(r => r.json());
        renderDatasetMenu(d.current, d.available || []);
    } catch(_) {}
}

function renderDatasetMenu(current, available) {
    const menu = document.getElementById('dataset-menu');
    if (!available.length) {
        menu.innerHTML =
            '<div class="dataset-menu-hint">No datasets on disk</div>';
        return;
    }

    // "all" pseudo-entry on top, then each individual dataset.
    const isAllCurrent = (current === 'all');
    const entries =
        [{ name: 'all', label: `all  (${available.length} datasets merged)`,
           current: isAllCurrent }]
        .concat(available.map(name => ({
            name, label: name, current: !isAllCurrent && name === current,
        })));

    menu.innerHTML =
        entries.map(e => {
            const cls = e.current ? 'current' : 'other';
            return `<div class="dataset-menu-item ${cls}" data-name="${e.name}">${e.label}</div>`;
        }).join('') +
        `<div class="dataset-menu-hint">
           To switch: restart with<br/>
           <code>./OSM_Geocoder serve &lt;name|all&gt;</code><br/>
           (click an entry to copy its command)
         </div>`;

    // Click on a non-current item copies the restart command to clipboard
    menu.querySelectorAll('.dataset-menu-item.other').forEach(el => {
        el.addEventListener('click', ev => {
            ev.stopPropagation();
            const cmd = `./OSM_Geocoder serve ${el.dataset.name}`;
            navigator.clipboard && navigator.clipboard.writeText(cmd);
            const orig = el.textContent;
            el.textContent = '✓ command copied';
            setTimeout(() => el.textContent = orig, 1200);
        });
    });
}

// Dropdown toggle
document.getElementById('dataset-chip').addEventListener('click', ev => {
    ev.stopPropagation();
    document.getElementById('dataset-menu').classList.toggle('hidden');
});
document.addEventListener('click', () => {
    document.getElementById('dataset-menu').classList.add('hidden');
});

// ── Reverse geocoder (Sheet 2 Task 3+4) ──────────────────────
const reverseLayer = L.layerGroup().addTo(map);

async function reverseLookup(lat, lon) {
    const zoom = map.getZoom();
    setStatus('Reverse-geocoding …');
    try {
        const r = await fetch(`${API}/api/reverse?lat=${lat}&lon=${lon}&zoom=${zoom}`)
                          .then(r => r.json());
        showReverseResult(r, [lat, lon]);
    } catch (e) {
        setStatus('Reverse lookup failed');
    }
}

function showReverseResult(geo, clickLatLng) {
    reverseLayer.clearLayers();

    // Red pin at click point
    const pin = L.circleMarker(clickLatLng, {
        radius: 6, color: '#ff4060', weight: 2, fillColor: '#ff4060', fillOpacity: 0.9
    });
    pin.addTo(reverseLayer);

    if (!geo.features || geo.features.length === 0) {
        setStatus('No object found near the click');
        return;
    }

    const f = geo.features[0];
    const p = f.properties || {};
    const kind = p.object_kind || 'unknown';

    // Highlight the object on the map
    const geomLayer = L.geoJSON(f, {
        pointToLayer: (_, latlng) => L.circleMarker(latlng, {
            radius: 8, color: '#4af0b0', weight: 3,
            fillColor: '#4af0b0', fillOpacity: 0.85
        }),
        style: () => ({ color: '#4af0b0', weight: 3, fillOpacity: 0.15 })
    });
    geomLayer.addTo(reverseLayer);

    // If we got a point geometry, draw a dashed connector
    if (f.geometry.type === 'Point') {
        const [olon, olat] = f.geometry.coordinates;
        L.polyline([clickLatLng, [olat, olon]],
                   { color: '#ff4060', weight: 1.5, dashArray: '4 4' })
            .addTo(reverseLayer);
    }

    // Status & popup
    const addr = [p.suburb, p.city, p.state, p.country]
                    .filter(v => v && v.length).join(', ');
    setStatus(`${kind.toUpperCase()}: ${p.name || '(no name)'} · ${addr}`);
    showReverseInfo(p, f.geometry.type);
}

function showReverseInfo(p, geomType) {
    const pop = document.getElementById('info-popup');
    const con = document.getElementById('info-content');

    // Object-level fields: only show non-empty rows (e.g. a city polygon
    // has no housenumber, no need for a blank row).
    const objectRows = [
        ['object kind', p.object_kind],
        ['name',        p.name],
        ['id',          p.id],
        ['admin level', p.admin_level],
        ['type',        p.type],
        ['street',      p.street],
        ['housenumber', p.housenumber],
        ['postcode',    p.postcode],
        ['distance',    p.distance_m != null ? p.distance_m.toFixed(1) + ' m' : null],
    ].filter(([k, v]) => v !== undefined && v !== null && v !== '' && v !== 'null');

    // Admin chain: ALWAYS show all 4 tiers, even if empty. Missing values
    // render as "—" so users know the data simply wasn't available.
    const dash = '—';
    const adminRows = [
        ['suburb',      p.suburb  || dash],
        ['city',        p.city    || dash],
        ['state',       p.state   || dash],
        ['country',     p.country || dash],
    ];

    const renderRow = ([k, v]) =>
        `<div class="info-row">
           <span class="info-key">${k}</span>
           <span class="info-val">${v}</span>
         </div>`;

    con.innerHTML =
        `<div class="info-type">REVERSE · ${geomType.toUpperCase()}</div>` +
        objectRows.map(renderRow).join('') +
        `<div class="info-row" style="opacity:0.5"><span class="info-key">——</span><span class="info-val">admin chain</span></div>` +
        adminRows.map(renderRow).join('');
    pop.classList.remove('hidden');
}

// Click anywhere on the map → reverse geocode at that point
map.on('click', e => {
    reverseLookup(e.latlng.lat, e.latlng.lng);
});

// ── Forward geocoder (Sheet 3 Task 2) ────────────────────────
const searchLayer    = L.layerGroup().addTo(map);
const highlightLayer = L.layerGroup().addTo(map);   // overlay for selected result

// Persistent state — features returned by the last search + which
// one is currently highlighted.
const searchState = { feats: [], selected: -1 };

async function runSearch() {
    const q = document.getElementById('search-input').value.trim();
    if (!q) return;
    const panel = document.getElementById('search-panel');
    const list  = document.getElementById('search-list');
    const summary = document.getElementById('search-summary');
    summary.textContent = 'Searching …';
    list.innerHTML = '';
    panel.classList.remove('hidden');

    // Clear anything left over from a previous reverse-geocode click.
    // The reverse result is stale as soon as the user asks something new.
    reverseLayer.clearLayers();
    document.getElementById('info-popup').classList.add('hidden');

    try {
        const r = await fetch(`${API}/api/search?q=${encodeURIComponent(q)}&limit=20`)
                        .then(r => r.json());
        renderSearchResults(r);
    } catch(e) {
        summary.textContent = 'Search failed';
    }
}

function renderSearchResults(gj) {
    const list    = document.getElementById('search-list');
    const summary = document.getElementById('search-summary');
    searchLayer.clearLayers();
    highlightLayer.clearLayers();
    list.innerHTML = '';
    searchState.feats    = [];
    searchState.selected = -1;

    const feats = gj.features || [];
    const total = gj.returned != null ? gj.returned : feats.length;
    const t = gj.query_ms != null ? ` · ${gj.query_ms.toFixed(1)} ms` : '';
    summary.textContent = `${feats.length} results${t}`;

    if (!feats.length) {
        list.innerHTML = '<li style="color:var(--text-dim);text-align:center">No matches</li>';
        return;
    }

    // Populate the sidebar only — the map layer is drawn lazily by
    // selectResult() for the currently focused item ONLY.  This avoids
    // painting 20 overlapping circles that look like "leftover highlights".
    feats.forEach((f, i) => {
        const p = f.properties || {};
        const kind = p.kind || 'unknown';

        const li = document.createElement('li');
        const addr = [p.city, p.state, p.country].filter(v => v && v.length).join(', ');
        const streetLine = p.street
            ? `${p.street}${p.housenumber ? ' ' + p.housenumber : ''}`
            : '';
        const badge = (p.score != null)
            ? `<span class="result-score">${p.score.toFixed(0)}</span>`
            : '';
        li.innerHTML =
            `<div class="result-kind">
               ${kind.toUpperCase()}${p.admin_level ? ' L'+p.admin_level : ''}
               ${badge}
             </div>` +
            `<div class="result-name">${p.name || streetLine || '(no name)'}</div>` +
            (streetLine && p.name ? `<div class="result-meta">${streetLine}</div>` : '') +
            (addr ? `<div class="result-meta">${addr}</div>` : '') +
            (p.reason ? `<div class="result-reason">${p.reason}</div>` : '');
        li.onclick = () => selectResult(i);
        list.appendChild(li);
        searchState.feats.push(f);
    });

    // Auto-select the first result (draws it + focuses map on it).
    if (feats.length) selectResult(0);
}

// ── Selection: exactly one result is rendered on the map at a time ──
//
// Anything previously on searchLayer / highlightLayer is wiped, then
// the newly-selected feature is drawn in two layers:
//   • a kind-coloured base (green/orange/blue)
//   • a bright-red overlay so it stands out from the base tile POIs
//
function selectResult(idx) {
    if (idx < 0 || idx >= searchState.feats.length) return;
    searchState.selected = idx;

    // Sidebar active state
    document.querySelectorAll('#search-list li').forEach((el, i) => {
        el.classList.toggle('active', i === idx);
    });

    // ── Wipe previous graphics — this is what fixes "residual highlight" ──
    searchLayer.clearLayers();
    highlightLayer.clearLayers();

    const f = searchState.feats[idx];
    const p = f.properties || {};
    const kind = p.kind || 'unknown';
    const geomType = f.geometry && f.geometry.type;

    // Per-kind base styling
    const kindStyle = {
        poi   : { color:'#4af0b0', fillColor:'#4af0b0' },
        street: { color:'#f0b44a' },
        admin : { color:'#7a9ef0', fillColor:'#7a9ef0', fillOpacity:0.10, dashArray:'4 4' },
    }[kind] || { color:'#e2e6f0' };

    // ── Base (kind-coloured) drawing of the selected feature ──
    L.geoJSON(f, {
        style: () => ({ weight: 3, opacity: 0.9, fillOpacity: 0.15, ...kindStyle }),
        pointToLayer: (_, latlng) => L.circleMarker(latlng, {
            radius: 9, weight: 3, opacity: 1.0,
            fillOpacity: 0.85, ...kindStyle
        }),
    }).addTo(searchLayer);

    // ── Accented highlight overlay so it stands out from base POIs ──
    if (geomType === 'Point') {
        const [lon, lat] = f.geometry.coordinates;
        L.circleMarker([lat, lon], {
            radius: 16, weight: 3,
            color: '#ff4060', fillColor: '#ff4060',
            fillOpacity: 0.30, opacity: 1.0,
        }).addTo(highlightLayer);
    } else if (geomType === 'LineString') {
        L.geoJSON(f, {
            style: () => ({ color: '#ff4060', weight: 6, opacity: 1.0 })
        }).addTo(highlightLayer);
    } else if (geomType === 'Polygon' || geomType === 'MultiPolygon') {
        L.geoJSON(f, {
            style: () => ({
                color: '#ff4060', weight: 4, opacity: 1.0, fill: false,
            })
        }).addTo(highlightLayer);
    }

    focusFeature(f);
}

function focusFeature(f) {
    if (!f.geometry) return;
    const g = f.geometry;
    if (g.type === 'Point') {
        const [lon, lat] = g.coordinates;
        map.setView([lat, lon], Math.max(map.getZoom(), 17));
    } else {
        const bounds = L.geoJSON(f).getBounds();
        if (bounds.isValid()) map.fitBounds(bounds, { padding: [40, 40] });
    }
}

document.getElementById('search-btn').addEventListener('click', runSearch);
document.getElementById('search-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') runSearch();
});
document.getElementById('search-close').addEventListener('click', () => {
    document.getElementById('search-panel').classList.add('hidden');
    searchLayer.clearLayers();
    highlightLayer.clearLayers();
    searchState.feats    = [];
    searchState.selected = -1;
});

// ── Boot ─────────────────────────────────────────────────────
(async () => {
    // Show loading overlay
    const overlay = document.createElement('div');
    overlay.id = 'loading-overlay';
    overlay.innerHTML = '<div class="spinner"></div><span>Connecting to backend …</span>';
    document.body.appendChild(overlay);

    await loadStats();
    await loadDatasetInfo();

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
