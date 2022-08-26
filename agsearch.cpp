#include <windows.h>
#include "agsearch.h"
#include <algorithm>
#include <cwctype>
#include <cmath>

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

}

void agsearch::clear () {
    this->pattern.clear ();
    this->current.mode = token::type::code;
    this->current.location.row = 0;
    this->current.location.column = 0;
}

void agsearch::replace (std::uint32_t row, std::wstring_view line) {
    this->pattern.erase (this->pattern.lower_bound ({ row + 0, 0 }),
                         this->pattern.lower_bound ({ row + 1, 0 }));
    this->current.location.row = row;
    this->process_text (line);
    this->normalize ();
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
    needle.normalize ();

    // no cleverness about empty sets

    if (!this->pattern.empty () && !needle.pattern.empty ()) {
        
        // basic search algorithm

        auto ih = this->pattern.cbegin ();
        auto eh = this->pattern.cend ();
        auto is = needle.pattern.cbegin ();
        auto es = needle.pattern.cend ();

        std::size_t n = 0;

        while (true) {
            std::uint32_t fx = 0; // start index in partially found first token
            std::uint32_t lx = 0; // length of partially found last token

            auto i = ih;
            for (auto s = is; ; ++i, ++s) {
                if (s == es) {

                    auto e = get_preceeding_iterator (i);
                    if (this->found (needle_text, n++,
                                     { ih->first.row, ih->first.column + fx },
                                     { e->first.row, e->first.column + e->second.length - lx})) {
                        std::advance (ih, needle.pattern.size () - 1);
                        break;
                    } else
                        return n;
                }

                // end of search
                if (i == eh)
                    return n;

                // compare tokens properly
                if (!this->compare_tokens (i->second, s->second,
                                           (s == is) ? &fx : nullptr,
                                           is_preceeding_iterator (s, es) ? &lx : nullptr))
                    break;
            }
            ++ih;
        }
    } else
        return 0;
}

bool agsearch::compare_tokens (const token & a, const token & b, std::uint32_t * first, std::uint32_t * last) {

    if (parameters.numbers) {
        if ((a.type == token::type::numeric) && (b.type == token::type::numeric)) {
            if ((a.integer == b.integer) && (a.decimal == b.decimal))
                return true;
        }
    }

    if ((a.type == token::type::code) || (b.type == token::type::code)) {
        if ((a.type == token::type::code) && (b.type == token::type::code)) {
            return a.value == b.value;
        } else {
            return false;
        }
    } else {
        if (!this->parameters.no_comment_distinction) {
            if ((a.type == token::type::comment) ^ (b.type == token::type::comment))
                return false;
        }
        if (!this->parameters.no_strings_distinction) {
            if ((a.type == token::type::string) ^ (b.type == token::type::string))
                return false;
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

        if (this->parameters.whole_words) {
            return CompareStringEx (LOCALE_NAME_INVARIANT, flags,
                                    a.value.data (), (int) a.value.size (),
                                    b.value.data (), (int) b.value.size (),
                                    NULL, NULL, 0)
                == CSTR_EQUAL;
        } else
        if (this->parameters.individual_partial_words) {
            return FindNLSStringEx (LOCALE_NAME_INVARIANT, flags,
                                    a.value.data (), (int) a.value.size (),
                                    b.value.data (), (int) b.value.size (),
                                    NULL, NULL, NULL, 0)
                != -1;
        } else {
            if (first || last) {
                auto length = 0;
                auto offset = FindNLSStringEx (LOCALE_NAME_INVARIANT, flags,
                                               a.value.data (), (int) a.value.size (),
                                               b.value.data (), (int) b.value.size (),
                                               &length, NULL, NULL, 0);
                if (offset != -1) {
                    if (first) {
                        *first = (std::uint32_t) offset;
                    }
                    if (last) {
                        *last = (std::uint32_t) (a.value.size () - length - offset);
                    }
                    return true;
                } else
                    return false;

            } else {
                return CompareStringEx (LOCALE_NAME_INVARIANT, flags,
                                        a.value.data (), (int) a.value.size (),
                                        b.value.data (), (int) b.value.size (),
                                        NULL, NULL, 0)
                    == CSTR_EQUAL;
            }
        }
    }
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

    // trim the end
    //  - makes some options below easier

    line = line.substr (0, line.find_last_not_of (whitespace) + 1);

    // process

    while (!line.empty ()) {
next:
        // skip whitespace

        {   auto n = line.find_first_not_of (whitespace);
            if (n != std::wstring_view::npos) {
                this->current.location.column += n;
                line.remove_prefix (n);
            } else
                break;
        }

        if (this->is_numeric_initial (line)) {

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

            this->append_numeric (line.substr (0, i), state.integer, state.decimal, i);
            line.remove_prefix (i);

        } else
        if (this->is_identifier_initial (line [0])) {
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
                    this->append_numeric (identifier, 0, 0, length);
                    goto next;
                }
            }
            if (parameters.boolean_is_integer) {
                if (identifier == L"true" || identifier == L"false") {
                    line.remove_prefix (length);
                    this->append_numeric (identifier, (identifier == L"true") ? 1 : 0, 0, length);
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
                        // TODO: code token, number, or string, or both?
                    }
                    if (line.starts_with (L'"')) {
                        line.remove_prefix (1);
                        this->current.location.column += 1;
                        this->current.mode = token::type::string;
                        
                        // encode string prefix, e.g.: 'L' in L"string" 

                        if (!this->pattern.empty ()) {
                            auto & last = this->pattern.crbegin ()->second;
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
    t.type = this->current.mode;
    t.value = value;
    t.length = advance;

    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }

    this->pattern.insert ({ this->current.location, t });
    this->current.location.column += advance;
}

void agsearch::append_identifier (std::wstring_view value, std::size_t advance) {
    token t;
    if (this->current.mode == token::type::code) {
        t.type = token::type::identifier;
    } else {
        t.type = this->current.mode;
    }
    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }

    t.value = this->fold (value);
    t.length = advance;

    this->pattern.insert ({ this->current.location, t });
    this->current.location.column += advance;
}

void agsearch::append_numeric (std::wstring_view value, std::uint64_t i, double d, std::size_t advance) {
    token t;
    if (this->current.mode == token::type::code) {
        t.type = token::type::numeric;
    } else {
        t.type = this->current.mode;
    }
    if (this->current.mode == token::type::string) {
        t.string_type = this->current.string_type;
    }
    t.value = value;
    t.length = advance;
    t.integer = i;
    t.decimal = d;

    this->pattern.insert ({ this->current.location, t });
    this->current.location.column += advance;
}

void agsearch::append_token (wchar_t c) {
    return this->append_token (std::wstring_view (&c, 1), 1);
}

void agsearch::normalize () {

    // ignore accelerator hints in strings
    //  - removes sole '&' inside strings; NOTE that string are tokenized too, so it may not always work

    if (this->parameters.ignore_accelerator_hints_in_strings) {
        for (auto & [location, token] : this->pattern) {
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

    if (this->parameters.undecorate_comments) {
        for (auto & [location, token] : this->pattern) {
            if (token.type == token::type::comment) {

            }
        }
    }

    // TODO: normalize order of tokens
    // TODO: normalize order of integer specs, remove redundand words but fix location & length
}
