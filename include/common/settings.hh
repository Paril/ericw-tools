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

#pragma once

#include <common/entdata.h>
#include <common/log.hh>
#include <common/qvec.hh>
#include <common/parser.hh>

#include <vector>
#include <string>
#include <cassert>
#include <sstream>
#include <map>
#include <limits>

namespace settings
{
	struct parse_exception : public std::exception
	{
	private:
		std::string _what;

	public:
		parse_exception(std::string str) :
			_what(std::move(str))
		{
		}

		const char *what() const override { return _what.c_str(); }
	};

    enum class source
    {
        DEFAULT = 0,
        MAP = 1,
        COMMANDLINE = 2
    };

    using strings = std::vector<const char *>;

    class lockable_base
    {
    protected:
        source _source;
        strings _names;

        inline lockable_base(const strings &names) : _source(source::DEFAULT), _names(names)
        {
            Q_assert(_names.size() > 0);
        }

        constexpr bool changeSource(source newSource)
        {
            if (newSource >= _source) {
                _source = newSource;
                return true;
            }
            return false;
        }

    public:
        inline const char *primaryName() const { return _names.at(0); }
        inline const strings &names() const { return _names; }

        virtual bool parse(parser_base_t &parser, bool locked = false) = 0;
        virtual std::string stringValue() const = 0;
        virtual std::string format() const = 0;

        constexpr bool isChanged() const { return _source != source::DEFAULT; }
        constexpr bool isLocked() const { return _source == source::COMMANDLINE; }

        constexpr const char *sourceString() const
        {
            switch (_source) {
                case source::DEFAULT: return "default";
                case source::MAP: return "map";
                case source::COMMANDLINE: return "commandline";
                default: FError("Error: unknown setting source");
            }
        }
    };

    class lockable_bool : public lockable_base
    {
    private:
        bool _value;

        inline void setBoolValueInternal(bool f, source newsource)
        {
            if (changeSource(newsource)) {
                _value = f;
            }
        }

    public:
        inline void setBoolValueLocked(bool f) { setBoolValueInternal(f, source::COMMANDLINE); }

        inline void setBoolValue(bool f) { setBoolValueInternal(f, source::MAP); }

        constexpr bool boolValue() const { return _value; }

        virtual bool parse(parser_base_t &parser, bool locked = false) override
        {
            // boolean flags can be just flagged themselves 
            if (parser.parse_token(PARSE_PEEK)) {
                // if the token that follows is 1, 0 or -1, we'll handle it
                // as a value, otherwise it's probably part of the next option.
                if (parser.token == "1" || parser.token == "0" || parser.token == "-1") {
                    parser.parse_token();

                    int intval = std::stoi(parser.token);

                    const bool f = (intval != 0 && intval != -1); // treat 0 and -1 as false

                    if (locked)
                        setBoolValueLocked(f);
                    else
                        setBoolValue(f);

                    return true;
                }
            }

            if (locked) {
                setBoolValueLocked(true);
            } else {
                setBoolValue(true);
            }

            return true;
        }

        virtual std::string stringValue() const { return _value ? "1" : "0"; }

        virtual std::string format() const { return "[0|1]"; }

        inline lockable_bool(const strings &names, bool v) : lockable_base(names), _value(v) { }

        inline lockable_bool(const char *name, bool v) : lockable_bool(strings{name}, v) { }
    };

    class lockable_scalar : public lockable_base
    {
    private:
        vec_t _value, _min, _max;

        inline void setFloatInternal(vec_t f, source newsource)
        {
            if (changeSource(newsource)) {
                if (f < _min) {
                    LogPrint("WARNING: '{}': {} is less than minimum value {}.\n", primaryName(), f, _min);
                    f = _min;
                }
                if (f > _max) {
                    LogPrint("WARNING: '{}': {} is greater than maximum value {}.\n", primaryName(), f, _max);
                    f = _max;
                }
                _value = f;
            }
        }

    public:
        constexpr bool boolValue() const
        {
            // we use -1 to mean false
            return intValue() == 1;
        }

        constexpr int intValue() const { return static_cast<int>(_value); }

        constexpr const vec_t &floatValue() const { return _value; }

        inline void setFloatValue(vec_t f) { setFloatInternal(f, source::MAP); }

        inline void setFloatValueLocked(vec_t f) { setFloatInternal(f, source::COMMANDLINE); }

        virtual bool parse(parser_base_t &parser, bool locked = false) override
        {
            if (!parser.parse_token()) {
                return false;
            }

            try {
                vec_t f = std::stod(parser.token);

                if (locked)
                    setFloatValueLocked(f);
                else
                    setFloatValue(f);

                return true;
            }
            catch (std::exception &) {
                return false;
            }
        }

        virtual std::string stringValue() const { return std::to_string(_value); }

        virtual std::string format() const { return "n"; }

        inline lockable_scalar(strings names, vec_t v, vec_t minval = -std::numeric_limits<vec_t>::infinity(),
            vec_t maxval = std::numeric_limits<vec_t>::infinity())
            : lockable_base(names), _value(v), _min(minval), _max(maxval)
        {
            // check the default value is valid
            Q_assert(_min < _max);
            Q_assert(_value >= _min);
            Q_assert(_value <= _max);
        }

        inline lockable_scalar(const char *name, vec_t v, vec_t minval = -std::numeric_limits<vec_t>::infinity(),
            vec_t maxval = std::numeric_limits<vec_t>::infinity())
            : lockable_scalar(strings{name}, v, minval, maxval)
        {
        }
    };

    class lockable_string : public lockable_base
    {
    private:
        std::string _value;

    public:
        virtual bool parse(parser_base_t &parser, bool locked = false) override
        {
            // peek the first token, if it was
            // a quoted string we can exit now
            if (!parser.parse_token(PARSE_PEEK)) {
                return false;
            }

            if (parser.was_quoted) {
                parser.parse_token();

                if (changeSource(locked ? source::COMMANDLINE : source::MAP)) {
                    _value = std::move(parser.token);
                }

                return true;
            }

            std::string value;

            // not a quoted string, so everything will be literal.
            // go until we reach a -.
            while (true) {
                if (parser.token[0] == '-') {
                    break;
                }

                if (!value.empty()) {
                    value += ' ';
                }

                value += parser.token;

                parser.parse_token();

                if (!parser.parse_token(PARSE_PEEK)) {
                    break;
                }
            }

		    while (std::isspace(value.back())) {
			    value.pop_back();
		    }
		    while (std::isspace(value.front())) {
			    value.erase(value.begin());
		    }

            if (changeSource(locked ? source::COMMANDLINE : source::MAP)) {
                _value = std::move(value);
            }

            return true;
        }

        virtual std::string stringValue() const { return _value; }

        virtual std::string format() const { return "\"str\""; }

        inline lockable_string(strings names, std::string v) : lockable_base(names), _value(v)
        {
        }

        inline lockable_string(const char *name, std::string v) : lockable_string(strings{name}, v) { }
    };

    class lockable_vec3 : public lockable_base
    {
    private:
        qvec3d _value;

        inline void transformAndSetVec3Value(const qvec3d &val, source newsource)
        {
            if (changeSource(newsource)) {
                transformVec3Value(val, _value);
            }
        }

    protected:
        virtual void transformVec3Value(const qvec3d &val, qvec3d &out) const
        {
            out = val;
        }

    public:
        inline lockable_vec3(
            strings names, vec_t a, vec_t b, vec_t c)
            : lockable_base(names)
        {
            transformVec3Value({a, b, c}, _value);
        }

        inline lockable_vec3(const char *name, vec_t a, vec_t b, vec_t c)
            : lockable_vec3(strings{name}, a, b, c)
        {
        }

        const qvec3d &vec3Value() const { return _value; }

        inline void setVec3Value(const qvec3d &val) { transformAndSetVec3Value(val, source::MAP); }

        inline void setVec3ValueLocked(const qvec3d &val) { transformAndSetVec3Value(val, source::COMMANDLINE); }

        virtual bool parse(parser_base_t &parser, bool locked = false) override
        {
            qvec3d vec;

            for (int i = 0; i < 3; i++) {
                if (!parser.parse_token()) {
                    return false;
                }

                try {
                    vec[i] = std::stod(parser.token);
                } catch (std::exception &) {
                    return false;
                }
            }

            if (locked)
                setVec3ValueLocked(vec);
            else
                setVec3Value(vec);

            return true;
        }

        virtual std::string stringValue() const { return qv::to_string(_value); }

        virtual std::string format() const { return "x y z"; }
    };

    class lockable_mangle : public lockable_vec3
    {
    protected:
        virtual void transformVec3Value(const qvec3d &val, qvec3d &out) const
        {
            out = qv::vec_from_mangle(val);
        }

    public:
        using lockable_vec3::lockable_vec3;
    };

    class lockable_color : public lockable_vec3
    {
    protected:
        virtual void transformVec3Value(const qvec3d &val, qvec3d &out) const
        {
            out = qv::normalize_color_format(val);
        }

    public:
        using lockable_vec3::lockable_vec3;
    };

    // settings dictionary

    class dict
    {
    private:
        std::map<std::string, lockable_base *> _settingsmap;
        std::set<lockable_base *> _settings;

    public:
        std::string programName, remainderName = "filename";

        inline dict(const std::initializer_list<lockable_base *> &settings)
        {
            addSettings(settings);
        }

        inline void addSettings(const std::initializer_list<lockable_base *> &settings)
        {
            for (lockable_base *setting : settings) {
                for (const auto &name : setting->names()) {
                    Q_assert(_settingsmap.find(name) == _settingsmap.end());
                    _settingsmap.emplace(name, setting);
                }

                _settings.emplace(setting);
            }
        }

        inline lockable_base *findSetting(const std::string &name) const
        {
            // strip off leading underscores
            if (name.find("_") == 0) {
                return findSetting(name.substr(1, name.size() - 1));
            }

            if (auto it = _settingsmap.find(name); it != _settingsmap.end()) {
                return it->second;
            }
            
            return nullptr;
        }

        inline void setSetting(const std::string &name, const std::string &value, bool locked)
        {
            lockable_base *setting = findSetting(name);

            if (setting == nullptr) {
                if (locked) {
                    throw parse_exception(fmt::format("Unrecognized command-line option '{}'\n", name));
                }
                return;
            }

            setting->parse(parser_t { value }, locked);
        }

        inline void setSettings(const entdict_t &epairs, bool locked)
        {
            for (const auto &epair : epairs) {
                setSetting(epair.first, epair.second, locked);
            }
        }
        
        inline auto begin() { return _settings.begin(); }
        inline auto end() { return _settings.end(); }

        inline auto begin() const { return _settings.begin(); }
        inline auto end() const { return _settings.end(); }

        [[noreturn]] void printHelp();
        void printSummary();

        /**
         * Parse options from the input parser. The parsing
         * process is fairly tolerant, and will only really
         * fail hard if absolutely necessary. The remainder
         * of the command line is returned (anything not
         * eaten by the options).
         */
        std::vector<std::string> parse(parser_base_t &parser);
    };
};