#include <windows.h>
#include "agsearch.h"
#include <cwctype>
#include <cmath>

#include <algorithm>
#include <tuple>
#include <set>

namespace {
    static constexpr std::wstring_view whitespace = L" \f\n\r\t\v\x1680\x180E\x2002\x2003\x2004\x2005\x2006\x2007\x2008\x2009\x200A\x200B\x202F\x205F\x2060\x3000\xFEFF\xFFFD\0";
    static constexpr std::wstring_view multi_character_tokens [] = {
        L"::", L"...", L"->*", L"->", L".*",
        L"==", L"!=", L"<=", L">=", L"<=>",
        L"++", L"--", L"<<", L">>",
        L"+=", L"-=", L"*=", L"/=", L"%=", L"&=", L"|=", L"^=", L"<<=", L">>=",
        L"&&", L"||", 
    };
    static constexpr std::pair <std::wstring_view, wchar_t> alternative_tokens [] = {
        { L"<%", L'{' }, { L"%>", L'}' },
        { L"<:", L'[' }, { L":>", L']' },
        { L"%:", L'#' },
    };
    static constexpr std::pair <std::wstring_view, wchar_t> trigraph_tokens [] = {
        { L"??<", L'{' }, { L"??>", L'}' },
        { L"??(", L'[' }, { L"??)", L']' },
        { L"??=", L'#' },
        { L"??/", L'\\' },
        { L"??'", L'^' },
        { L"??!", L'|' },
        { L"??-", L'~' },
    };
    static constexpr std::pair <std::wstring_view, std::wstring_view> iso646_tokens [] = {
        { L"and", L"&&" }, { L"and_eq", L"&=" }, { L"bitand", L"&" },
        { L"or",  L"||" }, { L"or_eq",  L"|=" }, { L"bitor", L"|" },
        { L"xor", L"^" },  { L"xor_eq", L"^=" }, { L"compl", L"~" },
        { L"not", L"!" },  { L"not_eq", L"!=" },
    };
    static const std::map <wchar_t, wchar_t> single_letter_escape_sequences = {
        { 'a', '\a' }, { 'b', '\b' },
        { 'f', '\f' },
        { 'r', '\r' }, { 'n', '\n' },
        { 't', '\t' }, { 'v', '\v' },
    };

    struct alternative_spelling {
        bool agsearch::parameter_set::* option;
        std::set <std::wstring>         spellings;

        bool operator < (const alternative_spelling & other) const noexcept {
            return this->spellings < other.spellings;
        }
    };
    static const std::set <alternative_spelling> alternative_spellings = {
        { &agsearch::parameter_set::match_ifs_and_conditional, { L"if", L"?" } },
        { &agsearch::parameter_set::match_class_struct_typename, { L"class", L"struct", L"typename" } },
        { &agsearch::parameter_set::match_float_and_double_decl, { L"float", L"double" } },
    };
    static const std::set <alternative_spelling> alternative_spellings_optional = {
        { &agsearch::parameter_set::match_ifs_and_conditional, { L"else", L":" } },
    };

    struct ignored_pattern {
        bool agsearch::parameter_set::* option;
        std::wstring                    prefix;
        std::set <std::wstring>         optional;

        bool operator < (const ignored_pattern & other) const noexcept {
            return std::tie (this->prefix, this->optional)
                 < std::tie (other.prefix, other.optional);
        }
    };
    static const std::set <ignored_pattern> ignored_patterns = {
        { &agsearch::parameter_set::match_any_inheritance_type, L":", { L"virtual", L"public", L"protected", L"private" } },
        { &agsearch::parameter_set::match_any_integer_decl_style, L"long", { L"int", L"unsigned", L"long" } },
        { &agsearch::parameter_set::match_any_integer_decl_style, L"short", { L"int", L"unsigned" } },
        { &agsearch::parameter_set::match_any_integer_decl_style, L"signed", { L"char", L"short", L"int", L"long" } },
        { &agsearch::parameter_set::match_any_integer_decl_style, L"unsigned", { L"char", L"short", L"int", L"long" } },
    };
}

void agsearch::clear () {
    this->pattern.clear ();
    this->current.mode = token::type::code;
    this->current.location.row = 0;
    this->current.location.column = 0;
}

namespace {
    template <typename IT>
    inline IT get_preceeding_iterator (IT it) {
        --it;
        return it;
    }
    template <typename IT>
    inline bool is_preceeding_iterator (IT it, IT e) {
        ++it;
        return it == e;
    }
}

std::size_t agsearch::find (std::wstring_view needle_text) {

    // convert needle to pattern

    agsearch needle;
    needle.parameters = this->parameters;
    needle.process_text (needle_text);
    needle.normalize_needle ();

    // no cleverness about empty sets

    if (!this->pattern.empty () && !needle.pattern.empty ()) {
        
        // basic search algorithm
        // TODO: parallel search in 'strings' and 'reordered' - remember last result and ignore repeats

        auto ipattern = this->pattern.cbegin ();
        auto epattern = this->pattern.cend ();
        auto is = needle.pattern.cbegin ();
        auto es = needle.pattern.cend ();

        location prev_b = { (std::uint32_t) -1, (std::uint32_t) -1 };
        location prev_e = { (std::uint32_t) -1, (std::uint32_t) -1 };
        std::size_t n = 0;

        while (true) {
            std::uint32_t fx = 0; // start index in partially found first token
            std::uint32_t lx = 0; // length of partially found last token

            auto i = ipattern;
            auto s = is;

            const std::set <std::wstring> * ignore = nullptr;
            bool ignore_skip_prefix = false;

            while (true) {
                if (s == es) {

                    auto lastfind = get_preceeding_iterator (i);
                    location found_b = { ipattern->location.row, ipattern->location.column + fx };
                    location found_e = { lastfind->location.row, lastfind->location.column + lastfind->length - lx };

                    if (this->found (needle_text, n++, found_b, found_e)) {
                        std::advance (ipattern, needle.pattern.size () - 1);

                        prev_b = found_b;
                        prev_e = found_e;
                        break;
                    } else
                        return n;
                }

                // end of search
                if (i == epattern)
                    return n;

                // check for optional patterns
                for (const auto & ip : ignored_patterns) {
                    if (this->parameters.*ip.option)
                        if (s->value == ip.prefix/* && s->second.type == token::type::code or idetifier*/) {
                            ignore = &ip.optional;
                            ignore_skip_prefix = true;
                            break;
                        }
                }
                bool skip = false;
                if (ignore) {
                    if (ignore_skip_prefix) {
                        ignore_skip_prefix = false;
                    } else {
                        if (ignore->contains (i->value)) {
                            skip = true;
                        } else {
                            ignore = nullptr;
                        }
                    }
                }

                // compare tokens properly
                auto equivalent = this->compare_tokens (*i, *s,
                                                        (s == is) ? &fx : nullptr,
                                                        is_preceeding_iterator (s, es) ? &lx : nullptr);
                if (equivalent) {
                    ++i;
                    ++s;
                } else
                if (skip) {
                    ++i;
                } else
                    break;
            }
            ++ipattern;
        }
    } else
        return 0;
}

bool agsearch::compare_tokens (const token & a, const token & b, std::uint32_t * first, std::uint32_t * last) {

    // NOTE: 'a' is the pattern/haystack, 'b' is always the searched query/needle

    if (parameters.numbers) {
        if ((a.type == token::type::numeric) && (b.type == token::type::numeric)) {
            
            if (this->parameters.match_floats_and_integers) {
                if ((a.integer == b.integer) && (a.decimal == b.decimal))
                    return true;

            } else {
                if ((a.is_decimal == b.is_decimal) && (a.integer == b.integer) && (a.decimal == b.decimal))
                    return true;
            }
        }
    }

    if ((a.type == token::type::code) || (b.type == token::type::code)) {

        // fast path for language symbols comparison

        if ((a.type == token::type::code) && (b.type == token::type::code))
            if (a.value == b.value)
                return true;

    } else {

        if (this->parameters.orthogonal) {

            // code/string/comment must match in orthogonal search
            //  - for this 'numeric' and 'identifier' are equivalent

            switch (b.type) {
                case token::type::numeric:
                case token::type::identifier:
                    switch (a.type) {
                        case token::type::numeric:
                        case token::type::identifier:
                            break;
                        default:
                            return false;
                    }
                    break;

                case token::type::string:
                case token::type::comment:
                    if (a.type != b.type)
                        return false;
            }

        } else {

            // if I enter regular query, I want it match everything
            //  - if I explicitly enter "string" I want it to match only strings
            //  - if I explicitly enter //comment I want it to match only comments

            switch (b.type) {
                case token::type::string:
                case token::type::comment:
                    if (a.type != b.type)
                        return false;
            }
        }

        DWORD flags = 0;

        if ((a.type == token::type::numeric) || (b.type == token::type::numeric)) {
            if (this->parameters.case_insensitive_numbers) {
                flags |= LINGUISTIC_IGNORECASE;// | NORM_IGNORECASE | NORM_LINGUISTIC_CASING | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE;
            }
        }
        if ((a.type == token::type::string) || (b.type == token::type::string)) {
            if (this->parameters.case_insensitive_strings) {
                flags |= LINGUISTIC_IGNORECASE;
            }
            if (this->parameters.fold_and_ignore_diacritics_strings) {
                flags |= LINGUISTIC_IGNOREDIACRITIC;
            }
        }
        if ((a.type == token::type::comment) || (b.type == token::type::comment)) {
            if (this->parameters.case_insensitive_comments) {
                flags |= LINGUISTIC_IGNORECASE;
            }
            if (this->parameters.fold_and_ignore_diacritics_comments) {
                flags |= LINGUISTIC_IGNOREDIACRITIC;
            }
        }
        if ((a.type == token::type::identifier) || (b.type == token::type::identifier)) {
            if (this->parameters.case_insensitive_identifiers) {
                flags |= LINGUISTIC_IGNORECASE;// | NORM_IGNORECASE | NORM_LINGUISTIC_CASING | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE;
            }
            if (this->parameters.fold_and_ignore_diacritics_identifiers) {
                flags |= LINGUISTIC_IGNOREDIACRITIC;// | NORM_IGNORENONSPACE;
            }
        }

        // compare values

        if (this->compare_strings (flags, a.value, b.value, first, last))
            return true;

        // compare alternative

        bool aa = !a.alternative.empty ();
        bool ab = !b.alternative.empty ();

        if (aa || ab) {
            if (ab) {
                if (this->compare_strings (flags, a.value, b.alternative, first, last))
                    return true;
            }
            if (aa) {
                if (this->compare_strings (flags, a.alternative, b.value, first, last))
                    return true;
            }
            if (aa && ab) {
                if (this->compare_strings (flags, a.alternative, b.alternative, first, last))
                    return true;
            }
        }
    }

    // alternative spellings

    for (auto & as : alternative_spellings) {
        if (this->parameters.*as.option)
            if (as.spellings.contains (a.value) && as.spellings.contains (b.value))
                return true;
    }
    if (a.opt_alt_spelling_allowed || b.opt_alt_spelling_allowed) {
        for (auto & as : alternative_spellings_optional)
            if (this->parameters.*as.option)
                if (as.spellings.contains (a.value) && as.spellings.contains (b.value))
                    return true;
    }

    return false;
}

bool agsearch::compare_strings (DWORD flags, const std::wstring & a, const std::wstring & b, std::uint32_t * first, std::uint32_t * last) {
    if (this->parameters.whole_words) {
        if (CompareStringEx (LOCALE_NAME_INVARIANT, flags,
                             a.data (), (int) a.size (),
                             b.data (), (int) b.size (),
                             NULL, NULL, 0) == CSTR_EQUAL)
            return true;
    } else
    if (this->parameters.individual_partial_words) {
        if (FindNLSStringEx (LOCALE_NAME_INVARIANT, flags,
                                a.data (), (int) a.size (),
                                b.data (), (int) b.size (),
                                NULL, NULL, NULL, 0) != -1)
            return true;
    } else {
        if (first || last) {
            auto length = 0;
            auto offset = FindNLSStringEx (LOCALE_NAME_INVARIANT, flags,
                                            a.data (), (int) a.size (),
                                            b.data (), (int) b.size (),
                                            &length, NULL, NULL, 0);
            if (offset != -1) {
                if (first) {
                    *first = (std::uint32_t) offset;
                }
                if (last) {
                    *last = (std::uint32_t) (a.size () - length - offset);
                }
                return true;
            }
        } else {
            if (CompareStringEx (LOCALE_NAME_INVARIANT, flags,
                                    a.data (), (int) a.size (),
                                    b.data (), (int) b.size (),
                                    NULL, NULL, 0) == CSTR_EQUAL)
                return true;
        }
    }
    return false;
}

void agsearch::process_text (std::wstring_view input) {
    auto i = std::wstring_view::npos;
    while ((i = input.find (L'\n', i + 1)) != std::wstring_view::npos) {
        this->process_line (input.substr (0, i));
        input.remove_prefix (i + 1);
    }
    this->process_line (input);
}

namespace {
    bool any_of (wchar_t c, std::wstring_view sv) {
        return sv.find (c) != std::wstring_view::npos;
    }
}

bool agsearch::is_identifier_initial (wchar_t c) {
    return std::iswalpha (c) // TODO: Unicode
        || c == L'_'
        ;
}
bool agsearch::is_identifier_continuation (wchar_t c) {
    switch (this->current.mode) {
        case token::type::string:
            if (parameters.ignore_accelerator_hints_in_strings) {
                if (c == L'&')
                    return true;
            }
            break;
    }
    return std::iswalnum (c) // TODO: Unicode
        || c == L'_'
        ;
}
bool agsearch::is_numeric_initial (std::wstring_view line) {
    return std::iswdigit (line [0])
        || ((line.length () > 1)
            && (line [0] == L'.')
            && std::iswdigit (line [1]));
}
std::size_t agsearch::parse_integer_part (std::wstring_view line, integer_parse_state & state) {
    std::size_t i = 0;

    if ((line.length () > 1) && (line [0] == L'0')) {
        switch (line [1]) {
            case L'x': case L'X': state.radix = 16; i = 2; break;
            case L'b': case L'B': state.radix = 2;  i = 2; break;
            default:              state.radix = 8;  i = 1; break;
        }
    }

    for (; i != line.length (); ++i) {
        switch (state.radix) {
            case 16:
                switch (line [i]) {
                    case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7': case L'8': case L'9':
                        state.integer *= 16;
                        state.integer += line [i] - L'0';
                        break;
                    case L'a': case L'b': case L'c': case L'd': case L'e': case L'f':
                        state.integer *= 16;
                        state.integer += 10 + (line [i] - L'a');
                        break;
                    case L'A': case L'B': case L'C': case L'D': case L'E': case L'F':
                        state.integer *= 16;
                        state.integer += 10 + (line [i] - L'A');
                        break;

                    case L'.': case L'p': case L'P':
                        state.real = true;
                        return i;

                    case L'\'':
                        break;
                    default:
                        return i;
                }
                break;

            case 10:
                switch (line [i]) {
                    case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7': case L'8': case L'9':
                        state.integer *= 10;
                        state.integer += line [i] - L'0';
                        break;
                        
                    case L'.': case L'e': case L'E':
                        state.real = true;
                        return i;

                    case L'\'':
                        break;
                    default:
                        return i;
                }
                break;

            case 8:
                switch (line [i]) {
                    case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7':
                        state.integer *= 8;
                        state.integer += line [i] - L'0';
                        break;

                    case L'\'':
                        break;
                    default:
                        return i;
                }
                break;

            case 2:
                switch (line [i]) {
                    case L'0': state.integer *= 2; break;
                    case L'1': state.integer *= 2; ++state.integer; break;

                    case L'\'':
                        break;
                    default:
                        return i;
                }
                break;
        }
    }
    return i;
}

std::size_t agsearch::parse_decimal_part (std::wstring_view line, integer_parse_state & state) {
    if (line [0] == L'.') {

        std::size_t i = 1;
        for (; i != line.length (); ++i) {

            double multiplier = 1.0;
            switch (state.radix) {
                case 16:
                    switch (line [i]) {
                        case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7': case L'8': case L'9':
                            multiplier /= 16.0;
                            state.decimal += multiplier * (line [i] - L'0');
                            break;
                        case L'a': case L'b': case L'c': case L'd': case L'e': case L'f':
                            multiplier /= 16.0;
                            state.decimal += multiplier * (10 + (line [i] - L'a'));
                            break;
                        case L'A': case L'B': case L'C': case L'D': case L'E': case L'F':
                            multiplier /= 16.0;
                            state.decimal += multiplier * (10 + (line [i] - L'A'));
                            break;

                        case L'\'':
                            break;
                        default:
                            return i + this->parse_decimal_exponent (line.substr (i), state);
                    }
                    break;

                case 10:
                    switch (line [i]) {
                        case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7': case L'8': case L'9':
                            multiplier /= 10.0;
                            state.decimal += multiplier * (line [i] - L'0');
                            break;

                        case L'\'':
                            break;
                        default:
                            return i + this->parse_decimal_exponent (line.substr (i), state);
                    }
                    break;

                case 8:
                    switch (line [i]) {
                        case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7':
                            multiplier /= 8.0;
                            state.decimal += multiplier * (line [i] - L'0');
                            break;

                        case L'\'':
                            break;
                        default:
                            return i + this->parse_decimal_exponent (line.substr (i), state);
                    }
                    break;
            }
        }
        return i;
    } else
        return this->parse_decimal_exponent (line, state);
}

std::size_t agsearch::parse_decimal_exponent (std::wstring_view line, integer_parse_state & state) {
    if (line.length () > 1) {

        switch (line [0]) {
            case L'p':
            case L'P':
            case L'e':
            case L'E':

                // parse exponent

                std::size_t i = 1;
                bool negative = false;

                if (line [i] == '-') {
                    negative = true;
                    ++i;
                }
                if (i < line.length ()) {

                    int exponent = 0;
                    for (; i != line.length (); ++i) {

                        switch (line [i]) {
                            case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7': case L'8': case L'9':
                                exponent *= 10;
                                exponent += line [i] - L'0';
                                break;

                            case L'\'':
                                break;
                            default:
                                goto apply;
                        }
                    }

apply:
                    if (negative) {
                        exponent = -exponent;
                    }
                    state.decimal += state.integer;
                    switch (line [0]) {
                        case L'p':
                        case L'P':
                            state.decimal *= std::pow (2, exponent);
                            break;

                        case L'e':
                        case L'E':
                            state.decimal *= std::pow (10.0, exponent);
                            break;
                    }
                    state.integer = state.decimal;
                    state.decimal -= state.integer;
                    return i;
                }
        }
    }
    return 0;
}

namespace {
    template <typename C>
    C at_or_0 (std::basic_string_view <C> & sv, std::size_t i) {
        if (i < sv.length ()) {
            return sv [i];
        } else
            return C (0);
    }

    // assume line[0] == L'\'';
    std::size_t character_literal_length (std::wstring_view line) {
        
        std::size_t e = 1u;
        while (e < line.length ()) {
            switch (line [e]) {
                case L'\'':
                    return e + 1;

                case L'\\':
                    e += 2;
                    break;

                default:
                    e += 1;
            }
        }
        return line.length ();
    }
}

std::size_t agsearch::parse_numeric_suffix (std::wstring_view line, integer_parse_state & state) {
    if (!line.empty ()) {
        if (state.real) {

            // floating-point suffixes

            switch (line [0]) {
                case L'f':
                case L'F':
                case L'l':
                case L'L':
                    return 1;
            }
        } else {

            // integer suffixes

            switch (line [0]) {
                case L'u':
                case L'U':
                    switch (at_or_0 (line, 1)) {
                        case L'l':
                        case L'L':
                            switch (at_or_0 (line, 2)) {
                                case L'l':
                                case L'L':
                                    return 3;
                            }
                            return 2;

                        case L'z':
                        case L'Z':
                            return 2;
                    }
                    return 1;

                case L'l':
                case L'L':
                    switch (at_or_0 (line, 1)) {
                        case L'l':
                        case L'L':
                            switch (at_or_0 (line, 2)) {
                                case L'u':
                                case L'U':
                                    return 3;
                            }
                            return 2;

                        case L'u':
                        case L'U':
                            return 2;
                    }
                    return 1;

                case L'z':
                case L'Z':
                    switch (at_or_0 (line, 1)) {
                        case L'u':
                        case L'U':
                            return 2;
                    }
                    return 1;
            }
        }
    }
    return 0;
}

void agsearch::process_line (std::wstring_view line) {
    
    // un-escape and similar transformations
    //  - 'unescaped' is local copy of 'line' in case it needs to be modified

    std::wstring unescaped;
    if (this->parameters.unescape/* || this->parameters.accelerators*/) {
        // this->unescape_strings (line, unescaped);
    }

    // strings

    /*switch (this->current.mode) {
        case token::type::string:
            // find ending "
            break;
        case token::type::code:
            break;
    }*/

    // trim the end
    //  - makes some options below easier

    line = line.substr (0, line.find_last_not_of (whitespace) + 1);

    // process

    while (!line.empty ()) {
next:
        // skip whitespace

        auto skipped_whitespace = line.find_first_not_of (whitespace);
        if (skipped_whitespace != std::wstring_view::npos) {
            this->current.location.column += skipped_whitespace;
            line.remove_prefix (skipped_whitespace);
        } else
            break;
        

        if (this->is_numeric_initial (line)) {

            // integers and floating point literals

            integer_parse_state state;
            auto i = this->parse_integer_part (line, state);
            if (i < line.length ()) {
                if (state.real) {
                    i += this->parse_decimal_part (line.substr (i), state);
                }
            }
            if (i < line.length ()) {
                i += this->parse_numeric_suffix (line.substr (i), state);
            }

            this->append_numeric (line.substr (0, i), state.integer, state.real ? &state.decimal : nullptr, i);
            line.remove_prefix (i);

        } else
        if (this->is_identifier_initial (line [0])) {

            // identifiers, functions, names, word operators, etc.

            std::size_t length = 1u;

            while ((length < line.length ()) && this->is_identifier_continuation (line [length])) {
                ++length;
            }

            auto identifier = line.substr (0, length);

            if (parameters.iso646) {
                for (auto & [iso646, simple ] : iso646_tokens) {
                    if (identifier == iso646) {
                        line.remove_prefix (length);
                        this->append_token (simple, length);
                        goto next;
                    }
                }
            }
            if (parameters.nullptr_is_0) {
                if (identifier == L"nullptr" || identifier == L"NULL") {
                    line.remove_prefix (length);
                    this->append_numeric (identifier, 0, nullptr, length);
                    goto next;
                }
            }
            if (parameters.boolean_is_integer) {
                if (identifier == L"true" || identifier == L"false") {
                    line.remove_prefix (length);
                    this->append_numeric (identifier, (identifier == L"true") ? 1 : 0, nullptr, length);
                    goto next;
                }
            }

            this->append_identifier (identifier, length);
            line.remove_prefix (length);

        } else {

            // code/comment/string switching

            switch (this->current.mode) {
                case token::type::code:
                    if (line.starts_with (L"/*")) {
                        line.remove_prefix (2);
                        this->current.location.column += 2;
                        this->current.mode = token::type::comment;

                        goto next;
                    }
                    if (line.starts_with (L"//")) {
                        line.remove_prefix (2);
                        this->current.location.column += 2;
                        this->current.mode = token::type::comment;

                        if (line.ends_with (L'\\')) {
                            this->single_line_comment = 2;
                        } else {
                            this->single_line_comment = 1;
                        }

                        goto next;
                    }
                    if (line.starts_with (L'\'')) {
                        if (auto e = character_literal_length (line)) {
                            
                            // encode string prefix, e.g.: 'L' in L'x' 

                            if (!this->pattern.empty ()) {
                                auto & last = *this->pattern.crbegin ();
                                if ((last.type == token::type::identifier) && (last.value.length () == 1)) {

                                    this->current.string_type = (char) last.value [0];

                                    // remove the token with the letter

                                    this->pattern.erase (get_preceeding_iterator (this->pattern.end ()));
                                }
                            }
                            
                            if (e > 1) {
                                this->current.mode = token::type::string;
                                this->append_token (line.substr (1, e - 2), e);
                                this->current.mode = token::type::code;
                            }
                            line.remove_prefix (e);
                            goto next;
                        }
                    }
                    if (line.starts_with (L'"')) {
                        line.remove_prefix (1);
                        this->current.location.column += 1;
                        this->current.mode = token::type::string;
                        
                        // encode string prefix, e.g.: 'L' in L"string" 

                        if (!this->pattern.empty ()) {
                            auto & last = *this->pattern.crbegin ();
                            if ((last.type == token::type::identifier) && (last.value.length () == 1)) {
                                
                                this->current.string_type = (char) last.value [0];

                                // remove the token with the letter

                                this->pattern.erase (get_preceeding_iterator (this->pattern.end ()));
                            }
                        }
                        goto next;
                    }
                    break;

                case token::type::comment:
                    if (line.starts_with (L"*/") && !this->single_line_comment) {
                        line.remove_prefix (2);
                        this->current.location.column += 2;
                        this->current.mode = token::type::code;

                        goto next;
                    }
                    if (this->parameters.undecorate_comments) {
                        if (line.starts_with (L'*') || line.starts_with (L'/')) {
                            line.remove_prefix (1);
                            this->current.location.column += 1;
                            goto next;
                        }
                    }
                    break;

                case token::type::string:
                    if (line.starts_with (L"\\\"")) {
                        line.remove_prefix (2);
                        this->append_token (L"\"", 2);

                        goto next;
                    }
                    if (line.starts_with (L'"')) {
                        line.remove_prefix (1);
                        this->current.location.column += 1;
                        this->current.mode = token::type::code;
                        this->current.string_type = 0;

                        goto next;
                    }
                    break;
            }

            for (auto & mct : multi_character_tokens) {
                if (line.starts_with (mct)) {
                    line.remove_prefix (mct.length ());

                    if (parameters.ignore_all_syntactic_tokens) {
                        this->current.location.column += mct.length ();
                    } else {
                        this->append_token (mct, mct.length ());
                    }
                    goto next;
                }
            }

            if (this->parameters.digraphs) {
                for (auto & digraph : alternative_tokens) {
                    if (line.starts_with (digraph.first)) {
                        line.remove_prefix (digraph.first.length ());

                        if (parameters.ignore_all_syntactic_tokens) {
                            this->current.location.column += digraph.first.length ();
                        } else {
                            this->append_token (digraph.second);
                        }
                        goto next;
                    }
                }
            }

            if (this->parameters.trigraphs) {
                for (auto & trigraph : trigraph_tokens) {
                    if (line.starts_with (trigraph.first)) {
                        line.remove_prefix (trigraph.first.length ());

                        if (parameters.ignore_all_syntactic_tokens) {
                            this->current.location.column += trigraph.first.length ();
                        } else {
                            this->append_token (trigraph.second);
                        }
                        goto next;
                    }
                }
            }

            if (parameters.ignore_all_syntactic_tokens) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_all_parentheses && ((line [0] == L'(') || (line [0] == L')'))) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_all_brackets && ((line [0] == L'[') || (line [0] == L']'))) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_all_braces && ((line [0] == L'{') || (line [0] == L'}'))) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_all_commas && (line [0] == L',')) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_all_semicolons && (line [0] == L';')) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_trailing_commas && (line [0] == L',') && (line.length () == 1)) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }
            if (parameters.ignore_trailing_semicolons && (line [0] == L';') && (line.length () == 1)) {
                ++this->current.location.column;
                line.remove_prefix (1);
                goto next;
            }

            this->append_token (line [0]);
            line.remove_prefix (1);
        }
    }

    this->current.location.row++;
    this->current.location.column = 0;

    if (this->single_line_comment) {
        --this->single_line_comment;
        if (this->single_line_comment == 0) {
            this->current.mode = token::type::code;
        }
    }
}

std::wstring agsearch::fold (std::wstring_view value) {
    bool fold = false;
    switch (this->current.mode) {
        case token::type::string: fold = parameters.fold_and_ignore_diacritics_strings; break;
        case token::type::comment: fold = parameters.fold_and_ignore_diacritics_comments; break;

        case token::type::code:
        case token::type::identifier: fold = parameters.fold_and_ignore_diacritics_identifiers; break;
    }
    if (fold) {
        auto flags = MAP_COMPOSITE | MAP_EXPAND_LIGATURES | MAP_FOLDCZONE | MAP_FOLDDIGITS;
        if (auto n = FoldStringW (flags, value.data (), (int) value.size (), NULL, 0)) {
            std::wstring folded;
            folded.resize (n);
            n = FoldStringW (flags, value.data (), (int) value.size (), folded.data (), (int) folded.size ());
            folded.resize (n);

            return folded;
        }
    }
    return std::wstring (value);
}

void agsearch::append_token (std::wstring_view value, std::size_t advance) {
    token t;
    t.location = this->current.location;
    t.type = this->current.mode;
    t.value = value;
    t.length = (std::uint32_t) advance;

    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }

    this->pattern.push_back (t);
    this->current.location.column += (std::uint32_t) advance;
}

void agsearch::append_identifier (std::wstring_view value, std::size_t advance) {
    token t;
    t.location = this->current.location;

    if (this->current.mode == token::type::code) {
        t.type = token::type::identifier;
    } else {
        t.type = this->current.mode;
    }
    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }

    t.value = this->fold (value);
    t.length = (std::uint32_t) advance;

    this->pattern.push_back (t);
    this->current.location.column += (std::uint32_t) advance;
}

void agsearch::append_numeric (std::wstring_view value, std::uint64_t i, double * d, std::size_t advance) {
    token t;
    t.location = this->current.location;

    if (this->current.mode == token::type::code) {
        t.type = token::type::numeric;
    } else {
        t.type = this->current.mode;
    }
    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }
    t.value = value;
    t.length = (std::uint32_t) advance;
    t.integer = i;

    if (d) {
        t.decimal = *d;
        t.is_decimal = true;
    }

    this->pattern.push_back (t);
    this->current.location.column += (std::uint32_t) advance;
}

void agsearch::append_token (wchar_t c) {
    return this->append_token (std::wstring_view (&c, 1), 1);
}

namespace {

    //
    std::size_t is_eligible_for_camelcasing (std::wstring_view sv) {
        auto i = sv.find_first_not_of (L'_');
        if (i != std::wstring_view::npos) { // not only undescores
            sv.remove_prefix (i);
            sv.remove_suffix (sv.length () - (sv.find_last_not_of (L'_') + 1));

            

        }
        return 0;
    }

}

void agsearch::normalize_needle () {

    // detect which ':' can be converted into else

    if (this->parameters.match_ifs_and_conditional) {
        auto n = 0u;
        for (auto & token : this->pattern) {
            if (token.value == L"?") {
                ++n;
            } else
            if (n && (token.value == L":")) {
                token.opt_alt_spelling_allowed = true;
                --n;
            }
        }
    }

    // ignore accelerator hints in strings
    //  - removes sole '&' inside strings; NOTE that string are tokenized too, so it may not always work

    if (this->parameters.ignore_accelerator_hints_in_strings) {
        for (auto & token : this->pattern) {
            if (token.type == token::type::string) {

                auto i = std::wstring::npos;
                while ((i = token.value.find (L'&', i + 1)) != std::wstring::npos) {

                    if ((i < token.value.length () - 1) && (token.value [i + 1] == L'&')) {
                        token.value.erase (i, 1);
                        ++i;
                    } else {
                        token.value.erase (i, 1);
                    }
                }
            }
        }
    }

    // create alternative "camelCaseIdentifiers" for all "snake_case_identifiers

    if (this->parameters.match_snake_and_camel_casing) {
        for (auto & token : this->pattern) {
            switch (token.type) {
                case token::type::identifier:
                case token::type::comment:
                case token::type::string:
                    
                    // is eligible for camelcasing
                    //  - if, ignoring prefix and suffix underscores, contains sole underscores between words

                    auto leading = token.value.find_first_not_of (L'_');
                    if (leading != std::wstring::npos) {

                        std::wstring_view sv (token.value);
                        sv.remove_prefix (leading);

                        auto trailing = sv.length () - (sv.find_last_not_of (L'_') + 1);
                        sv.remove_suffix (trailing);

                        // count underscores followed by letter

                        std::size_t underscores = 0;
                        for (std::size_t i = 0; i != sv.length () - 1; ++i) {
                            if ((sv [i] == L'_') && std::iswalpha (sv [i + 1]))
                                ++underscores;
                        }

                        // eligible, create alternative version

                        if (underscores) {
                            token.alternative.reserve (token.value.length () - underscores);
                            token.alternative.append (leading, L'_');

                            for (std::size_t i = 0; i < sv.length () - 1; ++i) {
                                if ((sv [i] == L'_') && std::iswalpha (sv [i + 1])) {
                                    token.alternative.append (1, std::towupper (sv [i + 1]));
                                    ++i;
                                } else {
                                    token.alternative.append (1, sv [i]);
                                }
                            }

                            token.alternative.append (1, sv.back ());
                            token.alternative.append (trailing, L'_');
                        }
                    }
            }
        }
    }

    // TODO: rewrite casts
    //  ? xxx_cast < A A A > ( B B B )
    //  > ( A A A ) B B B

}

void agsearch::normalize_full () {
    this->normalize_needle ();

    // unescape strings

    /*if (this->parameters.unescape) {
        for (auto & [location, token] : this->pattern) {
            if ((token.type == token::type::string) && !token.unescaped) {

                auto i = std::wstring::npos;
                while ((i = token.value.find (L'\\', i + 1)) != std::wstring::npos) {
                    if (i < token.value.length () - 1) {

                        wchar_t aaa [256];
                        std::swprintf (aaa, 256, L"%u:%u '%c'\r\n", location.row, location.column, token.value [i + 1]);
//                        std::swprintf (aaa, 256, L"combine \"%s\" and \"%s\"\r\n", i->second.value.c_str (), j->second.value.c_str ());
                        DWORD n;
                        WriteConsole (GetStdHandle (STD_OUTPUT_HANDLE), aaa, std::wcslen (aaa), &n, NULL);


                        switch (auto c = token.value [i + 1]) {
                            case L'x': // \x##

                                break;
                            case L'u': // \u####

                                break;
                            case L'U': // \u########

                                break;
                            default:
                                if (std::iswdigit (c)) {


                                } else {
                                    auto j = single_letter_escape_sequences.find (c);
                                    if (j != single_letter_escape_sequences.end ()) {

                                         token.value.replace (i, 2, 1, j->second);
                                        ++i;
                                    } else {

                                        // \000

                                        token.value.erase (i);
                                    }
                                    // unescaped.erase (i, 1);
                                    // unescaped.replace (i, ..., L'');
                                }
                                break;
                        }
                    }
                }

                token.unescaped = true;
            }
        }
    }*/
}
