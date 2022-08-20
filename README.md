# Code style -agnostic search for C++

Relatively simple 
with various features:

## Features

* Ignores insignificant whitespace; including line endings.

* Individual partial words matching, on top of classic whole word matching on/off modes.  
  `stat nlin boo` == `static inline bool`

* Linguistic folding, diacritics and case insensitivity of tokens implemented through Windows API



## TODO

* match different forms of escapes, e.g.: `\n == \013`
* match escaped characters to actual characters
* match different number forms when value matches, `1 == 0x01`
* match different number types (optionally) "1.0f" match "1.0" or "7u" match "7LL"
   * optionally match even "123.0f" == "0x007BuLL"
* match different notations for the same type `"unsigned int" == "int unsigned"`
   * `== "std::uint32_t"` (configurable plaftorm assumptions)
   * ignore redundant
* match different order of declaration qualifiers, e.g.: `"static inline" == "inline static"`
* match reinterpret_cast/static_cast to C-style cast
* match curly braces and lack of them where appropriate, e.g. single statement after `if`
* ignore unnecessary semicolons, trailing commas
* ignore accelerator prefixes (&) in strings (resource)
* all features optional
