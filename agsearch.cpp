#include "agsearch.h"
#include <algorithm>

namespace {
    struct implementation : public agsearch {
        static void process (std::map <location, token> &, std::uint32_t r, std::wstring_view text);
        static bool compare (const struct parameters &, const token & a, const token & b);
    };
}

void agsearch::clear () {
    this->pattern.clear ();
}
void agsearch::append (std::wstring_view text) {
    decltype (location::row) r = 0;
    if (!this->pattern.empty ()) {
        r = this->pattern.crbegin ()->first.row + 1;
    }
    return implementation::process (this->pattern, r, text);
}

void agsearch::replace (std::uint32_t row, std::wstring_view line) {
    this->pattern.erase (this->pattern.lower_bound ({ row + 0, 0 }),
                         this->pattern.lower_bound ({ row + 1, 0 }));
    return implementation::process (this->pattern, row, line);
}


std::size_t agsearch::find (std::wstring_view needle) {

    // convert needle to pattern

    std::map <location, token> needle_pattern;
    implementation::process (needle_pattern, 0, needle);

    // no cleverness about empty sets

    if (!this->pattern.empty () && !needle_pattern.empty ()) {
        
        // basic search algorithm

        auto ih = this->pattern.cbegin ();
        auto eh = this->pattern.cend ();
        auto is = needle_pattern.cbegin ();
        auto es = needle_pattern.cend ();

        std::size_t n = 0;

        while (true) {
            auto i = ih;
            for (auto s = is; ; ++i, ++s) {
                if (s == es) {

                    auto e = i;
                    --e;
                    
                    if (this->found (needle, n++, ih->first, { e->first.row, e->first.column + e->second.length })) {
                        std::advance (ih, needle_pattern.size () - 1);
                        break;
                    } else
                        return n;
                }

                // end of search
                if (i == eh)
                    return n;

                // compare 'tokens' properly
                if (!implementation::compare (this->parameters, i->second, s->second))
                    break;
            }
            ++ih;
        }
    } else
        return 0;
}

bool implementation::compare (const struct parameters & parameters, const token & a, const token & b) {

    return false;
}

void implementation::process (std::map <location, token> & pattern, std::uint32_t r, std::wstring_view text) {

    // break text into pattern, insert with row 'r'

}
