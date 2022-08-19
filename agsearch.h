#ifndef AGSEARCH_H
#define AGSEARCH_H

#include <string_view>
#include <vector>
#include <map>

// agsearch
//  - 
//  - https://github.com/tringi/code-style-agnostic-search
//
class agsearch {
public:

    // parameters
    //  - 
    //
    struct parameter_set {
        bool no_comment_distinction = true; // do not distinguish between regular code and commented text
        bool no_strings_distinction = true; // do not distinguish between regular code and strings

        bool case_insensitive_strings = true;
        bool case_insensitive_comments = true;
        bool case_insensitive_identifiers = true;
        
        bool fold_and_ignore_diacritics_strings = true;
        bool fold_and_ignore_diacritics_comments = true;
        bool fold_and_ignore_diacritics_identifiers = true;
    } parameters;

    // location
    //  - describes position in the original source text
    //
    struct location {
        std::uint32_t row;
        std::uint32_t column;
        
        inline bool operator < (const location & other) const noexcept {
            return ((((std::uint64_t) this->row) << 32) | (std::uint64_t) this->column)
                 < ((((std::uint64_t) other.row) << 32) | (std::uint64_t) other.column);
        }
    };

public:
    void clear ();

    // append
    //  - 
    //  - string 'text' MAY contain multiple lines
    //
    void append (std::wstring_view text) {
        this->append_text (text);
        this->normalize ();
    }
    
    // replace
    //  - 
    //  - string 'line' MUST NOT contain multiple lines
    //
    void replace (std::uint32_t row, std::wstring_view line);

    // load
    //  - 
    //
    template <typename Container>
    void load (const Container & text) {
        this->clear ();
        for (auto & line : text) {
            this->append_text (line);
        }
        this->normalize ();
    }

    // find
    //  - 
    //
    std::size_t find (std::wstring_view needle);

protected:

    // token
    //  - 
    //
    struct token {
        enum class type : std::uint8_t {
            token = 0,
            string,
            comment,
            identifier
        };

        std::wstring  value;
        type          type = type::token;   // comparison type
        std::uint32_t length = 0;           // original length
    };

    // pattern
    //  - processed, converted and folded (according to 'parameters') source text
    //
    std::map <location, token> pattern;

private:

    // found
    //  - invoked for every occurance of 'needle' in currently loaded source text
    //  - return 'true' to continue with search
    // 
    virtual bool found (std::wstring_view needle, std::size_t i, location begin, location end) { return true; };

private:
    bool in_string = false;
    bool in_comment = false;

    void normalize ();
    void append_text (std::wstring_view text);
    bool compare_tokens (const token &, const token &);
    void process_text (std::uint32_t r, std::wstring_view text);
    void process_line (std::uint32_t r, std::wstring_view line);
};


#endif
