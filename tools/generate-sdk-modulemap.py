#!/usr/bin/env python3
"""Generate the Clang module maps for Darling's open-source MacOSX.sdk.

Darling's SDK is a header tree assembled from the components of this repository.
Apple's SDK ships a curated Clang module map family on top of the same headers
(the master usr/include/module.modulemap and the Darwin*/c_standard_library/
os/Dispatch/ObjectiveC maps it references), and the Swift SDK overlays need that
structure: libswiftDarwin's Darwin.swift starts with `@_exported import Darwin`,
which only resolves when usr/include carries a module map naming the Clang module
Darwin.

The module structure is not copied from Apple's SDK at build time and no Apple SDK
is needed to run this tool: the maps it consumes live in Darwin's tree, under
tools/sdk-module-maps/. They are Apple's files for this SDK's header tree - a
module map is a description of the headers' interface, so it has the same
provenance as the 1921 vendored headers it describes - with two kinds of change,
each marked in the file:

  * a map Apple has and this tree needs is not carried at all (below), and
  * math.h is claimed inside Darwin rather than under _DarwinFoundation1._math.
    Apple files it under an explicit submodule of a different top-level module,
    so `import Darwin` does not carry it and the Swift Darwin overlay cannot see
    the C math surface its tgmath.swift needs (measured: "cannot find 'acosf' in
    scope"). With it claimed by Darwin, tgmath.swift compiles.

No API notes are taken; the three that this tree carries stay where their
components put them, and this tool says below which of them the SDK cannot see.

Three of Apple's maps are deliberately not carried:

  * os.modulemap. Darling's os/ headers come from XNU (os/trace.h is
    libkern/os/trace.h, which includes <xpc/xpc.h> under __has_include) and
    libtrace/libdispatch. Kept as a top-level module, `os` is mutually recursive
    with Darwin: Darwin -> os -> XPC -> Dispatch -> Darwin, which Clang rejects
    ("cyclic dependency in module 'Darwin'"). The modules Darwin itself needs
    from that set - os/base.h, os/clock.h, os/lock.h, os/proc.h - are the
    Darwin.os submodules Apple's own Darwin.modulemap declares, so Darwin keeps
    them; the rest of os/ stays includable, just not as a module. The os
    overlay's `@_exported import os` therefore needs its own emitted map before
    it can build.
  * DarwinBasic.modulemap, which defines MachO. Darling's mach-o headers reach
    Darwin through <mach/machine.h>, so MachO -> Darwin, while Darwin's own
    os/trace_base.h reaches <mach-o/loader.h>: the same cycle one level down.
  * kcdata.modulemap, a kernel-only interface whose headers include Darwin's.

The other maps the recipe names are emitted from their Apple structure:

  * Dispatch: dispatch.modulemap, top-level. Its .apinotes is NOT reachable
    (src/external/libdispatch/src/swift/Dispatch.apinotes is outside the SDK),
    so an SDK that needs the Swift names must expose it as
    usr/include/Dispatch.apinotes; this tool does not invent API notes.
  * ObjectiveC: ObjectiveC.modulemap, top-level, over the objc4 headers. Its
    .apinotes (objc4/runtime/Module/ObjectiveC.apinotes) is likewise outside the
    SDK and is not emitted.
  * CoreFoundation is not a usr/include module at all: Apple declares it as a
    framework module (umbrella header CoreFoundation.h) in
    CoreFoundation.framework/Modules/module.modulemap, so it needs that file,
    not this one.
  * XPC and os: XPC.modulemap is emitted; os is covered above.

Header files this tree cannot present as module headers are listed in
NON_MODULAR_HEADERS and dropped, and any module left without headers, umbrella
or children is dropped with them.

A module map is only valid against the header set it is filtered for. Passing
Apple's maps straight through fails twice, both measured:

  * Apple's maps name 1683 headers, but the SDK only has 1921 headers of its own
    and many named ones (the _modules/_*.h wrappers, for instance) do not exist
    here. Clang rejects a map that names a missing header.
  * A map that survives by deleting the missing headers can still be left with an
    empty module, or with `umbrella "dir"` whose directory has no header left;
    Clang rejects both ("inferred submodules require a module with an umbrella").

So this tool parses the module map grammar, drops every header, umbrella and
module that the SDK's headers cannot satisfy, and emits the result. It never
emits an umbrella over all of usr/include: usr/include holds headers that are
not modules (BerkeleyDB/db_cxx.h needs iostream.h), which is why Apple curates
the map per file instead of using a directory umbrella for it.

A module that requires a feature nothing will define is dropped rather than
emitted as a trap the client cannot avoid (see Filter.satisfiable).

Usage:
  tools/generate-sdk-modulemap.py --sdk-include <SDK usr/include>

Run from the directory of this script's checkout, or pass --template-dir and
--output-dir explicitly. Exits non-zero, naming the path, when the SDK header
tree is missing.
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
DEFAULT_TEMPLATE_DIR = os.path.join(SCRIPT_DIR, "sdk-module-maps")

# Header files that exist in the SDK but cannot be module headers, so the
# templates' entries for them are dropped. Apple's SDK ships post-processed
# copies of these XNU sources; Darling's SDK carries the XNU source versions,
# and each of them either does not compile as part of its module or pulls a
# kernel-only include chain across module boundaries. Kept explicit rather than
# inferred: each name is a fact about this tree, and the contract test checks
# that the emitted maps never name one.
NON_MODULAR_HEADERS = frozenset(
    [
        # Includes <net/multi_layer_pkt_log.h>, whose <os/log.h> reaches
        # Darwin -> os -> XPC -> Dispatch -> Darwin - a cycle Clang rejects.
        "netinet6/esp.h",
        # Needs <netinet/ip.h> first, which it does not include.
        "netinet/bootp.h",
        # Needs <netinet/ip_icmp.h> first (ICMP_MAXTYPE), which it does not include.
        "netinet/icmp_var.h",
        # Includes "mach/i386/asm.h", which this tree does not have.
        "mach/machine/asm.h",
        # Redefines what libkern/i386/OSByteOrder.h already defined.
        "libkern/machine/OSByteOrder.h",
        # Includes bsm/audit_internal.h, which this tree does not have.
        "bsm/audit_filter.h",
        # This tree's libkern/OSKextLib.h is XNU's, which includes
        # <CoreFoundation/CoreFoundation.h>; CoreFoundation's own CFBase.h
        # includes <libkern/OSTypes.h>, so an owning module closes the cycle
        # CoreFoundation -> libkern -> CoreFoundation, which Clang rejects.
        "libkern/OSKextLib.h",
    ]
)
# Module features a language mode or a normal build defines. A module that
# requires one of these is available in that mode; a module that positively
# requires anything else can never be imported and is dropped (see
# Filter.satisfiable). The names are the ones Clang and the Swift driver pass:
# Clang defines objc/objc_arc/blocks when the language uses them, C++ modes
# define cplusplus*, and the Swift driver defines swift when importing a Clang
# module.
KNOWN_FEATURES = frozenset(
    [
        "altivec",
        "blocks",
        "cplusplus",
        "cplusplus11",
        "cplusplus14",
        "cplusplus17",
        "cplusplus20",
        "objc",
        "objc_arc",
        "swift",
        "zvector",
    ]
)

# ---------------------------------------------------------------------------
# Lexer


class ModuleMapError(Exception):
    """A syntax error or an unsatisfiable module map request."""


@dataclass
class Token:
    kind: str  # 'word', 'string', 'punct'
    value: str
    line: int


_PUNCT = "{}[]!*,"
_IDENT_START = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")


def lex(text: str, path: str) -> List[Token]:
    """Tokenize a module map. Comments are dropped; strings stay one token.

    Identifiers may contain '.', which module names use for nesting
    (module Darwin.POSIX) and for nothing else.
    """
    tokens: List[Token] = []
    i = 0
    line = 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c in " \t\r\f\v":
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ModuleMapError("%s:%d: unterminated comment" % (path, line))
            line += text.count("\n", i, end)
            i = end + 2
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    j += 2
                elif text[j] == "\n":
                    raise ModuleMapError("%s:%d: newline in string" % (path, line))
                else:
                    j += 1
            if j >= n:
                raise ModuleMapError("%s:%d: unterminated string" % (path, line))
            tokens.append(Token("string", text[i + 1 : j], line))
            i = j + 1
        elif c in _PUNCT:
            tokens.append(Token("punct", c, line))
            i += 1
        elif c in _IDENT_START:
            j = i
            while j < n and (text[j].isalnum() or text[j] in "_."):
                j += 1
            tokens.append(Token("word", text[i:j], line))
            i = j
        else:
            raise ModuleMapError(
                "%s:%d: unexpected character %r" % (path, line, c)
            )
    return tokens


# ---------------------------------------------------------------------------
# Syntax tree


@dataclass
class Header:
    path: str
    textual: bool = False
    private: bool = False


@dataclass
class Umbrella:
    directory: Optional[str] = None
    header: Optional[str] = None


@dataclass
class ExcludeHeader:
    path: str


@dataclass
class Export:
    spec: str  # '*' or a module name


@dataclass
class Requires:
    clause: List[str]  # e.g. ['!swift'] or ['objc']


@dataclass
class Link:
    args: List[str]


@dataclass
class Module:
    name: str  # dotted path, or '*' for the inferred-submodule module
    explicit: bool = False
    attrs: List[str] = field(default_factory=list)
    members: List[object] = field(default_factory=list)
    # A framework module map declares its module with `framework module`, and
    # Clang resolves the header paths in it against the framework's Headers.
    framework: bool = False


@dataclass
class ExternModule:
    name: str
    file: str


class Parser:
    """Recursive-descent parser for the Clang module map grammar.

    Only the productions Darling's maps use are implemented; anything else is a
    hard error so grammar drift is noticed instead of silently mis-parsed.
    """

    def __init__(self, tokens: Sequence[Token], path: str):
        self.tokens = tokens
        self.pos = 0
        self.path = path

    def error(self, message: str) -> ModuleMapError:
        line = self.tokens[self.pos].line if self.pos < len(self.tokens) else 0
        return ModuleMapError("%s:%d: %s" % (self.path, line, message))

    def peek(self) -> Optional[Token]:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def next(self) -> Token:
        if self.pos >= len(self.tokens):
            raise self.error("unexpected end of file")
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def accept_punct(self, value: str) -> bool:
        token = self.peek()
        if token is not None and token.kind == "punct" and token.value == value:
            self.pos += 1
            return True
        return False

    def expect_string(self, what: str) -> str:
        token = self.next()
        if token.kind != "string":
            raise self.error("expected a quoted %s, got %r" % (what, token.value))
        return token.value

    def expect_word(self, what: str) -> str:
        token = self.next()
        if token.kind != "word":
            raise self.error("expected %s, got %r" % (what, token.value))
        return token.value

    def parse(self) -> List[object]:
        members = self.parse_members(top_level=True)
        if self.pos != len(self.tokens):
            raise self.error("trailing tokens")
        return members

    def parse_members(self, top_level: bool = False) -> List[object]:
        members: List[object] = []
        while True:
            token = self.peek()
            if token is None:
                if not top_level:
                    raise self.error("expected '}'")
                return members
            if token.kind == "punct" and token.value == "}":
                if top_level:
                    raise self.error("unexpected '}'")
                return members
            members.append(self.parse_member())

    def parse_feature(self) -> str:
        """One feature expression of a requires clause, e.g. 'objc' or '!swift'."""
        if self.accept_punct("!"):
            return "!" + self.expect_word("a feature name")
        return self.expect_word("a feature name")

    def parse_attributes(self) -> List[str]:
        attrs: List[str] = []
        if not self.accept_punct("["):
            return attrs
        while not self.accept_punct("]"):
            attrs.append(self.expect_word("an attribute"))
        return attrs

    def parse_member(self) -> object:
        private = False
        if self.peek() is not None and self.peek().kind == "word":
            if self.peek().value == "private":
                self.next()
                private = True
        token = self.peek()
        if token is None or token.kind != "word":
            raise self.error("expected a declaration")
        keyword = token.value

        if keyword == "header":
            self.next()
            return Header(self.expect_string("header path"), private=private)
        if keyword == "textual":
            self.next()
            if self.expect_word("'header'") != "header":
                raise self.error("expected 'header' after 'textual'")
            return Header(self.expect_string("header path"), textual=True, private=private)
        if keyword == "umbrella":
            self.next()
            if self.peek() is not None and self.peek().value == "header":
                self.next()
                return Umbrella(header=self.expect_string("umbrella header path"))
            return Umbrella(directory=self.expect_string("umbrella directory"))
        if keyword == "exclude":
            self.next()
            if self.expect_word("'header'") != "header":
                raise self.error("expected 'header' after 'exclude'")
            return ExcludeHeader(self.expect_string("header path"))
        if keyword == "export":
            self.next()
            token = self.next()
            if token.kind == "punct" and token.value == "*":
                return Export("*")
            if token.kind == "word":
                return Export(token.value)
            raise self.error("expected '*' or a module name after 'export'")
        if keyword == "requires":
            self.next()
            clause: List[str] = []
            while True:
                clause.append(self.parse_feature())
                if not self.accept_punct(","):
                    break
            return Requires(clause)
        if keyword == "link":
            self.next()
            args: List[str] = []
            if self.peek() is not None and self.peek().value == "framework":
                self.next()
                args.append("framework")
            args.append(self.expect_string("library name"))
            return Link(args)
        if keyword == "extern":
            self.next()
            if self.expect_word("'module'") != "module":
                raise self.error("expected 'module' after 'extern'")
            name = self.expect_word("a module name")
            return ExternModule(name, self.expect_string("a module map file"))
        if keyword == "module":
            self.next()
            return self.parse_module_body(False)
        if keyword == "framework":
            self.next()
            if self.expect_word("'module'") != "module":
                raise self.error("expected 'module' after 'framework'")
            return self.parse_module_body(False, framework=True)
        if keyword == "explicit":
            self.next()
            if self.expect_word("'module'") != "module":
                raise self.error("expected 'module' after 'explicit'")
            return self.parse_module_body(True)
        if keyword == "config_macros":
            self.next()
            raise self.error("config_macros is not supported")
        if keyword == "conflict":
            self.next()
            self.expect_word("a module name")
            return None
        if keyword == "infer":
            self.next()
            self.expect_word("'import'")
            self.expect_string("a module name")
            return None
        raise self.error("unsupported declaration %r" % keyword)

    def parse_module_body(self, explicit: bool, framework: bool = False) -> Module:
        name_token = self.next()
        if name_token.kind == "punct" and name_token.value == "*":
            name = "*"
        elif name_token.kind == "word":
            name = name_token.value
        else:
            raise self.error("expected a module name, got %r" % name_token.value)
        attrs = self.parse_attributes()
        if not self.accept_punct("{"):
            raise self.error("expected '{' after module %s" % name)
        members = self.parse_members()
        if not self.accept_punct("}"):
            raise self.error("expected '}' at the end of module %s" % name)
        members = [m for m in members if m is not None]
        return Module(name, explicit=explicit, attrs=attrs, members=members, framework=framework)


# ---------------------------------------------------------------------------
# Filtering


class Filter:
    """Filters a parsed module map against the headers the SDK actually has.

    A reference is kept only when the SDK can satisfy it: a header that exists,
    an umbrella directory that exists, an extern module whose file will be
    emitted. Modules that lose every header, umbrella and child are dropped, so
    the emitted map never mentions something the tree does not have.
    """

    def __init__(self, include_dir: str, emitted_files: set, features: set, dropped: set):
        self.include_dir = include_dir
        self.emitted_files = emitted_files
        self.features = features
        self.dropped = dropped

    def satisfiable(self, module: Module) -> bool:
        """False when the module requires a feature nothing will ever define.

        A module map may require features the language mode or the build defines
        (objc, blocks, swift, ...). A positive require on anything else can never
        be satisfied, so the module is unimportable and emitting it would only
        turn every client into an error: Apple's
        `_c_standard_library_obsolete` exists to fail a client whose search
        paths resolve stddef.h to the SDK instead of to Clang's own headers, and
        Darling's SDK does ship stddef.h, so that module always fires. It is
        dropped here instead. --feature adds a feature a caller does define.
        """
        for member in module.members:
            if isinstance(member, Requires):
                for feature in member.clause:
                    if not feature.startswith("!") and feature not in self.features:
                        return False
        return True

    def has_header(self, relative: str) -> bool:
        if relative in self.dropped:
            return False
        return os.path.isfile(os.path.join(self.include_dir, relative))

    def has_directory(self, relative: str) -> bool:
        return os.path.isdir(os.path.join(self.include_dir, relative))

    def umbrella_headers(self, directory: str) -> List[str]:
        """The directory's own headers, as the umbrella would sweep them in."""
        root = os.path.join(self.include_dir, directory)
        headers: List[str] = []
        for base, _dirs, files in os.walk(root):
            for name in files:
                headers.append(os.path.relpath(os.path.join(base, name), self.include_dir))
        return sorted(headers)

    def umbrella_survives(self, umbrella: Umbrella) -> bool:
        """True when this umbrella still has something to cover here.

        A directory umbrella needs at least one header this tree can make a
        module header; Clang rejects an umbrella module that ends up with no
        headers, and an inferred-submodule module needs a surviving umbrella.
        """
        if umbrella.header is not None:
            return self.has_header(umbrella.header)
        if umbrella.directory is None or not self.has_directory(umbrella.directory):
            return False
        return any(
            self.has_header(path) for path in self.umbrella_headers(umbrella.directory)
        )

    def filter_members(self, members: Sequence[object], umbrella: bool) -> List[object]:
        kept: List[object] = []
        for member in members:
            if isinstance(member, Header):
                if self.has_header(member.path):
                    kept.append(member)
            elif isinstance(member, ExcludeHeader):
                if self.has_header(member.path):
                    kept.append(member)
            elif isinstance(member, Umbrella):
                if not self.umbrella_survives(member):
                    continue
                kept.append(member)
                if member.directory:
                    # Clang infers a submodule for every header the umbrella
                    # directory holds. Name the ones this tree cannot make module
                    # headers (objc4's C++ helpers need <cstddef>) or the template
                    # already excludes in an `exclude header`, so no inferred
                    # submodule can sweep them in.
                    named = {m.path for m in members if isinstance(m, ExcludeHeader)}
                    kept.extend(
                        ExcludeHeader(path)
                        for path in self.umbrella_headers(member.directory)
                        if path not in named and not self.has_header(path)
                    )
            elif isinstance(member, ExternModule):
                if member.file in self.emitted_files:
                    kept.append(member)
            elif isinstance(member, Module):
                if member.name == "*":
                    # Clang infers a submodule per header from the enclosing
                    # module's umbrella and rejects `module *` without one
                    # ("inferred submodules require a module with an umbrella").
                    # Its own member list is empty by construction, so it is kept
                    # as declared rather than judged for emptiness.
                    if not umbrella:
                        continue
                    kept.append(
                        Module(member.name, member.explicit, list(member.attrs),
                               self.filter_members(member.members, True), member.framework)
                    )
                    continue
                child = self.filter_module(member)
                if child is not None:
                    kept.append(child)
            elif isinstance(member, (Export, Requires, Link)):
                kept.append(member)
            else:  # pragma: no cover - parser only produces the types above
                raise AssertionError("unhandled member %r" % (member,))
        return kept

    def filter_module(self, module: Module) -> Optional[Module]:
        if not self.satisfiable(module):
            return None
        # An inferred-submodule module (`module *`) requires an umbrella: Clang
        # rejects it otherwise ("inferred submodules require a module with an
        # umbrella"). The umbrella is a sibling in the same module, so check it
        # before filtering the children, and drop `module *` when the sibling is
        # not there or does not survive this tree's headers.
        has_umbrella = any(
            isinstance(m, Umbrella) and self.umbrella_survives(m) for m in module.members
        )
        children = self.filter_members(module.members, has_umbrella)
        kept: List[object] = []
        for child in children:
            if isinstance(child, Module) and child.name == "*" and not has_umbrella:
                continue
            kept.append(child)
        has_header = any(isinstance(m, (Header, Umbrella)) for m in kept)
        has_module = any(isinstance(m, Module) for m in kept)
        has_extern = any(isinstance(m, ExternModule) for m in kept)
        if not (has_header or has_module or has_extern):
            return None
        return Module(module.name, module.explicit, list(module.attrs), kept, module.framework)


def referenced_files(members: Sequence[object]) -> List[str]:
    files: List[str] = []
    for member in members:
        if isinstance(member, ExternModule):
            files.append(member.file)
        elif isinstance(member, Module):
            files.extend(referenced_files(member.members))
    return files


def filter_map(
    members: Sequence[object],
    include_dir: str,
    emitted_files: set,
    features: set,
    dropped: set,
) -> List[object]:
    flt = Filter(include_dir, emitted_files, features, dropped)
    kept: List[object] = []
    for member in members:
        if isinstance(member, Module):
            module = flt.filter_module(member)
            if module is not None:
                kept.append(module)
        elif isinstance(member, ExternModule):
            if member.file in emitted_files:
                kept.append(member)
        else:
            # A header, umbrella or export can only appear inside a module; at
            # the top level the parser accepts only modules and extern modules.
            raise ModuleMapError("top-level %r is not allowed" % (member,))
    return kept


# ---------------------------------------------------------------------------
# Rendering


def render(members: Sequence[object], indent: int = 0) -> str:
    lines: List[str] = []
    pad = "  " * indent
    for member in members:
        if isinstance(member, Header):
            prefix = ""
            if member.private:
                prefix += "private "
            if member.textual:
                prefix += "textual "
            lines.append('%s%sheader "%s"' % (pad, prefix, member.path))
        elif isinstance(member, Umbrella):
            if member.header is not None:
                lines.append('%sumbrella header "%s"' % (pad, member.header))
            else:
                lines.append('%sumbrella "%s"' % (pad, member.directory))
        elif isinstance(member, ExcludeHeader):
            lines.append('%sexclude header "%s"' % (pad, member.path))
        elif isinstance(member, Export):
            lines.append("%sexport %s" % (pad, member.spec))
        elif isinstance(member, Requires):
            lines.append("%srequires %s" % (pad, " ".join(member.clause)))
        elif isinstance(member, Link):
            quoted = " ".join(
                arg if arg == "framework" else '"%s"' % arg for arg in member.args
            )
            lines.append("%slink %s" % (pad, quoted))
        elif isinstance(member, ExternModule):
            lines.append('%sextern module %s "%s"' % (pad, member.name, member.file))
        elif isinstance(member, Module):
            if member.explicit:
                head = "explicit module"
            elif member.framework:
                head = "framework module"
            else:
                head = "module"
            if member.name == "*":
                head += " *"
            else:
                head += " " + member.name
            if member.attrs:
                head += " [" + " ".join(member.attrs) + "]"
            lines.append("%s%s {" % (pad, head))
            lines.append(render(member.members, indent + 1))
            lines.append("%s}" % pad)
        else:  # pragma: no cover
            raise AssertionError("unhandled member %r" % (member,))
    return "\n".join(l for l in lines if l != "")


# ---------------------------------------------------------------------------
# Driver


def top_level_module_names(members: Sequence[object]) -> List[str]:
    names: List[str] = []
    for member in members:
        if isinstance(member, Module) and member.name != "*":
            names.append(member.name)
    return names


def load_templates(template_dir: str) -> Dict[str, List[object]]:
    """Parse the *.modulemap templates at the top level of the directory.

    Only the top level: a nested module map (usr/include/simd/module.modulemap,
    for instance) resolves its header paths relative to its own directory, which
    is a different rule from the master map's, and Apple's master module map does
    not reference any of them. Darling therefore does not carry them.
    """
    templates: Dict[str, List[object]] = {}
    for name in sorted(os.listdir(template_dir)):
        if not name.endswith(".modulemap"):
            continue
        full = os.path.join(template_dir, name)
        if not os.path.isfile(full):
            continue
        with open(full, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        templates[name] = Parser(lex(text, full), full).parse()
    return templates


def run(
    template_dir: str,
    include_dir: str,
    output_dir: str,
    features: set,
    dropped: set,
    verbose: bool,
) -> int:
    if not os.path.isdir(template_dir):
        raise ModuleMapError("module map template directory not found: %s" % template_dir)
    if not os.path.isdir(include_dir):
        raise ModuleMapError("SDK header tree not found: %s" % include_dir)

    templates = load_templates(template_dir)
    if not templates:
        raise ModuleMapError("no *.modulemap templates found in %s" % template_dir)
    if "module.modulemap" not in templates:
        raise ModuleMapError(
            "template directory %s has no master module.modulemap" % template_dir
        )

    # Fixpoint: an extern module is kept only when the file it names will itself
    # be emitted with content, and dropping it can leave the modules around it
    # empty, which can make their file empty in turn.
    emitted = set(templates)
    filtered: Dict[str, List[object]] = {}
    for _ in range(len(templates) + 1):
        candidate = {
            name: filter_map(members, include_dir, emitted, features, dropped)
            for name, members in templates.items()
        }
        surviving = {name for name, members in candidate.items() if members}
        if surviving == emitted:
            filtered = candidate
            break
        emitted = surviving
    else:
        raise ModuleMapError("module map filtering did not converge")

    # Only the master map and the maps reachable from it through kept extern
    # declarations are loaded by Clang; emitting unreachable ones would add
    # files that the SDK never reads.
    reachable = {"module.modulemap"}
    queue = ["module.modulemap"]
    while queue:
        name = queue.pop()
        for referenced in referenced_files(filtered[name]):
            if referenced in filtered and referenced not in reachable:
                reachable.add(referenced)
                queue.append(referenced)

    written = 0
    for name in sorted(reachable):
        members = filtered[name]
        if not members:
            continue
        destination = os.path.join(output_dir, name)
        parent = os.path.dirname(destination)
        if parent:
            os.makedirs(parent, exist_ok=True)
        text = "// Generated by tools/generate-sdk-modulemap.py - do not edit.\n"
        text += render(members) + "\n"
        stale = None
        if os.path.exists(destination):
            with open(destination, "r", encoding="utf-8") as handle:
                if handle.read() == text:
                    stale = False
            if stale is None:
                stale = True
        if stale in (None, True):
            with open(destination, "w", encoding="utf-8") as handle:
                handle.write(text)
        written += 1
    if verbose:
        print(
            "sdk module maps: %d template(s), %d emitted, %d written to %s"
            % (len(templates), len(reachable), written, output_dir)
        )
    return written


def default_include_dir() -> Optional[str]:
    """The SDK include dir of this checkout, when run from inside it."""
    top = os.path.dirname(SCRIPT_DIR)
    candidate = os.path.join(
        top,
        "Developer", "Platforms", "MacOSX.platform", "Developer",
        "SDKs", "MacOSX.sdk", "usr", "include",
    )
    return candidate if os.path.isdir(candidate) else None


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--sdk-include",
        default=None,
        help="the SDK's usr/include to filter the maps against and to write them into",
    )
    parser.add_argument(
        "--template-dir",
        default=DEFAULT_TEMPLATE_DIR,
        help="directory holding the *.modulemap templates to filter",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="where to write the maps (default: the SDK include tree)",
    )
    parser.add_argument("--quiet", action="store_true", help="print nothing on success")
    parser.add_argument(
        "--drop-header",
        action="append",
        default=[],
        metavar="PATH",
        help="treat the SDK-relative header PATH as unusable as a module header "
        "(may be repeated); defaults to the curated list in this script",
    )
    parser.add_argument(
        "--feature",
        action="append",
        default=[],
        metavar="NAME",
        help="treat module feature NAME as defined by the caller's build, so modules "
        "requiring it are emitted (may be repeated)",
    )
    args = parser.parse_args(argv)

    include_dir = args.sdk_include or default_include_dir()
    if include_dir is None:
        parser.error("--sdk-include is required when run outside a Darling checkout")

    try:
        run(
            args.template_dir,
            os.path.abspath(include_dir),
            os.path.abspath(args.output_dir or include_dir),
            KNOWN_FEATURES | set(args.feature),
            NON_MODULAR_HEADERS | set(args.drop_header),
            not args.quiet,
        )
    except ModuleMapError as error:
        print("generate-sdk-modulemap: error: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
