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
    struct parameters {

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
    void append (std::wstring_view text);
    
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
            this->append (line);
        }
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
    virtual bool found (std::wstring_view needle, std::size_t i, location begin, location end) = 0;
};


#endif
