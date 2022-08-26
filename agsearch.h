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
        bool whole_words = false; // match only whole words
        bool individual_partial_words = false; // match partial words even individually

        bool no_comment_distinction = true; // do not distinguish between regular code and commented text
        bool no_strings_distinction = true; // do not distinguish between regular code and strings

        bool case_insensitive_numbers = true;
        bool case_insensitive_strings = true;
        bool case_insensitive_comments = true;
        bool case_insensitive_identifiers = true;
        
        bool fold_and_ignore_diacritics_strings = true;
        bool fold_and_ignore_diacritics_comments = true;
        bool fold_and_ignore_diacritics_identifiers = true;

        bool digraphs = true;   // match digraphs to corresponding tokens
        bool trigraphs = true;  // match trigraphs to corresponding tokens TODO: do not implement
        bool iso646 = true;     // match ISO646 tokens to corresponding operators

        bool unescape = true;

        bool ignore_all_syntactic_tokens = false; // simply do not insert tokens to pattern
        bool ignore_trailing_semicolons = false;
        bool ignore_trailing_commas = false;
        bool ignore_all_semicolons = false;
        bool ignore_all_commas = false;

        bool numbers = true; // match different numeric notations
        bool nullptr_is_0 = true;
        bool boolean_is_integer = true;

        bool ignore_accelerator_hints_in_strings = true;
        bool undecorate_comments = true; // ignore sequences of * characters in comments

        // TODO: reorder "const volatile", "static inline", 
        // TODO: ignore nontype decl specs: "static inline virtual...
        // TODO: typename -> class, struct -> class
        // TODO: ignore ": public" or ": virtual public"

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
        this->process_text (text);
        this->normalize ();
    }
    
    // replace
    //  - 
    //  - string 'line' MUST NOT contain multiple lines
    //
    void replace (std::uint32_t row, std::wstring_view line);

    // load
    //  - load text from container of wstring_views
    //
    template <typename Container>
    void load (const Container & text) {
        this->clear ();
        for (auto & line : text) {
            this->process_text (line);
        }
        this->normalize ();
    }

    // find
    //  - searches for instances of 'needle' in loaded code
    //  - for each found instance, calls 'found' virtual callback
    //  - returns number of instances found
    //
    std::size_t find (std::wstring_view needle);

protected:

    // token
    //  - represents an element of pre-processed source text
    //
    struct token {
        enum class type : std::uint8_t {
            code = 0,
            string,
            comment,
            identifier,
            numeric,
        };

        std::wstring  value;
        std::uint32_t length = 0; // original length
        type          type {};
        char          string_type = 0; // 0, 'L', 'u', 'U', '8', 'R'
        bool          unescaped = false;

        std::uint64_t integer = 0;
        double        decimal = 0.0;
    };

    // pattern
    //  - processed, converted and folded (according to 'parameters') source text
    //
    std::map <location, token> pattern;

    // std::map <location, std::wstring> strings;

private:

    // found
    //  - invoked for every occurance of 'needle' in currently loaded source text
    //  - return 'true' to continue search
    // 
    virtual bool found (std::wstring_view needle, std::size_t i, location begin, location end) { return true; };

private:
    struct {
        enum token::type mode {};
        location         location { 0, 0 };
        char             string_type = 0;
    } current;

    std::uint8_t single_line_comment = 0;

    void normalize ();
    bool compare_tokens (const token &, const token &, std::uint32_t * first, std::uint32_t * last);
    void process_text (std::wstring_view text);
    void process_line (std::wstring_view line);

    std::wstring fold (std::wstring_view);

    bool is_identifier_initial (wchar_t);
    bool is_identifier_continuation (wchar_t);
    bool is_numeric_initial (std::wstring_view);

    struct integer_parse_state {
        bool real = false;
        int radix = 10;
        std::uint64_t integer = 0;
        double        decimal = 0.0;
    };

    std::size_t parse_integer_part (std::wstring_view line, integer_parse_state &);
    std::size_t parse_decimal_part (std::wstring_view line, integer_parse_state &);
    std::size_t parse_decimal_exponent (std::wstring_view line, integer_parse_state &);
    std::size_t parse_numeric_suffix (std::wstring_view line, integer_parse_state &);

    void append_token (wchar_t c);
    void append_token (std::wstring_view value, std::size_t advance);
    void append_identifier (std::wstring_view value, std::size_t advance);
    void append_numeric (std::wstring_view value, std::uint64_t integer, double decimal, std::size_t advance);
};


#endif
