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
#include <set>
#include <limits>
#include <optional>

namespace settings
{
struct parse_exception : public std::exception
{
private:
    std::string _what;

public:
    parse_exception(std::string str) : _what(std::move(str)) { }

    const char *what() const override { return _what.c_str(); }
};

enum class source
{
    DEFAULT = 0,
    MAP = 1,
    COMMANDLINE = 2
};

using strings = std::vector<std::string>;

struct settings_group
{
    const char *name;
    const int32_t order;
};

class dict;

// base class for any lockable setting
class lockable_base
{
protected:
    source _source = source::DEFAULT;
    strings _names;
    const settings_group *_group;
    const char *_description;

    lockable_base(dict *dictionary, const strings &names, const settings_group *group, const char *description);

    inline lockable_base(dict *dictionary, const char *name, const settings_group *group, const char *description)
        : lockable_base(dictionary, strings{name}, group, description)
    {
    }

    constexpr bool changeSource(source newSource)
    {
        if (newSource >= _source) {
            _source = newSource;
            return true;
        }
        return false;
    }

    // convenience function for parsing a whole string
    inline std::optional<std::string> parseString(parser_base_t &parser)
    {
        // peek the first token, if it was
        // a quoted string we can exit now
        if (!parser.parse_token(PARSE_PEEK)) {
            return std::nullopt;
        }

        if (parser.was_quoted) {
            parser.parse_token();
            return parser.token;
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

        return std::move(value);
    }

public:
    inline const std::string &primaryName() const { return _names.at(0); }
    inline const strings &names() const { return _names; }
    inline const settings_group *getGroup() const { return _group; }
    inline const char *getDescription() const { return _description; }

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

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) = 0;
    virtual std::string stringValue() const = 0;
    virtual std::string format() const = 0;
};

// a special type of lockable that acts as a flag but
// calls back to a function to actually do the tasks.
// be careful because this won't show up in summary.
class lockable_func : public lockable_base
{
protected:
    std::function<void()> _func;

public:
    inline lockable_func(
        dict *dictionary, const strings &names, std::function<void()> func, const settings_group *group = nullptr, const char *description = "")
        : lockable_base(dictionary, names, group, description), _func(func)
    {
    }

    inline lockable_func(
        dict *dictionary, const char *name, std::function<void()> func, const settings_group *group = nullptr, const char *description = "")
        : lockable_func(dictionary, strings{name}, func, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        _func();
        return true;
    }

    virtual std::string stringValue() const override { return ""; }

    virtual std::string format() const override { return ""; }
};

// base class for a lockable setting that has its own value
template<typename T>
class lockable_value : public lockable_base
{
protected:
    T _value;

    virtual void setValueInternal(T value, source newSource)
    {
        if (changeSource(newSource)) {
            _value = value;
        }
    }

    inline void setValueFromParse(T value, bool locked)
    {
        if (locked) {
            setValueLocked(value);
        } else {
            setValue(value);
        }
    }

public:
    inline lockable_value(
        dict *dictionary, const strings &names, T v, const settings_group *group = nullptr, const char *description = "")
        : lockable_base(dictionary, names, group, description), _value(v)
    {
    }

    inline lockable_value(dict *dictionary, const char *name, T v, const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, strings{name}, v, group, description)
    {
    }

    const T &value() const { return _value; }

    inline void setValueLocked(T f) { setValueInternal(f, source::COMMANDLINE); }

    inline void setValue(T f) { setValueInternal(f, source::MAP); }
};

class lockable_bool : public lockable_value<bool>
{
private:
    bool _default;

protected:
    bool parseInternal(parser_base_t &parser, bool locked, bool truthValue)
    {
        // boolean flags can be just flagged themselves
        if (parser.parse_token(PARSE_PEEK)) {
            // if the token that follows is 1, 0 or -1, we'll handle it
            // as a value, otherwise it's probably part of the next option.
            if (parser.token == "1" || parser.token == "0" || parser.token == "-1") {
                parser.parse_token();

                int intval = std::stoi(parser.token);

                const bool f = (intval != 0 && intval != -1) ? truthValue : !truthValue; // treat 0 and -1 as false

                setValueFromParse(f, locked);

                return true;
            }
        }

        setValueFromParse(truthValue, locked);

        return true;
    }

public:
    inline lockable_bool(
        dict *dictionary, const strings &names, bool v, const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, names, v, group, description), _default(v)
    {
    }

    inline lockable_bool(dict *dictionary, const char *name, bool v, const settings_group *group = nullptr, const char *description = "")
        : lockable_bool(dictionary, strings{name}, v, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, true);
    }

    virtual std::string stringValue() const override { return _value ? "1" : "0"; }

    virtual std::string format() const override { return _default ? "[0]" : ""; }
};

// an extension to lockable_bool; this automatically adds "no" versions
// to the list, and will allow them to be used to act as `-name 0`.
class lockable_invertable_bool : public lockable_bool
{
private:
    strings extendNames(const strings &names)
    {
        strings n = names;

        for (auto &name : names) {
            n.push_back("no" + name);
        }

        return n;
    }

public:
    inline lockable_invertable_bool(
        dict *dictionary, const strings &names, bool v, const settings_group *group = nullptr, const char *description = "")
        : lockable_bool(dictionary, extendNames(names), v, group, description)
    {
    }

    inline lockable_invertable_bool(
        dict *dictionary, const char *name, bool v, const settings_group *group = nullptr, const char *description = "")
        : lockable_invertable_bool(dictionary, strings{name}, v, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, settingName.compare(0, 2, "no") == 0 ? false : true);
    }
};

class lockable_redirect : public lockable_base
{
private:
    std::vector<lockable_base *> _settings;

public:
    inline lockable_redirect(dict *dictionary, const strings &names, const std::initializer_list<lockable_base *> &settings,
        const settings_group *group = nullptr, const char *description = "")
        : lockable_base(dictionary, names, group, description), _settings(settings)
    {
    }

    inline lockable_redirect(dict *dictionary, const char *name, const std::initializer_list<lockable_base *> &settings,
        const settings_group *group = nullptr, const char *description = "")
        : lockable_redirect(dictionary, strings{name}, settings, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        // this is a bit ugly, but we run the parse function for
        // every setting that we redirect from. for every entry
        // except the last, we'll backup & restore the state.
        for (size_t i = 0; i < _settings.size(); i++) {
            if (i != _settings.size() - 1) {
                parser.push_state();
            }

            if (!_settings[i]->parse(settingName, parser, locked)) {
                return false;
            }

            if (i != _settings.size() - 1) {
                parser.pop_state();
            }
        }

        return true;
    }

    virtual std::string stringValue() const override { return _settings[0]->stringValue(); }

    virtual std::string format() const override { return _settings[0]->format(); }
};

template<typename T>
class lockable_numeric : public lockable_value<T>
{
protected:
    T _min, _max;

    virtual void setValueInternal(T f, source newsource) override
    {
        if (f < _min) {
            LogPrint("WARNING: '{}': {} is less than minimum value {}.\n", primaryName(), f, _min);
            f = _min;
        }
        if (f > _max) {
            LogPrint("WARNING: '{}': {} is greater than maximum value {}.\n", primaryName(), f, _max);
            f = _max;
        }

        lockable_value::setValueInternal(f, newsource);
    }

public:
    inline lockable_numeric(
        dict *dictionary, strings names, T v, T minval, T maxval, const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, names, v, group, description), _min(minval), _max(maxval)
    {
        // check the default value is valid
        Q_assert(_min < _max);
        Q_assert(_value >= _min);
        Q_assert(_value <= _max);
    }

    inline lockable_numeric(
        dict *dictionary, const char *name, T v, T minval, T maxval, const settings_group *group = nullptr, const char *description = "")
        : lockable_numeric(dictionary, strings{name}, v, minval, maxval, group, description)
    {
    }

    template<typename = std::enable_if_t<!std::is_enum_v<T>>>
    inline lockable_numeric(dict *dictionary, strings names, T v, const settings_group *group = nullptr, const char *description = "")
        : lockable_numeric(
              dictionary, names, v, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), group, description)
    {
    }

    template<typename = std::enable_if_t<!std::is_enum_v<T>>>
    inline lockable_numeric(dict *dictionary, const char *name, T v, const settings_group *group = nullptr, const char *description = "")
        : lockable_numeric(dictionary, strings{name}, v, group, description)
    {
    }

    template<typename = std::enable_if_t<!std::is_enum_v<T>>>
    inline bool boolValue() const
    {
        return _value > 0;
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
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

            setValueFromParse(f, locked);

            return true;
        }
        catch (std::exception &) {
            return false;
        }
    }

    virtual std::string stringValue() const override { return std::to_string(_value); }

    virtual std::string format() const override { return "n"; }
};

using lockable_scalar = lockable_numeric<vec_t>;
using lockable_int32 = lockable_numeric<int32_t>;

template<typename T>
class lockable_enum : public lockable_value<T>
{
private:
    std::map<std::string, T, natural_less> _values;

public:
    inline lockable_enum(dict *dictionary, strings names, T v, const std::initializer_list<std::pair<const char *, T>> &enumValues,
        const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, names, v, group, description), _values(enumValues.begin(), enumValues.end())
    {
    }

    inline lockable_enum(dict *dictionary, const char *name, T v, const std::initializer_list<std::pair<const char *, T>> &enumValues,
        const settings_group *group = nullptr, const char *description = "")
        : lockable_enum(dictionary, strings{name}, v, enumValues, group, description)
    {
    }

    virtual std::string stringValue() const override
    {
        for (auto &value : _values) {
            if (value.second == _value) {
                return value.first;
            }
        }

        throw std::exception();
    }

    virtual std::string format() const override
    {
        std::string f;

        for (auto &value : _values) {
            if (!f.empty()) {
                f += " | ";
            }

            f += value.first;
        }

        return f;
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        if (auto it = _values.find(parser.token); it != _values.end()) {
            setValueFromParse(it->second, locked);
            return true;
        }

        return false;
    }
};

class lockable_string : public lockable_value<std::string>
{
private:
    std::string _format;

public:
    inline lockable_string(dict *dictionary, strings names, std::string v, const std::string_view &format = "\"str\"",
        const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, names, v, group, description), _format(format)
    {
    }

    inline lockable_string(dict *dictionary, const char *name, std::string v, const std::string_view &format = "\"str\"",
        const settings_group *group = nullptr, const char *description = "")
        : lockable_string(dictionary, strings{name}, v, format, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (auto value = parseString(parser)) {
            setValueFromParse(std::move(*value), locked);
            return true;
        }

        return false;
    }

    [[deprecated("use value()")]] virtual std::string stringValue() const override { return _value; }

    virtual std::string format() const override { return _format; }
};

class lockable_vec3 : public lockable_value<qvec3d>
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const { return val; }

    virtual void setValueInternal(qvec3d f, source newsource) override
    {
        lockable_value::setValueInternal(transformVec3Value(f), newsource);
    }

public:
    inline lockable_vec3(
        dict *dictionary, strings names, vec_t a, vec_t b, vec_t c, const settings_group *group = nullptr, const char *description = "")
        : lockable_value(dictionary, names, transformVec3Value({a, b, c}), group, description)
    {
    }

    inline lockable_vec3(dict *dictionary, const char *name, vec_t a, vec_t b, vec_t c, const settings_group *group = nullptr,
        const char *description = "")
        : lockable_vec3(dictionary, strings{name}, a, b, c, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        qvec3d vec;

        for (int i = 0; i < 3; i++) {
            if (!parser.parse_token()) {
                return false;
            }

            try {
                vec[i] = std::stod(parser.token);
            }
            catch (std::exception &) {
                return false;
            }
        }

        setValueFromParse(vec, locked);

        return true;
    }

    virtual std::string stringValue() const override { return qv::to_string(_value); }

    virtual std::string format() const override { return "x y z"; }
};

class lockable_mangle : public lockable_vec3
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const override { return qv::vec_from_mangle(val); }

public:
    using lockable_vec3::lockable_vec3;
};

class lockable_color : public lockable_vec3
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const override { return qv::normalize_color_format(val); }

public:
    using lockable_vec3::lockable_vec3;
};

// settings dictionary

class dict
{
    struct settings_less
    {
        constexpr bool operator()(const settings_group *a, const settings_group *b) const
        {
            int32_t a_order = a ? a->order : std::numeric_limits<int32_t>::min();
            int32_t b_order = b ? b->order : std::numeric_limits<int32_t>::min();

            return a_order < b_order;
        }
    };

    std::map<std::string, lockable_base *> _settingsmap;
    std::set<lockable_base *> _settings;
    std::map<const settings_group *, std::set<lockable_base *>, settings_less> _groupedSettings;

public:
    std::string programName, remainderName = "filename", usage;

    inline void registerSetting(lockable_base *setting)
    {
        for (const auto &name : setting->names()) {
            Q_assert(_settingsmap.find(name) == _settingsmap.end());
            _settingsmap.emplace(name, setting);
        }

        _settings.emplace(setting);
        _groupedSettings[setting->getGroup()].insert(setting);
    }

    template<typename TIt>
    inline void registerSettings(TIt begin, TIt end)
    {
        for (auto it = begin; it != end; it++) {
            registerSetting(*it);
        }
    }

    inline void registerSettings(const std::initializer_list<lockable_base *> &settings)
    {
        registerSettings(settings.begin(), settings.end());
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

        setting->parse(name, parser_t{value}, locked);
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

// global groups
extern settings_group performance_group, logging_group;

class common_settings : public virtual dict
{
public:
    // global settings
    lockable_int32 threads{this, "threads", 0, &performance_group, "number of threads to use, maximum; leave 0 for automatic"};
    
    lockable_bool verbose{this, strings{"verbose", "v"}, false, &logging_group, "verbose output"};
    lockable_bool quiet{this, strings{"quiet", "noverbose"}, false, &logging_group, "suppress non-important output"};
    lockable_bool nopercent{this, "nopercent", false, &logging_group, "don't output percentage messages"};

    virtual void setParameters(int argc, const char **argv);

    // before the parsing routine; set up options, members, etc
    virtual void preinitialize(int argc, const char **argv)
    {
        setParameters(argc, argv);
    }
    // do the actual parsing
    virtual void initialize(int argc, const char **argv)
    {
        parse(token_parser_t(argc, argv));
    }
    // after parsing has concluded, handle the side effects
    virtual void postinitialize(int argc, const char **argv);

    // run all three steps
    inline void run(int argc, const char **argv)
    {
        preinitialize(argc, argv);
        initialize(argc, argv);
        postinitialize(argc, argv);
    }
};
}; // namespace settings