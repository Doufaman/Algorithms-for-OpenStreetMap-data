#pragma once
#include <string>

namespace Server {

// Loads JSON data files once at startup, then serves HTTP on port 8080.
// Routes:
//   GET /                          → index.html
//   GET /map.js                    → map.js
//   GET /style.css                 → style.css
//   GET /api/points?bbox=w,s,e,n&limit=N
//   GET /api/lines?bbox=w,s,e,n&limit=N
//   GET /api/admin?bbox=w,s,e,n
void run(const std::string& data_dir, const std::string& web_dir, int port = 8080);

} // namespace Server
