#pragma once
#include <string>

namespace Server {

// Serves ONE parsed dataset (data_root/<dataset>/) via HTTP.
//
//   data_root : parent directory containing all dataset subdirs
//               (used by /api/datasets to enumerate what's available)
//   dataset   : name of the dataset to load and serve
//
// Routes:
//   GET /                          → index.html
//   GET /map.js                    → map.js
//   GET /style.css                 → style.css
//   GET /api/points?bbox=w,s,e,n&limit=N
//   GET /api/lines?bbox=w,s,e,n&limit=N
//   GET /api/admin?bbox=w,s,e,n
//   GET /api/reverse?lat=..&lon=..&zoom=..
//   GET /api/search?q=..&limit=..
//   GET /api/stats                 → point/line/admin counts
//   GET /api/info                  → current dataset + counts
//   GET /api/datasets              → { current, available[] }
void run(const std::string& data_root, const std::string& dataset,
         const std::string& web_dir,   int port = 8080);

} // namespace Server
