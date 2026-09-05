// Unit tests for core/src/stringlist.cc -- the singly-linked string-list
// type used throughout core for variable/dimension name lists.
#include <cstdio>
#include <cstring>

#include <doctest/doctest.h>

#include "ncview/includes.h"
#include "ncview/defines.h"
#include "ncview/protos.h"
#include "ncview/stringlist.h"

TEST_CASE("stringlist: add_string appends in order") {
    Stringlist *sl = nullptr;
    CHECK(stringlist_add_string(&sl, (char *)"one", nullptr, SLTYPE_NULL) == 0);
    CHECK(stringlist_add_string(&sl, (char *)"two", nullptr, SLTYPE_NULL) == 0);
    CHECK(stringlist_add_string(&sl, (char *)"three", nullptr, SLTYPE_NULL) == 0);
    REQUIRE(sl != nullptr);
    CHECK(std::strcmp(sl->string, "one") == 0);
    // Stringlist::next is an AnyPtr (see ncview/anyptr.h), not a raw
    // Stringlist*, so it converts implicitly on assignment but doesn't
    // support chained -> -- hop through explicit locals instead.
    Stringlist *second = sl->next;
    REQUIRE(second != nullptr);
    CHECK(std::strcmp(second->string, "two") == 0);
    Stringlist *third = second->next;
    REQUIRE(third != nullptr);
    CHECK(std::strcmp(third->string, "three") == 0);
    CHECK(third->next == nullptr);
    CHECK(stringlist_len(sl) == 3);
    stringlist_delete_entire_list(sl);
}

TEST_CASE("stringlist: empty list has zero length") {
    CHECK(stringlist_len(nullptr) == 0);
}

TEST_CASE("stringlist: match_string_exact finds and misses") {
    Stringlist *sl = nullptr;
    stringlist_add_string(&sl, (char *)"alpha", nullptr, SLTYPE_NULL);
    stringlist_add_string(&sl, (char *)"beta", nullptr, SLTYPE_NULL);

    Stringlist *hit = stringlist_match_string_exact(sl, (char *)"beta");
    REQUIRE(hit != nullptr);
    CHECK(std::strcmp(hit->string, "beta") == 0);

    CHECK(stringlist_match_string_exact(sl, (char *)"gamma") == nullptr);
    // Exact match: a substring or differently-cased match must not hit.
    CHECK(stringlist_match_string_exact(sl, (char *)"bet") == nullptr);
    CHECK(stringlist_match_string_exact(sl, (char *)"BETA") == nullptr);

    stringlist_delete_entire_list(sl);
}

TEST_CASE("stringlist: cat appends second list to first") {
    Stringlist *a = nullptr, *b = nullptr;
    stringlist_add_string(&a, (char *)"a1", nullptr, SLTYPE_NULL);
    stringlist_add_string(&a, (char *)"a2", nullptr, SLTYPE_NULL);
    stringlist_add_string(&b, (char *)"b1", nullptr, SLTYPE_NULL);

    CHECK(stringlist_cat(&a, &b) == 0);
    CHECK(stringlist_len(a) == 3);
    Stringlist *a_next = a->next;
    REQUIRE(a_next != nullptr);
    Stringlist *a_third = a_next->next;
    REQUIRE(a_third != nullptr);
    CHECK(std::strcmp(a_third->string, "b1") == 0);

    stringlist_delete_entire_list(a);
}

TEST_CASE("stringlist: write then read round-trips through a file") {
    Stringlist *sl = nullptr;
    stringlist_add_string(&sl, (char *)"round", nullptr, SLTYPE_NULL);
    stringlist_add_string(&sl, (char *)"trip", nullptr, SLTYPE_NULL);

    FILE *f = std::tmpfile();
    REQUIRE(f != nullptr);
    CHECK(stringlist_write_to_file(sl, f) == 0);
    std::rewind(f);

    Stringlist *readback = nullptr;
    CHECK(stringlist_read_from_file(&readback, f) == 0);
    CHECK(stringlist_len(readback) == 2);
    REQUIRE(readback != nullptr);
    CHECK(std::strcmp(readback->string, "round") == 0);
    Stringlist *readback_next = readback->next;
    REQUIRE(readback_next != nullptr);
    CHECK(std::strcmp(readback_next->string, "trip") == 0);

    std::fclose(f);
    stringlist_delete_entire_list(sl);
    stringlist_delete_entire_list(readback);
}
