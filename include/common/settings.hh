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
        source _source = source::DEFAULT;
        strings _names;
        const char *_group, *_description;

        inline lockable_base(const strings &names, const char *group, const char *description) : _names(names), _group(group), _description(description)
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
        inline const char *getGroup() const { return _group; }
        inline const char *getDescription() const { return _description; }

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
        inline lockable_bool(const strings &names, bool v, const char *group = "", const char *description = "") : lockable_base(names, group, description), _value(v) { }

        inline lockable_bool(const char *name, bool v, const char *group = "", const char *description = "") : lockable_bool(strings{name}, v, group, description) { }

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
    };

    template<typename T>
    class lockable_numeric : public lockable_base
    {
    private:
        T _value, _min, _max;

        inline void setValueInternal(T f, source newsource)
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
        constexpr const T &numberValue() const { return _value; }

        inline bool boolValue() const { return _value > 0; }

        inline void setNumberValue(T f) { setValueInternal(f, source::MAP); }

        inline void setNumberValueLocked(T f) { setValueInternal(f, source::COMMANDLINE); }

        virtual bool parse(parser_base_t &parser, bool locked = false) override
        {
            if (!parser.parse_token()) {
                return false;
            }

            try {
                T f;
                
                if constexpr (std::is_floating_point_v<T>) {
                    f = std::stod(parser.token);
                } else {
                    f = static_cast<T>(std::stoull(parser.token));
                }

                if (locked)
                    setNumberValueLocked(f);
                else
                    setNumberValue(f);

                return true;
            }
            catch (std::exception &) {
                return false;
            }
        }

        virtual std::string stringValue() const { return std::to_string(_value); }

        virtual std::string format() const { return "n"; }

        inline lockable_numeric(strings names, T v, T minval, T maxval, const char *group = "", const char *description = "")
            : lockable_base(names, group, description), _value(v), _min(minval), _max(maxval)
        {
            // check the default value is valid
            Q_assert(_min < _max);
            Q_assert(_value >= _min);
            Q_assert(_value <= _max);
        }

        template<typename = std::enable_if_t<!std::is_enum_v<T>>>
        inline lockable_numeric(strings names, T v, const char *group = "", const char *description = "")
            : lockable_numeric(names, v, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), group, description)
        {
        }

        inline lockable_numeric(const char *name, T v, T minval, T maxval, const char *group = "", const char *description = "")
            : lockable_numeric(strings{name}, v, minval, maxval, group, description)
        {
        }
        
        template<typename = std::enable_if_t<!std::is_enum_v<T>>>
        inline lockable_numeric(const char *name, T v, const char *group = "", const char *description = "")
            : lockable_numeric(strings{name}, v, group, description)
        {
        }
    };
    
    using lockable_scalar = lockable_numeric<vec_t>;
    using lockable_int32 = lockable_numeric<int32_t>;

    class lockable_string : public lockable_base
    {
    private:
        std::string _value;
        std::string _format;

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

        virtual std::string format() const { return _format; }

        inline lockable_string(strings names, std::string v, const std::string_view &format = "\"str\"", const char *group = "", const char *description = "") : lockable_base(names, group, description), _value(v), _format(format)
        {
        }

        inline lockable_string(const char *name, std::string v, const std::string_view &format = "\"str\"", const char *group = "", const char *description = "") : lockable_string(strings{name}, v, format, group, description) { }
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
            strings names, vec_t a, vec_t b, vec_t c, const char *group = "", const char *description = "")
            : lockable_base(names, group, description)
        {
            transformVec3Value({a, b, c}, _value);
        }

        inline lockable_vec3(const char *name, vec_t a, vec_t b, vec_t c, const char *group = "", const char *description = "")
            : lockable_vec3(strings{name}, a, b, c, group, description)
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
        std::map<std::string, std::set<lockable_base *>> _groupedSettings;

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
                _groupedSettings[setting->getGroup()].insert(setting);
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

        inline auto grouped() const { return _groupedSettings; }

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

    // global settings
    extern lockable_int32 threads;
    extern lockable_bool verbose;
    extern lockable_bool quiet;
    extern lockable_bool nopercent;

    // global settings dict, used by all tools
    extern dict globalSettings;

    // initialize stuff from the settings above
    void initGlobalSettings();
};