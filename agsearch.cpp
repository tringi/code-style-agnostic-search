#include <windows.h>
#include "agsearch.h"
#include <algorithm>
#include <cwctype>

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

                    auto e = i;
                    --e;
                    
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

void agsearch::process_line (std::wstring_view line) {
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
    
    // un-escape and similar transformations
    //  - 'unescaped' stores copy of 'line' in case it needs to be modified

    std::wstring unescaped;

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

        if (std::iswdigit (line [0])) {

            // if (line.starts_with (L"0x"

            // parse number
            line.remove_prefix (1);
            ++this->current.location.column;



        } else
        if (std::iswalpha (line [0]) || line [0] == L'_') { // TODO: Unicode
            std::size_t length = 1u;

            while ((length < line.length ()) && (std::iswalnum (line [length]) || line [length] == L'_')) { // TODO: Unicode
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
                    if (line.starts_with (L'"')) {
                        line.remove_prefix (1);
                        this->current.location.column += 1;
                        this->current.mode = token::type::string;

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

    /*DWORD n;
    WriteConsole (GetStdHandle (STD_OUTPUT_HANDLE), line.data (), line.size (), &n, NULL);
    WriteConsole (GetStdHandle (STD_OUTPUT_HANDLE), L"\r\n", 2, &n, NULL);*/
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
    t.value = this->fold (value);
    t.length = advance;

    this->pattern.insert ({ this->current.location, t });
    this->current.location.column += advance;
}

void agsearch::append_numeric (std::wstring_view value, std::uint64_t i, std::uint64_t d, std::size_t advance) {
    token t;
    if (this->current.mode == token::type::code) {
        t.type = token::type::numeric;
    } else {
        t.type = this->current.mode;
    }
    t.value = this->fold (value);
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
    // TODO: normalize order of tokens
    // TODO: normalize order of integer specs, remove redundand words but fix location & length
}
