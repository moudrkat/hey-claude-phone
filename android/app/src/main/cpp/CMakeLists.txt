cmake_minimum_required(VERSION 3.22)
project(tinyhouse C)
# The local model: a 4M-parameter transformer (tiny/llm.h), its tokenizer and the interpreter of the small language
# it writes (tiny/house_lang.h). One C file, no dependencies.
add_library(tinyhouse SHARED tinyhouse.c)
target_compile_options(tinyhouse PRIVATE -O2 -Wno-unused-function)
target_link_libraries(tinyhouse m log)
