import json
import subprocess
import textwrap

import pytest

from uppercut_c_style import (
    CStyleError,
    StyleConfig,
    check_paths,
    check_repo,
    config_from_mapping,
    find_config,
    list_sources,
    load_config,
    main,
    strip_literals,
)


def write(root, name, text):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text).lstrip("\n"), encoding="utf-8")
    return path


@pytest.fixture
def repo(tmp_path):
    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    write(tmp_path, "types.h", "typedef unsigned char u8;\n")
    write(tmp_path, ".gitignore", "build/\n")
    return tmp_path


def messages(result):
    return [str(finding) for finding in result.findings]


GOOD = """
#include "limits.h"
static const u8* first(const u8* bytes, size_t size) {
    for (u8 i = 0; i < COUNT; i++) {
        if (bytes[i]) return bytes + i;
    }
    return 0;
}
int main(int argc, char** argv) { return 0; }
"""


def test_clean_sources_pass(repo):
    write(repo, "limits.h", "#define COUNT 16\n")
    write(repo, "game.c", GOOD)
    config = StyleConfig(exclude=("types.h",))
    result = check_repo(repo, config)
    assert result.ok
    assert result.checked == 2


def test_integer_spellings_are_reported_with_the_label(repo):
    write(repo, "game.c", """
    int score;
    unsigned long frames;
    uint32_t ticks;
    s16 offset;
    i32 fine;
    """)
    result = check_repo(repo, StyleConfig(exclude=("types.h",), label="lab"))
    assert messages(result) == [
        "game.c:1: use a lab fixed-width type for int",
        "game.c:2: use a lab fixed-width type for unsigned",
        "game.c:3: use a lab fixed-width type for uint32_t",
        "game.c:4: use a lab fixed-width type for s16",
    ]


def test_comments_strings_casts_and_main_are_not_findings(repo):
    write(repo, "game.c", """
    // int in a comment
    /* long
       short */
    const char* text = "unsigned int";
    i32 value = (int32_t)other;
    int main(int argc, char** argv) { return 0; }
    """)
    assert check_repo(repo, StyleConfig(exclude=("types.h",))).ok


def test_pointer_stars_go_on_the_type(repo):
    write(repo, "game.c", """
    typedef struct { u8 x; } Point;
    u8 *bytes;
    Point * where;
    char *name;
    const Point* fine;
    i32 product = a * b;
    """)
    result = check_repo(repo, StyleConfig(exclude=("types.h",)))
    assert messages(result) == [
        "game.c:2: attach the pointer to its type",
        "game.c:3: attach the pointer to its type",
        "game.c:4: attach the pointer to its type",
    ]


def test_byte_range_loops(repo):
    write(repo, "sizes.h", "#define SLOTS 8\n#define WIDE 300\n")
    write(repo, "game.c", """
    #include "sizes.h"
    void run(void) {
        for (i32 i = 0; i < 16; i++) {}
        for (size_t i = 0; i < SLOTS; i++) {}
        for (u8 i = 0; i <= 255; i++) {}
        for (u8 i = 0; i < 0x100; i++) {}
        for (i32 i = 0; i < WIDE; i++) {}
        for (u8 i = 0; i < 255; i++) {}
        for (i32 i = 0; i < UNKNOWN; i++) {}
    }
    """)
    result = check_repo(repo, StyleConfig(exclude=("types.h",)))
    assert messages(result) == [
        "game.c:3: use u8 for the byte-range loop",
        "game.c:4: use u8 for the byte-range loop",
        "game.c:5: use an explicit exit before the u8 index wraps",
        "game.c:6: use an explicit exit before the u8 index wraps",
    ]


def test_a_macro_defined_twice_with_different_values_is_skipped(repo):
    write(repo, "a.h", "#define N 4\n")
    write(repo, "b.h", "#define N 9\n")
    write(repo, "game.c", '#include "a.h"\n#include "b.h"\nvoid f(void) { for (i32 i = 0; i < N; i++) {} }\n')
    assert check_repo(repo, StyleConfig(exclude=("types.h",))).ok


def test_abi_signatures_are_exempt_but_their_bodies_are_not(repo):
    write(repo, "audio/stream.c", """
    static void stream_pcm(void* user, unsigned char* buffer, int length)
    {
        int left = length;
    }
    void other(int value) {}
    """)
    config = StyleConfig(exclude=("types.h",), abi={"audio/stream.c": ("stream_pcm",)})
    assert messages(check_repo(repo, config)) == [
        "audio/stream.c:3: use a fixed-width type for int",
        "audio/stream.c:5: use a fixed-width type for int",
    ]


def test_excludes_suffixes_and_ignored_files(repo):
    write(repo, "vendor/lib/zlib.c", "int crc;\n")
    write(repo, "build/out.c", "int generated;\n")
    write(repo, "notes.txt", "int\n")
    write(repo, "shaders.inl", "int bad;\n")
    config = StyleConfig(exclude=("types.h", "vendor/*"))
    assert list_sources(repo, config) == ["shaders.inl"]
    assert list_sources(repo, StyleConfig(exclude=("types.h", "vendor/*"), untracked=False)) == []
    assert list_sources(repo, StyleConfig(exclude=("types.h",), suffixes=(".c",))) == ["vendor/lib/zlib.c"]


def test_pathspecs_limit_the_files(repo):
    write(repo, "a/one.c", "i32 x;\n")
    write(repo, "b/two.c", "int y;\n")
    config = StyleConfig(exclude=("types.h",))
    assert check_repo(repo, config, ["a"]).ok
    assert StyleConfig(include=("a",)).include == ("a",)
    assert check_repo(repo, StyleConfig(exclude=("types.h",), include=("a",))).checked == 1


def test_other_alias_names(repo):
    write(repo, "game.c", "for (U8 i = 0; i < 4; i++) {}\nfor (S32 i = 0; i < 4; i++) {}\nU8 *p;\n")
    config = StyleConfig(types=("U8", "S32"), byte_type="U8", exclude=("types.h",))
    assert messages(check_repo(repo, config)) == [
        "game.c:3: attach the pointer to its type",
        "game.c:2: use U8 for the byte-range loop",
    ]


def test_check_paths_reads_only_the_named_files(repo):
    write(repo, "game.c", "int x;\n")
    write(repo, "other.c", "int y;\n")
    assert messages(check_paths(repo, ["other.c"], StyleConfig())) == ["other.c:1: use a fixed-width type for int"]


def test_strip_literals_keeps_line_numbers():
    text = '/* a\nb */ x = "s\\"q"; // c\n'
    stripped = strip_literals(text)
    assert stripped.count("\n") == text.count("\n")
    assert "a" not in stripped and "q" not in stripped and "x =" in stripped


def test_config_from_pyproject_toml_and_json(tmp_path):
    (tmp_path / "pyproject.toml").write_text(
        '[tool.uppercut-c-style]\nlabel = "lab"\nexclude = ["shared/lab_types.h"]\n'
        '[tool.uppercut-c-style.abi]\n"shared/audio.c" = ["stream_pcm"]\n',
        encoding="utf-8",
    )
    config = find_config(tmp_path)
    assert config.label == "lab"
    assert config.exclude == ("shared/lab_types.h",)
    assert config.abi == {"shared/audio.c": ("stream_pcm",)}
    other = tmp_path / "style.json"
    other.write_text(json.dumps({"types": ["u8", "u16"], "byte-type": "u8", "untracked": False}), encoding="utf-8")
    assert load_config(other) == StyleConfig(types=("u8", "u16"), untracked=False)


def test_find_config_order_and_defaults(tmp_path):
    assert find_config(tmp_path) == StyleConfig()
    (tmp_path / "pyproject.toml").write_text('[project]\nname = "x"\n', encoding="utf-8")
    (tmp_path / "uppercut-c-style.toml").write_text('label = "game"\n', encoding="utf-8")
    assert find_config(tmp_path).label == "game"


@pytest.mark.parametrize(
    "table, message",
    [
        ({"colour": "red"}, "unknown setting 'colour'"),
        ({"types": "u8"}, "types must be a list of strings"),
        ({"byte-type": "u9"}, "byte-type 'u9' must be one of the types"),
        ({"types": ["u 8"]}, "is not a C identifier"),
        ({"abi": {"a.c": ["bad name"]}}, "is not a C identifier"),
        ({"untracked": "yes"}, "untracked must be true or false"),
    ],
)
def test_bad_settings_are_refused(table, message):
    with pytest.raises(CStyleError, match=message):
        config_from_mapping(table)


def test_command_line_reports_findings_and_exit_codes(repo, capsys):
    write(repo, "uppercut-c-style.toml", 'exclude = ["types.h"]\n')
    write(repo, "game.c", "i32 fine;\n")
    assert main(["--root", str(repo)]) == 0
    assert capsys.readouterr().out == "Checked 1 C sources and headers\n"
    write(repo, "bad.c", "long x;\n")
    assert main(["--root", str(repo)]) == 1
    assert capsys.readouterr().out == "bad.c:1: use a fixed-width type for long\n"
    assert main(["--root", str(repo), "game.c"]) == 0
    capsys.readouterr()
    assert main(["--root", str(repo), "missing"]) == 1
    assert capsys.readouterr().err == "No C sources found\n"


def test_command_line_finds_the_root_from_the_config(repo, capsys):
    config = write(repo, "style.json", json.dumps({"exclude": ["types.h"], "label": "lab"}))
    write(repo, "game.c", "short x;\n")
    assert main(["--config", str(config)]) == 1
    assert capsys.readouterr().out == "game.c:1: use a lab fixed-width type for short\n"


def test_command_line_outside_a_repository(tmp_path, capsys):
    assert main(["--root", str(tmp_path)]) == 1
    assert capsys.readouterr().err.startswith("uppercut-c-style: ")


def test_module_entry_point(repo):
    write(repo, "uppercut-c-style.toml", 'exclude = ["types.h"]\n')
    write(repo, "game.c", "int x;\n")
    run = subprocess.run(
        [__import__("sys").executable, "-m", "uppercut_c_style"], cwd=repo, capture_output=True, text=True
    )
    assert run.returncode == 1
    assert run.stdout == "game.c:1: use a fixed-width type for int\n"
