// Copyright 2026 Alex Tsvetanov
// SPDX-License-Identifier: Apache-2.0
//
// The end-to-end benchmark server: one binary, the arm chosen at run time (design note section 3).
//
// Usage: tep_server --arm R|P [--port N] [--cpus C[,C...]] [--max-streams N]
//   --cpus   one worker thread per CPU, pinned to it (the lab's server cores); default one unpinned
// It prints one line when it listens, and exits cleanly on SIGTERM or SIGINT (Linux), or on a
// console Ctrl+C, Ctrl+Break or close event (Windows, which raises no SIGTERM).
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "h2_server.hpp"

namespace {

std::atomic<e2e::server*> running{nullptr};

extern "C" void on_signal(int) {
    if (e2e::server* s = running.load(); s != nullptr) s->stop();
}

#ifdef _WIN32
// Runs on a thread the system creates for the event; stop() only sets a flag and wakes the workers.
BOOL WINAPI on_console_event(DWORD) {
    on_signal(0);
    return TRUE;
}
#endif

int usage() {
    std::fprintf(stderr, "usage: tep_server --arm R|P [--port N] [--cpus C[,C...]] [--max-streams N]\n");
    return 2;
}

bool parse_cpus(std::string_view list, std::vector<int>& out) {
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string item(list.substr(0, comma));
        char* end = nullptr;
        const long cpu = std::strtol(item.c_str(), &end, 10);
        if (item.empty() || *end != '\0' || cpu < 0 || cpu > 1023) return false;
        out.push_back(static_cast<int>(cpu));
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return !out.empty();
}

}  // namespace

int main(int argc, char** argv) {
    e2e::server_options options;
    std::string_view arm;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (i + 1 >= argc) return usage();
        const std::string_view v = argv[++i];
        if (a == "--arm") {
            arm = v;
        } else if (a == "--port") {
            const long port = std::strtol(std::string(v).c_str(), nullptr, 10);
            if (port < 0 || port > 65535) return usage();
            options.port = static_cast<std::uint16_t>(port);
        } else if (a == "--cpus") {
            if (!parse_cpus(v, options.cpus)) return usage();
        } else if (a == "--max-streams") {
            const long n = std::strtol(std::string(v).c_str(), nullptr, 10);
            if (n < 1 || n > 1'000'000) return usage();
            options.max_concurrent_streams = static_cast<std::uint32_t>(n);
        } else {
            return usage();
        }
    }
    if (arm == "R") {
        options.make_arm = e2e::make_reflection_arm;
    } else if (arm == "P") {
        options.make_arm = e2e::make_protoc_arm;
    } else {
        return usage();
    }
    try {
        e2e::server s(options);
        running.store(&s);
#ifdef _WIN32
        if (::SetConsoleCtrlHandler(on_console_event, TRUE) == 0)
            std::fprintf(stderr, "tep_server: SetConsoleCtrlHandler failed (%lu)\n", ::GetLastError());
#else
        std::signal(SIGTERM, on_signal);
        std::signal(SIGINT, on_signal);
#endif
        std::printf("tep_server: arm %s, port %u, %zu worker(s)\n", std::string(arm).c_str(),
                    static_cast<unsigned>(s.port()), options.cpus.empty() ? std::size_t{1} : options.cpus.size());
        std::fflush(stdout);
        s.run();
        running.store(nullptr);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tep_server: %s\n", e.what());
        return 1;
    }
    return 0;
}
