#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
# SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
"""error_msg.py — 从 [SC] 错误码契约生成消息映射表与 gettext 目录

唯一数据源是 kernel/include/uapi/linux/airymax/error.h：每条
`#define AIRY_* <数值> /* 英文说明 */` 生成一条 {code, msgid, text}。
msgid 取错误码符号名，因此 .po 的键与契约一一对应，翻译不必理解数值
分段；运行时 airy_strerror() 先用 dgettext 查目录，未装目录时回退到
本表里的英文源文，任何部署形态都有可读文本。

手写副本会与契约分叉：改了错误码却忘记同步消息，用户看到的是旧语义。
因此映射表与模板都由本工具生成并签入，--check 重新生成后逐字比对，
任何漂移直接判负。

用法:
    python3 services/tools/error-msg/error_msg.py --check
    python3 services/tools/error-msg/error_msg.py --gen

选项:
    --root DIR       服务子仓根（默认：本脚本上两级）
    --header PATH    错误码头（默认：<服务子仓>/../kernel/.../error.h）
    --po-dir DIR     消息目录目录（默认：<服务子仓>/po）

退出码: 0 通过；1 检查失败；2 用法或输入错误。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_USAGE = 2

DOMAIN = "airymaxos"
POT_NAME = "airymaxos.pot"

# 契约头里成功码只有数值没有注释。补一条英文源文，避免生成器为单行
# 特判；新增无注释条目必须在此登记，否则生成直接失败。
FALLBACK_TEXT = {
    "AIRY_EOK": "Success",
}

DEFINE_RE = re.compile(
    r"^#define[ \t]+(AIRY_[A-Z0-9_]+)[ \t]+((?:0[xX])?[0-9]+)"
    r"(?:[ \t]*/\*[ \t]*(.*?)[ \t]*\*/)?[ \t]*$"
)
QUOTED_RE = re.compile(r'^"(.*)"\s*$')

TABLE_HEAD = """\
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
/*
 * Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
 *
 * error_msg_table.c — 错误码到消息标识的映射表（自动生成，请勿手改）
 *
 * 唯一数据源：kernel/include/uapi/linux/airymax/error.h [SC]
 * 生成器：services/tools/error-msg/error_msg.py
 * 重新生成：python3 services/tools/error-msg/error_msg.py --gen
 *
 * 错误码或注释变更后必须重新生成；ci/gates/i18n.sh 比对签入产物，
 * 漂移即判负，避免消息目录与 [SC] 契约各说各话。
 */
#include <stddef.h>

#include "error_msg_int.h"

struct airy_err_msg {
\tunsigned int code;
\tconst char *msgid;
\tconst char *text;
};

static const struct airy_err_msg g_err_msgs[] = {
"""

TABLE_TAIL = """\
};

static const struct airy_err_msg *find_msg(unsigned int code)
{
\tsize_t i;

\tfor (i = 0; i < sizeof(g_err_msgs) / sizeof(g_err_msgs[0]); i++) {
\t\tif (g_err_msgs[i].code == code)
\t\t\treturn &g_err_msgs[i];
\t}

\treturn NULL;
}

const char *airy_err_msgid(unsigned int code)
{
\tconst struct airy_err_msg *msg = find_msg(code);

\treturn msg != NULL ? msg->msgid : NULL;
}

const char *airy_err_text(unsigned int code)
{
\tconst struct airy_err_msg *msg = find_msg(code);

\treturn msg != NULL ? msg->text : NULL;
}
"""

POT_HEAD = """\
# AirymaxOS 错误消息模板（gettext 域 airymaxos）。
# 自动生成，请勿手改；重新生成：
#   python3 services/tools/error-msg/error_msg.py --gen
# 唯一数据源：kernel/include/uapi/linux/airymax/error.h [SC]
#, fuzzy
msgid ""
msgstr ""
"Project-Id-Version: AirymaxOS 0.0.5\\n"
"Report-Msgid-Bugs-To: wangliren@spharx.cn\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Content-Transfer-Encoding: 8bit\\n"
"X-Generator: error_msg.py\\n"

#. Unknown error code
msgid "Unknown error"
msgstr ""

"""


class InputError(Exception):
    """契约头不可读取或内容不满足生成前提。"""


@dataclass(frozen=True)
class ErrMsg:
    code: int
    name: str
    text: str


def default_root() -> Path:
    """服务子仓根：本脚本位于 <服务子仓>/tools/error-msg/。"""
    return Path(__file__).resolve().parents[2]


def default_header(root: Path) -> Path:
    return root.parent / "kernel/include/uapi/linux/airymax/error.h"


def parse_header(path: Path) -> list[ErrMsg]:
    """按头文件顺序收集错误码，拒绝重复、缺注释与非法数值。"""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise InputError(f"错误码头不可读取：{path}（{exc}）") from exc

    entries: list[ErrMsg] = []
    names: dict[str, int] = {}
    codes: dict[int, str] = {}
    problems: list[str] = []

    for raw in text.splitlines():
        match = DEFINE_RE.match(raw.strip())
        if not match:
            continue
        name, literal, desc = match.groups()
        code = int(literal, 0)
        if code > 0xFFFFFFFF:
            problems.append(f"{name} 数值超出 32 位无符号：{literal}")
            continue
        if name in names:
            problems.append(f"{name} 重复定义")
            continue
        if code in codes:
            problems.append(f"{name} 与 {codes[code]} 数值相同：{code}")
            continue
        text_desc = (desc or "").strip() or FALLBACK_TEXT.get(name, "")
        if not text_desc:
            problems.append(f"{name} 缺少注释且未登记英文源文")
            continue
        names[name] = code
        codes[code] = name
        entries.append(ErrMsg(code, name, text_desc))

    if problems:
        raise InputError("；".join(problems))
    if not entries:
        raise InputError(f"未解析到任何错误码：{path}")
    return entries


def c_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def render_entry(entry: ErrMsg) -> list[str]:
    """一行放得下就一行，否则把说明折到续行，保持 80 列以内。"""
    one = f'\t{{ {entry.code}u, "{entry.name}", "{c_string(entry.text)}" }},'
    if len(one) <= 79:
        return [one]

    two = [
        f'\t{{ {entry.code}u, "{entry.name}",',
        f'\t  "{c_string(entry.text)}" }},',
    ]
    if len(two[1]) > 79:
        raise InputError(f"{entry.name} 的说明过长，无法在 80 列内排版")
    return two


def render_table(entries: list[ErrMsg]) -> str:
    lines = TABLE_HEAD.splitlines()
    for entry in entries:
        lines.extend(render_entry(entry))
    lines.extend(TABLE_TAIL.splitlines())
    return "\n".join(lines) + "\n"


def render_pot(entries: list[ErrMsg]) -> str:
    parts = [POT_HEAD]
    for entry in entries:
        parts.append(
            f"#. {entry.text}\n"
            f'msgid "{entry.name}"\n'
            'msgstr ""\n\n'
        )
    return "".join(parts)


def unquote(text: str) -> str:
    """还原 .po 引号串里的标准转义。"""
    escapes = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}
    out: list[str] = []
    i = 0
    while i < len(text):
        char = text[i]
        if char == "\\" and i + 1 < len(text):
            out.append(escapes.get(text[i + 1], text[i + 1]))
            i += 2
        else:
            out.append(char)
            i += 1
    return "".join(out)


def parse_po(path: Path, problems: list[str]) -> tuple[dict[str, str], dict[str, str]]:
    """解析单行/多行 .po，返回（头部字段, msgid->msgstr）。"""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        problems.append(f"{path.name} 不可读取或不是 UTF-8：{exc}")
        return {}, {}

    header: dict[str, str] = {}
    entries: dict[str, str] = {}
    msgid: str | None = None
    msgstr: list[str] = []
    target = ""
    fuzzy = False

    def commit() -> None:
        nonlocal msgid, msgstr, fuzzy
        if msgid is None:
            return
        value = "".join(msgstr)
        if msgid == "":
            for line in value.splitlines():
                key, sep, rest = line.partition(":")
                if sep:
                    header[key.strip()] = rest.strip()
        elif fuzzy:
            problems.append(f"{path.name}: msgid 被标记 fuzzy：{msgid}")
        elif msgid in entries:
            problems.append(f"{path.name}: msgid 重复：{msgid}")
        else:
            entries[msgid] = value
        msgid, msgstr, fuzzy = None, [], False

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            if line.startswith("#,") and "fuzzy" in line:
                fuzzy = True
            continue
        if line.startswith("msgid "):
            commit()
            quoted = QUOTED_RE.match(line[len("msgid "):])
            if not quoted:
                problems.append(f"{path.name}:{lineno} msgid 行不可解析")
                continue
            msgid = unquote(quoted.group(1))
            target = "id"
            continue
        if line.startswith("msgstr "):
            quoted = QUOTED_RE.match(line[len("msgstr "):])
            if not quoted:
                problems.append(f"{path.name}:{lineno} msgstr 行不可解析")
                continue
            msgstr = [unquote(quoted.group(1))]
            target = "str"
            continue
        quoted = QUOTED_RE.match(line)
        if quoted and msgid is not None:
            if target == "str":
                msgstr.append(unquote(quoted.group(1)))
            else:
                msgid += unquote(quoted.group(1))
            continue
        problems.append(f"{path.name}:{lineno} 无法识别的行")

    commit()
    return header, entries


def check_catalog(path: Path, expected: set[str], problems: list[str]) -> None:
    header, entries = parse_po(path, problems)
    locale = path.stem
    if header.get("Language") != locale:
        problems.append(f"{path.name}: Language 应为 {locale}")
    charset = header.get("Content-Type", "").lower()
    if "charset=utf-8" not in charset:
        problems.append(f"{path.name}: Content-Type 不是 UTF-8")

    missing = sorted(expected - set(entries))
    if missing:
        problems.append(
            f"{path.name}: 缺少 {len(missing)} 条消息（{', '.join(missing[:4])}…）"
        )
    extra = sorted(set(entries) - expected)
    if extra:
        problems.append(
            f"{path.name}: 多出 {len(extra)} 条消息（{', '.join(extra[:4])}…）"
        )
    empty = sorted(key for key in expected & set(entries) if not entries[key])
    if empty:
        problems.append(
            f"{path.name}: {len(empty)} 条未翻译（{', '.join(empty[:4])}…）"
        )


def first_diff(name: str, current: str, expected: str) -> str:
    """给出首个漂移行，便于直接在本地定位。"""
    have = current.splitlines()
    want = expected.splitlines()
    for index, (left, right) in enumerate(zip(have, want), 1):
        if left != right:
            return f"{name}: 第 {index} 行漂移（签入 {left!r}，期望 {right!r}）"
    return f"{name}: 行数漂移（签入 {len(have)}，期望 {len(want)}）"


def write_file(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_gen(root: Path, header: Path, po_dir: Path) -> int:
    entries = parse_header(header)
    write_file(root / "common" / "error_msg_table.c", render_table(entries))
    write_file(po_dir / POT_NAME, render_pot(entries))
    print(f"AIRYMAX-I18N-GEN ok codes={len(entries)}")
    return EXIT_PASS


def run_check(root: Path, header: Path, po_dir: Path) -> int:
    entries = parse_header(header)
    problems: list[str] = []

    table_path = root / "common" / "error_msg_table.c"
    pot_path = po_dir / POT_NAME
    for path, rendered in (
        (table_path, render_table(entries)),
        (pot_path, render_pot(entries)),
    ):
        try:
            current = path.read_text(encoding="utf-8")
        except OSError as exc:
            problems.append(f"{path.name} 不可读取：{exc}")
            continue
        if current != rendered:
            problems.append(first_diff(path.name, current, rendered))

    catalogs = sorted(po_dir.glob("*.po"))
    if not catalogs:
        problems.append(f"{po_dir} 下没有任何 .po 目录")
    expected = {entry.name for entry in entries} | {"Unknown error"}
    for catalog in catalogs:
        check_catalog(catalog, expected, problems)

    if problems:
        for item in problems:
            print(f"error: {item}", file=sys.stderr)
        return EXIT_FAIL

    print(f"AIRYMAX-I18N-CHECK ok codes={len(entries)} locales={len(catalogs)}")
    return EXIT_PASS


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(add_help=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="比对签入产物（默认）")
    mode.add_argument("--gen", action="store_true", help="重新生成映射表与模板")
    parser.add_argument("--root", type=Path, help="服务子仓根")
    parser.add_argument("--header", type=Path, help="错误码头路径")
    parser.add_argument("--po-dir", type=Path, help="消息目录目录")
    args = parser.parse_args(argv)

    root = (args.root or default_root()).resolve()
    header = (args.header or default_header(root)).resolve()
    po_dir = (args.po_dir or root / "po").resolve()

    try:
        if args.gen:
            return run_gen(root, header, po_dir)
        return run_check(root, header, po_dir)
    except InputError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
