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
#include <fmt/color.h>
#include <string>

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
bool enable_color_codes = true;

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

    fmt::text_style style;

    if (enable_color_codes) {
        if (string_icontains(str, "error")) {
            style = fmt::fg(fmt::color::red);
        } else if (string_icontains(str, "warning")) {
            style = fmt::fg(fmt::terminal_color::yellow);
        } else if (bitflags<flag>(logflag) & flag::PERCENT) {
            style = fmt::fg(fmt::terminal_color::blue);
        } else if (bitflags<flag>(logflag) & flag::STAT) {
            style = fmt::fg(fmt::terminal_color::cyan);
        }
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

    if (enable_color_codes) {
        // stdout (assume the terminal can render ANSI colors)
        fmt::print(style, "{}", str);
    } else {
        std::cout << str;
    }

    // for TB, etc...
    fflush(stdout);

    print_mutex.unlock();
}

static time_point start_time;
static bool is_timing = false;
static uint64_t last_count = -1;
static time_point last_indeterminate_time;
static std::atomic_bool locked = false;

void percent(uint64_t count, uint64_t max, bool displayElapsed)
{
    bool expected = false;

    if (!(logging::mask & flag::CLOCK_ELAPSED)) {
        displayElapsed = false;
    }

    if (count == max) {
        while (!locked.compare_exchange_weak(expected, true)) ; // wait until everybody else is done
    } else {
        if (!locked.compare_exchange_weak(expected, true)) {
            return; // somebody else is doing this already
        }
    }

    // we got the lock

    if (!is_timing) {
        start_time = I_FloatTime();
        is_timing = true;
        last_count = -1;
        last_indeterminate_time = {};
    }

    if (count == max) {
        auto elapsed = I_FloatTime() - start_time;
        is_timing = false;
        if (displayElapsed) {
            if (max == indeterminate) {
                print(flag::PERCENT, "[done] time elapsed: {:.3}\n", elapsed);
            } else {
                print(flag::PERCENT, "[100%] time elapsed: {:.3}\n", elapsed);
            }
        }
        last_count = -1;
    } else {
        if (max != indeterminate) {
            uint32_t pct = static_cast<uint32_t>((static_cast<float>(count) / max) * 100);
            if (last_count != pct) {
                print(flag::PERCENT, "[{:>3}%]\r", pct);
                last_count = pct;
            }
        } else {
            auto t = I_FloatTime();

            if (t - last_indeterminate_time > std::chrono::milliseconds(100)) {
                constexpr const char *spinners[] = {
                    ".   ",
                    " .  ",
                    "  . ",
                    "   ."
                };
                last_count = (last_count + 1) >= std::size(spinners) ? 0 : (last_count + 1);
                print(flag::PERCENT, "[{}]\r", spinners[last_count]);
                last_indeterminate_time = t;
            }
        }
    }

    // unlock for next call
    locked = false;
}

void percent_clock::print()
{
    if (!ready) {
        return;
    }

    ready = false;
    
#ifdef _DEBUG
    if (max != indeterminate) {
        if (count != max) {
            logging::print("ERROR TO FIX LATER: clock counter ended too early\n");
        }
    }
#endif

    percent(max, max, displayElapsed);
}
}; // namespace logging
