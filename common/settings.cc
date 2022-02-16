/*  Copyright (C) 2016 Eric Wasylishen

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

#include "common/settings.hh"
#include "common/threads.hh"

namespace settings
{
    // global settings
	static settings_group performance_group { "Performance", 10 };
    lockable_int32 threads { "threads", 0, &performance_group, "number of threads to use, maximum; leave 0 for automatic" };

	static settings_group logging_group { "Logging", 5 };
	lockable_bool verbose { strings { "verbose", "v" }, false, &logging_group, "verbose output" };
    lockable_bool quiet { strings { "quiet", "noverbose" }, false, &logging_group, "suppress non-important output" };
    lockable_bool nopercent { "nopercent", false, &logging_group, "don't output percentage messages" };

    // global settings dict, used by all tools
    dict globalSettings { &threads, &verbose, &quiet, &nopercent };

	[[noreturn]] void dict::printHelp()
	{
		fmt::print("usage: {} [-help/-h/-?] [-options] {}\n\n", programName, remainderName);

		for (auto grouped : grouped()) {
			if (grouped.first) {
				fmt::print("{}:\n", grouped.first->name);
			}

			for (auto setting : grouped.second) {
				size_t numPadding = max(static_cast<size_t>(0), 28 - (strlen(setting->primaryName()) + 4));
				fmt::print("  -{} {:{}}{}\n", setting->primaryName(), setting->format(), numPadding, setting->getDescription());

				for (int i = 1; i < setting->names().size(); i++) {
					fmt::print("  |{}\n", setting->names()[i]);
				}
			}

			printf("\n");
		}

		exit(0);
	}

	void dict::printSummary()
	{
		for (auto setting : _settings) {
			if (setting->isChanged()) {
				LogPrint("    \"{}\" was set to \"{}\" (from {})\n", setting->primaryName(), setting->stringValue(),
					setting->sourceString());
			}
		}
	}

	std::vector<std::string> dict::parse(parser_base_t &parser)
	{
		// the settings parser loop will continuously eat tokens as long as
		// it begins with a -; once we have no more settings to consume, we
		// break out of this loop and return the remainder.
		while (true)
		{
			// end of cmd line
			if (!parser.parse_token(PARSE_PEEK)) {
				break;
			}

			// end of options
			if (parser.token[0] != '-') {
				break;
			}

			// actually eat the token since we peeked above
			parser.parse_token();

			// remove leading hyphens. we support any number of them.
			while (parser.token.front() == '-') {
				parser.token.erase(parser.token.begin());
			}

			if (parser.token.empty()) {
				throw parse_exception("stray \"-\" in command line; please check your parameters");
			}

			if (parser.token == "help" || parser.token == "h" || parser.token == "?") {
				printHelp();
			}

			auto setting = findSetting(parser.token);

			if (!setting) {
				throw parse_exception(fmt::format("unknown option \"{}\"", parser.token));
			}

			// pass off to setting to parse; store
			// name for error message below
			std::string token = std::move(parser.token);

			if (!setting->parse(parser, true)) {
				throw parse_exception(fmt::format("invalid value for option \"{}\"; should be in format {}", token, setting->format()));
			}
		}

		// return remainder
		std::vector<std::string> remainder;

		while (true) {
			if (parser.at_end() || !parser.parse_token()) {
				break;
			}

			remainder.emplace_back(std::move(parser.token));
		}

		return remainder;
	}

	void initGlobalSettings()
	{
		configureTBB(threads.numberValue());

		if (verbose.boolValue()) {
			log_mask |= 1 << LOG_VERBOSE;
		}

		if (nopercent.boolValue()) {
			log_mask &= ~(1 << LOG_PERCENT);
		}
		
		if (quiet.boolValue()) {
			log_mask &= ~((1 << LOG_PERCENT) | (1 << LOG_STAT) | (1 << LOG_PROGRESS));
		}
	}
}