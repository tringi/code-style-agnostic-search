#include <windows.h>
#include "agsearch.h"
#include <algorithm>
#include <cwctype>

void agsearch::clear () {
    this->pattern.clear ();
    this->in_string = false;
    this->in_comment = false;
}
void agsearch::append_text (std::wstring_view text) {
    decltype (location::row) r = 0;
    if (!this->pattern.empty ()) {
        r = this->pattern.crbegin ()->first.row + 1;
    }
    this->process_text (r, text);
}

void agsearch::replace (std::uint32_t row, std::wstring_view line) {
    this->pattern.erase (this->pattern.lower_bound ({ row + 0, 0 }),
                         this->pattern.lower_bound ({ row + 1, 0 }));
    this->process_text (row, line);
    this->normalize ();
}

std::size_t agsearch::find (std::wstring_view needle_text) {

    // convert needle to pattern

    agsearch needle;
    needle.process_text (0, needle_text);

    // no cleverness about empty sets

    if (!this->pattern.empty () && !needle.pattern.empty ()) {
        
        // basic search algorithm

        auto ih = this->pattern.cbegin ();
        auto eh = this->pattern.cend ();
        auto is = needle.pattern.cbegin ();
        auto es = needle.pattern.cend ();

        std::size_t n = 0;

        while (true) {
            auto i = ih;
            for (auto s = is; ; ++i, ++s) {
                if (s == es) {

                    auto e = i;
                    --e;
                    
                    if (this->found (needle_text, n++, ih->first, { e->first.row, e->first.column + e->second.length })) {
                        std::advance (ih, needle.pattern.size () - 1);
                        break;
                    } else
                        return n;
                }

                // end of search
                if (i == eh)
                    return n;

                // compare 'tokens' properly
                if (!this->compare_tokens (i->second, s->second))
                    break;
            }
            ++ih;
        }
    } else
        return 0;
}

bool agsearch::compare_tokens (const token & a, const token & b) {

    if ((a.type == token::type::token) || (b.type == token::type::token)) {
        if ((a.type == token::type::token) && (b.type == token::type::token)) {
            return a.value == b.value;
        } else {
            return false;
        }
    } else {
        if (!parameters.no_comment_distinction) {
            if ((a.type == token::type::comment) ^ (b.type == token::type::comment))
                return false;
        }
        if (!parameters.no_strings_distinction) {
            if ((a.type == token::type::string) ^ (b.type == token::type::string))
                return false;
        }

        DWORD flags = 0;

        if ((a.type == token::type::string) || (b.type == token::type::string)) {
            if (parameters.case_insensitive_strings) {
                flags |= LINGUISTIC_IGNORECASE | NORM_IGNORECASE | NORM_LINGUISTIC_CASING | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE;
            }
            if (parameters.fold_and_ignore_diacritics_strings) {
                flags |= LINGUISTIC_IGNOREDIACRITIC | NORM_IGNORENONSPACE;
            }
        }
        if ((a.type == token::type::comment) || (b.type == token::type::comment)) {
            if (parameters.case_insensitive_comments) {
                flags |= LINGUISTIC_IGNORECASE | NORM_IGNORECASE | NORM_LINGUISTIC_CASING | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE;
            }
            if (parameters.fold_and_ignore_diacritics_comments) {
                flags |= LINGUISTIC_IGNOREDIACRITIC | NORM_IGNORENONSPACE;
            }
        }
        if ((a.type == token::type::identifier) || (b.type == token::type::identifier)) {
            if (parameters.case_insensitive_identifiers) {
                flags |= LINGUISTIC_IGNORECASE | NORM_IGNORECASE | NORM_LINGUISTIC_CASING | NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE;
            }
            if (parameters.fold_and_ignore_diacritics_identifiers) {
                flags |= LINGUISTIC_IGNOREDIACRITIC | NORM_IGNORENONSPACE;
            }
        }

        return CompareStringEx (LOCALE_NAME_INVARIANT, flags,
                                a.value.data (), a.value.size (),
                                b.value.data (), b.value.size (),
                                NULL, NULL, 0)
            == CSTR_EQUAL;
    }
}

void agsearch::process_text (std::uint32_t r, std::wstring_view input) {
    auto i = std::wstring_view::npos;
    while ((i = input.find (L'\n', i)) != std::wstring_view::npos) {
        this->process_line (r++, input.substr (0, i));
        input.remove_prefix (i + 1);
    }
    this->process_line (r, input);
}

void agsearch::process_line (std::uint32_t r, std::wstring_view line) {
    static constexpr std::wstring_view whitespace = L" \f\n\r\t\v\x1680\x180E\x2002\x2003\x2004\x2005\x2006\x2007\x2008\x2009\x200A\x200B\x202F\x205F\x2060\x3000\xFEFF\xFFFD\0";

    auto i = std::wstring_view::npos;
    while ((i = line.find_first_not_of (whitespace, i + 1)) != std::wstring_view::npos) {

        token token;

        if (std::iswdigit (line [i])) {


        } else
        if (std::iswalpha (line [i])) {


        } else {
            // is multi-symbol token

            // else single char

        }

        // if in comment, just mark token, do not make new introducing the comment
        //  - same for strings
        //  - configurable
        // if " start string
        // if /* do comments etc

        bool fold = false;
        switch (token.type) {
            case token::type::string: fold = parameters.fold_and_ignore_diacritics_strings; break;
            case token::type::comment: fold = parameters.fold_and_ignore_diacritics_comments; break;
            case token::type::identifier: fold = parameters.fold_and_ignore_diacritics_identifiers; break;
        }
        if (fold) {
            auto flags = MAP_COMPOSITE | MAP_EXPAND_LIGATURES | MAP_FOLDCZONE | MAP_FOLDDIGITS;
            if (auto n = FoldStringW (flags, token.value.c_str (), token.value.length (), NULL, 0)) {
                std::wstring folded;
                folded.resize (n);
                n = FoldStringW (flags, token.value.c_str (), token.value.length (), folded.data (), folded.size ());
                folded.resize (n);

                token.value = std::move (folded);
            }
        }
    }
}

void agsearch::normalize () {
    // TODO: normalize order of tokens
    // TODO: normalize order of integer specs, remove redundand words but fix location & length
}
