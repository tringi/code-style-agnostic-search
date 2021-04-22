# Code style -agnostic search for C++

TBD

## TODO

* ignore insignificant whitespace
   * including line endings
* match different forms of escapes, e.g.: `\n == \013`
* match escaped characters to actual characters
* match different number forms when value matches, `1 == 0x01`
* match different notations for the same type `"unsigned int" == "int unsigned"`
   * `== "std::uint32_t"` (configurable plaftorm assumptions)
   * ignore redundant
* match different order of declaration qualifiers, e.g.: `"static inline" == "inline static"`
* match reinterpret_cast/static_cast to C-style cast
* match curly braces and lack of them where appropriate, e.g. single statement after `if`
* all features optional
