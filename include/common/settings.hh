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
#include <unordered_set>

namespace settings
{
struct parse_exception : public std::exception
{
private:
    std::string _what;

public:
    parse_exception(std::string str) : _what(std::move(str)) { }

    const char *what() const noexcept override { return _what.c_str(); }
};

// thrown after displaying `--help` text.
// the command-line tools should catch this and exit with status 0.
// tests should let the test framework catch this and fail.
// (previously, the `--help` code called exit(0); directly which caused
// spurious test successes.)
struct quit_after_help_exception : public std::exception
{};

enum class source
{
    DEFAULT = 0,
    MAP = 1,
    COMMANDLINE = 2
};

class nameset : public std::vector<std::string>
{
public:
    nameset(const char *str) : vector<std::string>({str}) { }
    nameset(const std::string &str) : vector<std::string>({str}) { }
    nameset(const std::initializer_list<const char *> &strs) : vector(strs.begin(), strs.end()) { }
    nameset(const std::initializer_list<std::string> &strs) : vector(strs) { }
};

struct setting_group
{
    const char *name;
    const int32_t order;
};

class setting_container;

// base class for any setting
class setting_base
{
protected:
    source _source = source::DEFAULT;
    nameset _names;
    const setting_group *_group;
    const char *_description;

    setting_base(
        setting_container *dictionary, const nameset &names, const setting_group *group, const char *description);

    constexpr bool changeSource(source newSource)
    {
        if (newSource >= _source) {
            _source = newSource;
            return true;
        }
        return false;
    }

public:
    ~setting_base() = default;

    // copy constructor is deleted. the trick we use with:
    //
    // class some_settings public settings_container {
    //     setting_bool s {this, "s", false};
    // }
    //
    // is incompatible with the settings_container/setting_base types being copyable.
    setting_base(const setting_base& other) = delete;

    // copy assignment
    setting_base& operator=(const setting_base& other) = delete;

    inline const std::string &primaryName() const { return _names.at(0); }
    inline const nameset &names() const { return _names; }
    inline const setting_group *getGroup() const { return _group; }
    inline const char *getDescription() const { return _description; }

    constexpr bool isChanged() const { return _source != source::DEFAULT; }
    constexpr bool isLocked() const { return _source == source::COMMANDLINE; }
    constexpr source getSource() const { return _source; }

    constexpr const char *sourceString() const
    {
        switch (_source) {
            case source::DEFAULT: return "default";
            case source::MAP: return "map";
            case source::COMMANDLINE: return "commandline";
            default: FError("Error: unknown setting source");
        }
    }

    // copies value and source
    virtual bool copyFrom(const setting_base& other) = 0;

    // convenience form of parse() that constructs a temporary parser_t
    bool parseString(const std::string &string, bool locked = false);

    // resets value to default, and source to source::DEFAULT
    virtual void reset() = 0;
    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) = 0;
    virtual std::string stringValue() const = 0;
    virtual std::string format() const = 0;
};

// a special type of setting that acts as a flag but
// calls back to a function to actually do the tasks.
// be careful because this won't show up in summary.
class setting_func : public setting_base
{
protected:
    std::function<void()> _func;

public:
    inline setting_func(setting_container *dictionary, const nameset &names, std::function<void()> func,
        const setting_group *group = nullptr, const char *description = "")
        : setting_base(dictionary, names, group, description), _func(func)
    {
    }

    inline bool copyFrom(const setting_base& other) override {
        return true;
    }

    inline void reset() override {}

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        _func();
        return true;
    }

    std::string stringValue() const override { return ""; }

    std::string format() const override { return ""; }
};

// base class for a setting that has its own value
template<typename T>
class setting_value : public setting_base
{
protected:
    T _default;
    T _value;

    virtual void setValueInternal(const T &value, source newSource)
    {
        if (changeSource(newSource)) {
            _value = value;
        }
    }

    inline void setValueFromParse(const T &value, bool locked)
    {
        if (locked) {
            setValueLocked(value);
        } else {
            setValue(value);
        }
    }

public:
    inline setting_value(setting_container *dictionary, const nameset &names, T v, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description), _default(v), _value(v)
    {
    }

    const T &value() const { return _value; }

    inline void setValueLocked(const T &f) { setValueInternal(f, source::COMMANDLINE); }

    inline void setValue(const T &f) { setValueInternal(f, source::MAP); }

    inline bool copyFrom(const setting_base& other) override {
        if (auto *casted = dynamic_cast<const setting_value<T> *>(&other)) {
            _value = casted->_value;
            _source = casted->_source;
            return true;
        }
        return false;
    }

    inline void reset() override {
        _value = _default;
        _source = source::DEFAULT;
    }
};

class setting_bool : public setting_value<bool>
{
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
    inline setting_bool(setting_container *dictionary, const nameset &names, bool v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, true);
    }

    std::string stringValue() const override { return _value ? "1" : "0"; }

    std::string format() const override { return _default ? "[0]" : ""; }
};

// an extension to setting_bool; this automatically adds "no" versions
// to the list, and will allow them to be used to act as `-name 0`.
class setting_invertible_bool : public setting_bool
{
private:
    nameset extendNames(const nameset &names)
    {
        nameset n = names;

        for (auto &name : names) {
            n.push_back("no" + name);
        }

        return n;
    }

public:
    inline setting_invertible_bool(setting_container *dictionary, const nameset &names, bool v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_bool(dictionary, extendNames(names), v, group, description)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, settingName.compare(0, 2, "no") == 0 ? false : true);
    }
};

class setting_redirect : public setting_base
{
private:
    std::vector<setting_base *> _settings;

public:
    inline setting_redirect(setting_container *dictionary, const nameset &names,
        const std::initializer_list<setting_base *> &settings, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description), _settings(settings)
    {
    }

    inline bool copyFrom(const setting_base& other) override {
        return true;
    }

    inline void reset() override {}

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
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

    std::string stringValue() const override { return _settings[0]->stringValue(); }

    std::string format() const override { return _settings[0]->format(); }
};

template<typename T>
class setting_numeric : public setting_value<T>
{
    static_assert(!std::is_enum_v<T>, "use setting_enum for enums");
protected:
    T _min, _max;

    void setValueInternal(const T &f, source newsource) override
    {
        if (f < _min) {
            logging::print("WARNING: '{}': {} is less than minimum value {}.\n", this->primaryName(), f, _min);
        }
        if (f > _max) {
            logging::print("WARNING: '{}': {} is greater than maximum value {}.\n", this->primaryName(), f, _max);
        }

        this->setting_value<T>::setValueInternal(std::clamp(f, _min, _max), newsource);
    }

public:
    inline setting_numeric(setting_container *dictionary, const nameset &names, T v, T minval, T maxval,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value<T>(dictionary, names, v, group, description), _min(minval), _max(maxval)
    {
        // check the default value is valid
        Q_assert(_min < _max);
        Q_assert(this->_value >= _min);
        Q_assert(this->_value <= _max);
    }

    inline setting_numeric(setting_container *dictionary, const nameset &names, T v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_numeric(
              dictionary, names, v, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), group, description)
    {
    }

    inline bool boolValue() const
    {
        return this->_value > 0;
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
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

            this->setValueFromParse(f, locked);

            return true;
        }
        catch (std::exception &) {
            return false;
        }
    }

    std::string stringValue() const override { return std::to_string(this->_value); }

    std::string format() const override { return "n"; }
};

using setting_scalar = setting_numeric<vec_t>;
using setting_int32 = setting_numeric<int32_t>;

template<typename T>
class setting_enum : public setting_value<T>
{
private:
    std::map<std::string, T, natural_less> _values;

public:
    inline setting_enum(setting_container *dictionary, const nameset &names, T v,
        const std::initializer_list<std::pair<const char *, T>> &enumValues, const setting_group *group = nullptr,
        const char *description = "")
        : setting_value<T>(dictionary, names, v, group, description), _values(enumValues.begin(), enumValues.end())
    {
    }

    std::string stringValue() const override
    {
        for (auto &value : _values) {
            if (value.second == this->_value) {
                return value.first;
            }
        }

        throw std::exception();
    }

    std::string format() const override
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

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        // see if it's a string enum case label
        if (auto it = _values.find(parser.token); it != _values.end()) {
            this->setValueFromParse(it->second, locked);
            return true;
        }

        // see if it's an integer
        try {
            const int i = std::stoi(parser.token);

            this->setValueFromParse(static_cast<T>(i), locked);
            return true;
        } catch (std::invalid_argument &) {
        } catch (std::out_of_range &) {
        }

        return false;
    }
};

class setting_string : public setting_value<std::string>
{
private:
    std::string _format;

public:
    inline setting_string(setting_container *dictionary, const nameset &names, std::string v,
        const std::string_view &format = "\"str\"", const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description), _format(format)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (parser.parse_token()) {
            setValueFromParse(parser.token, locked);
            return true;
        }

        return false;
    }

    [[deprecated("use value()")]] std::string stringValue() const override { return _value; }

    std::string format() const override { return _format; }
};

class setting_path : public setting_value<fs::path>
{
public:
    inline setting_path(setting_container *dictionary, const nameset &names, fs::path v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        // make sure we can parse token out
        if (!parser.parse_token()) {
            return false;
        }
        
        setValueFromParse(parser.token, locked);
        return true;
    }
    
    std::string stringValue() const override { return _value.string(); }

    std::string format() const override { return "\"relative/path\" or \"C:/absolute/path\""; }
};

class setting_set : public setting_base
{
private:
    std::unordered_set<std::string> _values;
    std::string _format;

    virtual void addValueInternal(const std::string &value, source newSource)
    {
        if (changeSource(newSource)) {
            _values.insert(value);
        }
    }

    inline void addValueFromParse(const std::string &value, bool locked)
    {
        if (locked) {
            setValueLocked(value);
        } else {
            setValue(value);
        }
    }

public:
    inline setting_set(setting_container *dictionary, const nameset &names,
        const std::string_view &format = "\"str\" <multiple allowed>", const setting_group *group = nullptr, const char *description = "")
        : setting_base(dictionary, names, group, description),
        _format(format)
    {
    }

    const std::unordered_set<std::string> &values() const { return _values; }

    inline void setValueLocked(const std::string &f) { addValueInternal(f, source::COMMANDLINE); }

    inline void setValue(const std::string &f) { addValueInternal(f, source::MAP); }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token(PARSE_PEEK))
            return false;

        parser.parse_token();
        addValueFromParse(parser.token, locked);
        return true;
    }

    inline bool copyFrom(const setting_base& other) override {
        if (auto *casted = dynamic_cast<const setting_set *>(&other)) {
            _values = casted->_values;
            _source = casted->_source;
            return true;
        }
        return false;
    }

    inline void reset() override {
        _values.clear();
        _source = source::DEFAULT;
    }

    std::string format() const override { return _format; }

    std::string stringValue() const override
    {
        std::string result;

        for (auto &v : _values) {
            if (!result.empty()) {
                result += ' ';
            }

            result += '\"' + v + '\"';
        }

        return result;
    }
};

class setting_vec3 : public setting_value<qvec3d>
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const { return val; }

    void setValueInternal(const qvec3d &f, source newsource) override
    {
        setting_value::setValueInternal(transformVec3Value(f), newsource);
    }

public:
    inline setting_vec3(setting_container *dictionary, const nameset &names, vec_t a, vec_t b, vec_t c,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, transformVec3Value({a, b, c}), group, description)
    {
    }

    bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
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

    std::string stringValue() const override { return qv::to_string(_value); }

    std::string format() const override { return "x y z"; }
};

class setting_mangle : public setting_vec3
{
protected:
    qvec3d transformVec3Value(const qvec3d &val) const override { return qv::vec_from_mangle(val); }

public:
    using setting_vec3::setting_vec3;
};

class setting_color : public setting_vec3
{
protected:
    qvec3d transformVec3Value(const qvec3d &val) const override { return qv::normalize_color_format(val); }

public:
    using setting_vec3::setting_vec3;
};

// settings dictionary

class setting_container
{
    struct less
    {
        constexpr bool operator()(const setting_group *a, const setting_group *b) const
        {
            int32_t a_order = a ? a->order : std::numeric_limits<int32_t>::min();
            int32_t b_order = b ? b->order : std::numeric_limits<int32_t>::min();

            return a_order < b_order;
        }
    };

    std::map<std::string, setting_base *> _settingsmap;
    std::set<setting_base *> _settings;
    std::map<const setting_group *, std::set<setting_base *>, less> _groupedSettings;

public:
    std::string programName;
    std::string remainderName = "filename";
    std::string programDescription;

    inline setting_container() {}

    ~setting_container();

    // copy constructor (can't be copyable, see setting_base)
    setting_container(const setting_container& other) = delete;

    // copy assignment
    setting_container& operator=(const setting_container& other) = delete;

    void reset();

    void copyFrom(const setting_container& other);

    inline void registerSetting(setting_base *setting)
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

    inline void registerSettings(const std::initializer_list<setting_base *> &settings)
    {
        registerSettings(settings.begin(), settings.end());
    }

    inline setting_base *findSetting(const std::string &name) const
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
        setting_base *setting = findSetting(name);

        if (setting == nullptr) {
            if (locked) {
                throw parse_exception(fmt::format("Unrecognized command-line option '{}'\n", name));
            }
            return;
        }

        parser_t p{value};
        setting->parse(name, p, locked);
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

    void printHelp();
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
extern setting_group performance_group, logging_group, game_group;

enum class search_priority_t
{
    LOOSE,
    ARCHIVE
};

class common_settings : public virtual setting_container
{
public:
    // global settings
    setting_int32 threads{
        this, "threads", 0, &performance_group, "number of threads to use, maximum; leave 0 for automatic"};
    setting_bool lowpriority{
        this, "lowpriority", false, &performance_group, "run in a lower priority, to free up headroom for other processes"};
    
    setting_invertible_bool log{this, "log", true, &logging_group, "whether log files are written or not"};
    setting_bool verbose{this, {"verbose", "v"}, false, &logging_group, "verbose output"};
    setting_bool nopercent{this, "nopercent", false, &logging_group, "don't output percentage messages"};
    setting_bool nostat{this, "nostat", false, &logging_group, "don't output statistic messages"};
    setting_bool noprogress{this, "noprogress", false, &logging_group, "don't output progress messages"};
    setting_redirect quiet{this, {"quiet", "noverbose"}, {&nopercent, &nostat, &noprogress}, &logging_group, "suppress non-important messages (equivalent to -nopercent -nostat -noprogress)"};
    setting_path gamedir{this, "gamedir", "", &game_group, "override the default mod base directory. if this is not set, or if it is relative, it will be derived from the input file or the basedir if specified."};
    setting_path basedir{this, "basedir", "", &game_group, "override the default game base directory. if this is not set, or if it is relative, it will be derived from the input file or the gamedir if specified."};
    setting_enum<search_priority_t> filepriority{this, "filepriority", search_priority_t::LOOSE, { { "loose", search_priority_t::LOOSE }, { "archive", search_priority_t::ARCHIVE } }, &game_group, "which types of archives (folders/loose files or packed archives) are higher priority and chosen first for path searching" };
    setting_set paths{this, "path", "\"/path/to/folder\" <multiple allowed>", &game_group, "additional paths or archives to add to the search path, mostly for loose files"};
    setting_bool q2rtx{this, "q2rtx", false, &game_group, "adjust settings to best support Q2RTX"};
    setting_invertible_bool defaultpaths{this, "defaultpaths", true, &game_group, "whether the compiler should attempt to automatically derive game/base paths for games that support it"};

    virtual void setParameters(int argc, const char **argv);

    // before the parsing routine; set up options, members, etc
    virtual void preinitialize(int argc, const char **argv) { setParameters(argc, argv); }
    // do the actual parsing
    virtual void initialize(int argc, const char **argv) {
        token_parser_t p(argc, argv);
        parse(p);
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