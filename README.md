# Coding Style -agnostic (and more) search for C++

* Ignores insignificant whitespace; including line endings
* Individual partial words matching, on top of classic whole word matching on/off modes  
  `stat nlin boo` == `static inline bool`
* Linguistic folding, diacritics and case insensitivity of tokens implemented through Windows API NLS
* Entering query (or part) as `/*comment*/` or `"string"` searches (that part) within comments/strings only
   * orthogonal mode will search code only within code
* Matching of `camelCase' and `snake_case` identifiers
* Matching different numeric notations  
  `0x007B`, `0173`, `0b0'0111'1011` all match `123`  
  `0x7BuLL` matches `123.0f` unless the option to match integers and floats is turned off
* Matching specific language tokens to their numeric values
   * `true` and `false` match 0/1
   * `NULL` and `nullptr` match 0
* Matching semantically similar constructs user may not care for when searching
   * `class abc` will find `struct abc` as well, `template<typename` will find `template<class`
   * `: zzz` will find all derived from zzz, even `: virtual public zzz`
   * `short a;` will find also `short int unsigned a;` (`short` must be first in this version)

* Option to ignore keyboard accelerator hints (&, Win32 GUI feature) in strings
* Options to ignore all syntactic tokens, or braces, brackets or parentheses in particular
   * For commas or semicolons it's either all or trailing only
* Matching digraphs, trigraphs and ISO646 alternative tokens to primary tokens they represent
* Removes `*` and `/` decorations from comments before searching

## Example program

* [SearchTest.exe](https://github.com/tringi/code-style-agnostic-search/blob/main/test/SearchTest.exe?raw=true) (x64)  
   * start the program, load any short C++ file, and try searching; *mind interferences between options*
   * colorized variant of the code on the right shows the internal pattern (for debugging purposes)

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

* better approach to different notations for the same type `"unsigned int" == "int unsigned"`
   * `== "std::uint32_t"` (configurable plaftorm assumptions)
   * ignoring redundant
* match different order of declaration qualifiers, e.g.: `"static inline" == "inline static"`
* text string search
   * match different forms of escapes, e.g.: `\n == \013`
   * match escaped characters to actual characters
   * right now string contents are tokenized too
   * also do string literal combining

## Future

* wildcard `*` matching anything in between two code segments
* match reinterpret_cast/static_cast to C-style cast
* improve memory usage of token
   * union switched on type to merge exlusive members
   * `alternative` allocated only when necessary
* improve memory usage of pattern
   * vector instead of map will remove a lot of pointers and improve performance
