// SPDX-License-Identifier: GPL-3.0-or-later
// The console's logs: kept across runs, and sent to a computer on the local
// network when one is named.
//
//   sdmc:/3ds/d2d/d2d.log        this run (the game starts it afresh)
//   sdmc:/3ds/d2d/d2d-prev.log   the run before (a crash's log is here on the next start)
//   sdmc:/3ds/d2d/d2d-prev2.log  the one before that
//
// Upload: put the server's address in sdmc:/3ds/d2d/logurl.txt, e.g.
//   http://192.168.1.10:8099/log
// (platform/3ds/tools/devserver.py is that server). At each start the two
// earlier runs' logs are POSTed there, and this run's when the game is left
// by its own exit. Nothing is sent without that file; a build can name a
// default with -DD2D_LOG_URL=... for test builds.
#include <3ds.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string g_url;
std::string g_notes;   // what the uploads did, for the game's log once it is open

void note(const std::string& line) { g_notes += line + "\n"; }

std::vector<char> read_tail(const char* path, std::size_t most) {
    std::vector<char> data;
    if (std::FILE* file = std::fopen(path, "rb")) {
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        const long from = size > long(most) ? size - long(most) : 0;   // the end, if it's long: what happened last is what's wanted
        std::fseek(file, from, SEEK_SET);
        data.resize(std::size_t(size - from));
        if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) data.clear();
        std::fclose(file);
    }
    return data;
}

void post(const char* path, const char* name) {
    auto data = read_tail(path, 768 * 1024);
    if (data.empty()) return;
    data.resize((data.size() + 3) & ~std::size_t(3), '\n');          // the service takes whole words
    const std::string url = g_url + (g_url.find('?') == std::string::npos ? "?name=" : "&name=") + name;
    Result rc = 0;
    u32 status = 0;
    for (int attempt = 0; attempt < 3 && status != 200; ++attempt) {
        httpcContext context;
        rc = httpcOpenContext(&context, HTTPC_METHOD_POST, url.c_str(), 0);
        status = 0;
        if (R_SUCCEEDED(rc)) {
            httpcAddRequestHeaderField(&context, "Content-Type", "text/plain");
            httpcAddPostDataRaw(&context, reinterpret_cast<const u32*>(data.data()), data.size());
            rc = httpcBeginRequest(&context);
            if (R_SUCCEEDED(rc)) rc = httpcGetResponseStatusCodeTimeout(&context, &status, 20000000000ull);   // a long log over the console's Wi-Fi
            httpcCloseContext(&context);
        }
    }
    char line[320];
    std::snprintf(line, sizeof line, "log upload: %s (%lu bytes) to %s: %08lX, HTTP %lu", name, static_cast<unsigned long>(data.size()), url.c_str(),
                  static_cast<unsigned long>(rc), static_cast<unsigned long>(status));
    note(line);
}

bool upload(std::initializer_list<std::pair<const char*, const char*>> files) {
    if (g_url.empty()) return false;
    // The http service holds a POST's body in memory shared with it.
    if (R_FAILED(httpcInit(1 << 20))) { note("log upload: no http service"); return false; }
    for (const auto& [path, name] : files) post(path, name);
    httpcExit();
    return true;
}

}  // namespace

// Before the game opens its log: the earlier runs' logs move down one and go to the server.
void logs_3ds_start() {
    std::remove("sdmc:/3ds/d2d/d2d-prev2.log");
    std::rename("sdmc:/3ds/d2d/d2d-prev.log", "sdmc:/3ds/d2d/d2d-prev2.log");
    std::rename("sdmc:/3ds/d2d/d2d.log", "sdmc:/3ds/d2d/d2d-prev.log");
    if (std::ifstream file("sdmc:/3ds/d2d/logurl.txt"); file) {
        std::getline(file, g_url);
        while (!g_url.empty() && (g_url.back() == '\r' || g_url.back() == ' ')) g_url.pop_back();
    }
#ifdef D2D_LOG_URL
    if (g_url.empty()) g_url = D2D_LOG_URL;
#endif
    if (g_url.rfind("http://", 0) != 0) g_url.clear();              // the local network: plain http only
    upload({ { "sdmc:/3ds/d2d/d2d-prev2.log", "prev2" }, { "sdmc:/3ds/d2d/d2d-prev.log", "prev" } });
}

// What the start's uploads did, for the game's log (it wasn't open then).
const char* logs_3ds_notes() { return g_notes.c_str(); }

// Leaving by the game's own exit: this run's log now.
void logs_3ds_exit() {
    upload({ { "sdmc:/3ds/d2d/d2d.log", "exit" } });
}
