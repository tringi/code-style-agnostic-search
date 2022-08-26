# Coding Style -agnostic (and more) search for C++

Relatively simple 
with various features:

## Features

* Ignores insignificant whitespace; including line endings
* Individual partial words matching, on top of classic whole word matching on/off modes  
  `stat nlin boo` == `static inline bool`
* Linguistic folding, diacritics and case insensitivity of tokens implemented through Windows API NLS
* Matching different numeric notations
  `0x007B`, `0173`, `0b0'0111'1011` all match `123`

* Option to ignore keyboard accelerator hints (&, Win32 GUI feature) in strings
* Options to ignore all syntactic tokens, and commas or semicolons, either all or trailing only
* Matching digraphs, trigraphs and ISO646 alternative tokens to primary tokens they represent

## Usage
*[SearchTest.cpp](https://github.com/tringi/code-style-agnostic-search/blob/main/test/SearchTest.cpp)*

    #include "agsearch.h"
    
    struct search : public agsearch {
        std::vector <std::pair <location, location>> results;
    
        std::size_t find (std::wstring_view needle) {
            this->results.clear ();
            return this->agsearch::find (needle);
        }
    
    private:
        bool found (std::wstring_view needle, std::size_t i, location begin, location end) override {
            this->results.push_back ({ begin, end });
            return true;
        }
    } search;
    
    int main () {
    
        // ...
    
        search.parameters.whole_words = false; // configure the engine
    
        search.load (text);         // 'text' is container of std::wstring_view
        search.append (line);       // 'line' is single line of code std::wstring_view
        search.replace (row, line); // 'row' is row index to replace
    
        // ...
    
        search.find (needle); // 'needle' is code to be searched for
        search.results;       // contains pair of 'location' for every found instance
    
        // ...
    }

**Notes:**

* whole **text** must be reloaded when any of the `agsearch::parameters` change
* `agsearch::location` contains `row` and `column` members, and both are 0-based
* return false from `found` virtual callback to stop search

## TODO

* match different forms of escapes, e.g.: `\n == \013`
* match escaped characters to actual characters
* add option whether to match integers and floats, i.e. "123.0f" == "0x007BuLL"
* match different notations for the same type `"unsigned int" == "int unsigned"`
   * `== "std::uint32_t"` (configurable plaftorm assumptions)
   * ignore redundant
* match different order of declaration qualifiers, e.g.: `"static inline" == "inline static"`
* match reinterpret_cast/static_cast to C-style cast
* match curly braces and lack of them where appropriate, e.g. single statement after `if`
* combine string literals
