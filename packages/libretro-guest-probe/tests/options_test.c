/* The core option table: patterns with *, first match wins, each answer reported once. */
#include <string.h>

#include "check.h"
#include "lgp/lgp.h"

int main(void)
{
    LgpOptions *o = lgp_options_new();
    int first;
    const char *v;

    CHECK(lgp_glob_match("abc", "abc") && !lgp_glob_match("abc", "abcd") && !lgp_glob_match("abc", "ab"), "exact");
    CHECK(lgp_glob_match("*", "") && lgp_glob_match("*", "anything"), "a lone star");
    CHECK(lgp_glob_match("*serial*", "flycast_serial_console") && lgp_glob_match("*serial*", "serial") &&
              !lgp_glob_match("*serial*", "flycast_seria"),
          "contains");
    CHECK(lgp_glob_match("a*c", "abc") && lgp_glob_match("a*c", "ac") && lgp_glob_match("a*c", "abcbc") &&
              !lgp_glob_match("a*c", "abcb"),
          "a star in the middle backs up");
    CHECK(lgp_glob_match("a**b", "ab") && lgp_glob_match("*a*b*", "xxaxxbxx"), "several stars");

    CHECK(lgp_options_add(o, "*serial*", "enabled") && lgp_options_add(o, "*allow_service_buttons*", "enabled"), "add");
    CHECK(lgp_options_parse(o, "exact_key=a=b") && lgp_options_parse(o, "*=fallback"), "parse keeps text after the first =");
    CHECK(!lgp_options_parse(o, "no equals") && !lgp_options_parse(o, "=value") && !lgp_options_add(o, "", "x") &&
              !lgp_options_add(o, "k", NULL) && !lgp_options_add(NULL, "k", "v"),
          "bad input is refused");
    CHECK(lgp_options_count(o) == 4, "four entries");

    v = lgp_options_get(o, "flycast_serial_console", &first);
    CHECK(v && strcmp(v, "enabled") == 0 && first == 1, "a pattern answers and the first answer is flagged");
    v = lgp_options_get(o, "flycast_serial_console", &first);
    CHECK(v && first == 0, "the second answer for the same key is not");
    v = lgp_options_get(o, "other_serial", &first);
    CHECK(v && first == 1, "another key is first again");
    v = lgp_options_get(o, "exact_key", &first);
    CHECK(v && strcmp(v, "a=b") == 0, "an exact entry");
    v = lgp_options_get(o, "unmatched", NULL);
    CHECK(v && strcmp(v, "fallback") == 0, "the catch-all, with no `first` wanted");
    lgp_options_free(o);

    o = lgp_options_new();
    CHECK(lgp_options_get(o, "k", &first) == NULL && first == 0, "an empty table answers nothing");
    lgp_options_add(o, "k", "one");
    lgp_options_add(o, "k", "two");
    v = lgp_options_get(o, "k", NULL);
    CHECK(v && strcmp(v, "one") == 0, "the first matching entry wins");
    CHECK(lgp_options_get(NULL, "k", &first) == NULL, "a NULL table");
    lgp_options_free(o);
    lgp_options_free(NULL);

    printf("options_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
