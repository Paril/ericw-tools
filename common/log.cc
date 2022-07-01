/*  Copyright (C) 2000-2001  Kevin Shanahan

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

/*
 * common/log.c
 */

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <fmt/ostream.h>
#include <fmt/chrono.h>

#include <common/log.hh>
#include <common/threads.hh>
#include <common/settings.hh>
#include <common/cmdlib.hh>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // for OutputDebugStringA
#endif

static std::ofstream logfile;

namespace logging
{
bitflags<flag> mask = bitflags<flag>(flag::ALL) & ~bitflags<flag>(flag::VERBOSE);

void init(const fs::path &filename, const settings::common_settings &settings)
{
    if (settings.log.value()) {
        logfile.open(filename);
        fmt::print(logfile, "---- {} / ericw-tools {} ----\n", settings.programName, ERICWTOOLS_VERSION);
    }
}

void close()
{
    if (logfile) {
        logfile.close();
    }
}

static std::mutex print_mutex;

void print(flag logflag, const char *str)
{
    if (!(mask & logflag)) {
        return;
    }

    static const char *escape_red = "\033[31m";
    static const char *escape_yellow = "\033[33m";
    static const char *escape_reset = "\033[0m";

    std::string ansi_str;
    if (string_icontains(str, "error")) {
        ansi_str = fmt::format("{}{}{}", escape_red, str, escape_reset);
    } else if (string_icontains(str, "warning")) {
        ansi_str = fmt::format("{}{}{}", escape_yellow, str, escape_reset);
    } else {
        ansi_str = str;
    }

    print_mutex.lock();
 
    if (logflag != flag::PERCENT) {
        // log file, if open
        if (logfile) {
            logfile << str;
            logfile.flush();
        }

#ifdef _WIN32
        // print to windows console.
        // if VS's Output window gets support for ANSI colors, we can change this to ansi_str.c_str()
        OutputDebugStringA(str);
#endif
    }

    // stdout (assume the termaial can render ANSI colors)
    std::cout << ansi_str;

    print_mutex.unlock();
}

static time_point start_time;
static bool is_timing = false;
static uint64_t last_count = -1;

void percent(uint64_t count, uint64_t max, bool displayElapsed)
{
    if (!is_timing) {
        start_time = I_FloatTime();
        is_timing = true;
        last_count = -1;
    }

    if (count == max) {
        auto elapsed = I_FloatTime() - start_time;
        is_timing = false;
        if (displayElapsed) {
            print(flag::PERCENT, "[100%] time elapsed: {:.3}\n", elapsed);
        }
        last_count = -1;
    } else {
        uint32_t pct = static_cast<uint32_t>((static_cast<float>(count) / max) * 100);
        if (last_count != pct) {
            print(flag::PERCENT, "[{:>3}%]\r", pct);
            last_count = pct;
        }
    }
}
};
