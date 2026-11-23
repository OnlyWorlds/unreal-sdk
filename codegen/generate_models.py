#!/usr/bin/env python3
"""The plugin's generated models: one Blueprint struct per element type, from schema-dist's schema.json.

    python codegen/generate_models.py            # verify the vendored dist against the pin, then write every output
    python codegen/generate_models.py --check    # verify, then exit 1 if any output would change (the drift guard)

Standard library only. It shares generate.py's pin check (the vendored tree is what codegen/PIN says, or nothing is
written). What it reads and writes:

  codegen/schema-dist/schema.json        the 22 types: base fields, then each type's own, with their kinds
  and, under Plugin/OnlyWorlds/Source/OnlyWorlds/:
  -> Public/OWModels.h                   USTRUCT(BlueprintType) FOW<Type> per type, and UOWElements, the Blueprint
                                         library that reads them out of a UOWWorld and gives one as the JSON text a
                                         write node takes (Get<Type>Json)
  -> Public/OWModelsJson.h               OWFromJson(body, struct) and OWToJson(struct) per type, for C++ callers
  -> Private/OWModels.cpp                all of them, on the core's OWJsonValue (helpers: OWModelRead.h, OWModelWrite.h)

The mapping (the first Unreal generator's, with the Unity generator's names), read and written back:

  scalar_str   FString                ("" when the key is missing or not a string); written as its text, "" the unset
                                      (string-empty-is-unset). The id is left out when it is "" (keel refuses an
                                      empty id; the client mints one for an element without)
  scalar_int   bool bHas<Field> and int64 <Field>  (false and 0 for null or a missing key; an integral number written
                                      N.0 reads as N). Not a TOptional<int64>: the editor gives one no pin on a Break or
                                      a Make node in 5.8. Written as its digits, or null when not
                                      bHas<Field> (nullable-by-default)
  scalar_bool  bool                   written as itself
  single       FString                (the linked element's id; "" for null); written null for ""
  multi        TArray<FString>        (the linked elements' ids); written as the array, [] for none
  generic      two FStrings, from the field's type_key and id_key ("element_type", "element_id"); written as keel
                                      serves an unset pair: the type "" and the id null

`world` is read and never written (required-world-wire-caveat); a struct holds no extension key and no server stamp,
so its JSON is the schema's fields alone: a PUT of it clears an element's extensions, a PATCH keeps them.

Member names are PascalCase as the Unity SDK's properties are (`birth_date` -> `BirthDate`, `STR` -> `Str`), so no
schema name meets a C++ keyword (`operator` -> `Operator`). The JSON key and the schema's description become the
member's tooltip.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from generate import DIST, ROOT, read_pin, verify_dist

SCHEMA = DIST / "schema.json"
MODULE = ROOT / "Plugin" / "OnlyWorlds" / "Source" / "OnlyWorlds"
OUT_H = MODULE / "Public" / "OWModels.h"
OUT_JSON_H = MODULE / "Public" / "OWModelsJson.h"
OUT_CPP = MODULE / "Private" / "OWModels.cpp"

# Plurals the +s rule gets wrong, for the GetAll<Types> accessors.
PLURALS = {"ability": "Abilities", "family": "Families", "species": "Species", "phenomenon": "Phenomena"}


def pascal(name: str) -> str:
    """`birth_date` -> `BirthDate`; `STR` -> `Str` (the Unity generator's rule)."""
    if name.isupper():
        return name[0] + name[1:].lower()
    return "".join(part[:1].upper() + part[1:] for part in name.split("_"))


def struct_name(slug: str) -> str:
    return "FOW" + pascal(slug)


def plural(slug: str) -> str:
    return PLURALS.get(slug, pascal(slug) + "s")


def doc(text: str) -> str:
    return " ".join(text.replace("*/", "* /").split())


def members(field: dict) -> list[tuple[str, str, str, str]]:
    """(C++ type, member name, JSON key, how it is read) for one schema field."""
    kind, name = field["kind"], field["name"]
    if kind == "scalar_str":
        return [("FString", pascal(name), name, "Str")]
    if kind == "scalar_int":
        return [("bool", "bHas" + pascal(name), name, "HasInt"), ("int64", pascal(name), name, "IntValue")]
    if kind == "scalar_bool":
        return [("bool", "b" + pascal(name), name, "Bool")]
    if kind == "single":
        return [("FString", pascal(name), name, "Str")]
    if kind == "multi":
        return [("TArray<FString>", pascal(name), name, "Ids")]
    if kind == "generic":
        return [("FString", pascal(field["type_key"]), field["type_key"], "Str"),
                ("FString", pascal(field["id_key"]), field["id_key"], "Str")]
    raise SystemExit(f"unknown field kind {kind!r} on {name!r}: teach generate_models.py the kind first")


def writes(field: dict) -> list[str]:
    """The lines of OWToJson that write one schema field into Json from the struct In (none for `world`)."""
    kind, name = field["kind"], field["name"]
    if name == "world":
        return []
    if name == "id":
        return ['\tif (!In.Id.IsEmpty()) Json.Set("id", OWModelWrite::Str(In.Id));']
    if kind == "scalar_str":
        return [f'\tJson.Set("{name}", OWModelWrite::Str(In.{pascal(name)}));']
    if kind == "scalar_int":
        return [f'\tJson.Set("{name}", OWModelWrite::Int(In.bHas{pascal(name)}, In.{pascal(name)}));']
    if kind == "scalar_bool":
        return [f'\tJson.Set("{name}", OWModelWrite::Bool(In.b{pascal(name)}));']
    if kind == "single":
        return [f'\tJson.Set("{name}", OWModelWrite::Link(In.{pascal(name)}));']
    if kind == "multi":
        return [f'\tJson.Set("{name}", OWModelWrite::Ids(In.{pascal(name)}));']
    if kind == "generic":
        t, i = field["type_key"], field["id_key"]
        return [f'\tJson.Set("{t}", OWModelWrite::Str(In.{pascal(t)}));',
                f'\tJson.Set("{i}", OWModelWrite::Link(In.{pascal(i)}));']
    raise SystemExit(f"unknown field kind {kind!r} on {name!r}: teach generate_models.py the kind first")


# The names the generated OWToJson and Get<Type>Json use besides the struct's members: no member may take one.
WRITE_NAMES = {"In", "Json"}


def header(tag: str) -> str:
    return (f"// Generated by codegen/generate_models.py from schema-dist {tag} (schema.json). Regenerate, never edit.")


def emit_h(schema: dict, tag: str) -> str:
    out = ["#pragma once", header(tag),
           "// One struct per element type, and UOWElements, the Blueprint library that reads them out of a world and gives",
           "// one back as the JSON text a write node takes.", "",
           '#include "CoreMinimal.h"', '#include "Kismet/BlueprintFunctionLibrary.h"', "",
           '#include "OWModels.generated.h"', "", "class UOWWorld;", ""]
    for slug, fields in schema["types"].items():
        s = struct_name(slug)
        out += [f"/** {pascal(slug)}: an element of type `{slug}`. */", "USTRUCT(BlueprintType)",
                f"struct ONLYWORLDS_API {s}", "{", "\tGENERATED_BODY()", ""]
        seen: set[str] = set()
        for f in schema["base"] + fields:
            for ctype, member, key, read in members(f):
                if member in seen:
                    raise SystemExit(f"{s}: two members named {member} (the field {key!r}): rename one before generating")
                if member in WRITE_NAMES:
                    raise SystemExit(f"{s}: a member named {member} (the field {key!r}) meets a name OWToJson uses: "
                                     "rename the generator's name before generating")
                seen.add(member)
                if read == "HasInt":
                    text = f"{key}: whether it is set (false for null or a missing key)"
                else:
                    text = f"{key}: {doc(f.get('desc', ''))}" + (f" (0 when not bHas{member})" if read == "IntValue" else "")
                default = {"bool": " = false", "int64": " = 0"}.get(ctype, "")
                out += [f"\t/** {text} */",
                        f'\tUPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OnlyWorlds|{pascal(slug)}")',
                        f"\t{ctype} {member}{default};", ""]
        out[-1:] = ["};", ""]
    out += ["/** The elements of a world as structs, by type, and each struct as the JSON text a write node takes. */",
            "UCLASS()",
            "class ONLYWORLDS_API UOWElements : public UBlueprintFunctionLibrary", "{", "\tGENERATED_BODY()", "",
            "public:"]
    for slug in schema["types"]:
        s, p, t = struct_name(slug), pascal(slug), plural(slug)
        out += [f"\t/** Every {p} of the world, in the order read. */",
                f'\tUFUNCTION(BlueprintPure, Category = "OnlyWorlds|{p}")',
                f"\tstatic TArray<{s}> GetAll{t}(const UOWWorld* World);", "",
                f"\t/** The {p} with this id; false when the world holds no {p} with it. */",
                f'\tUFUNCTION(BlueprintPure, Category = "OnlyWorlds|{p}")',
                f"\tstatic bool Find{p}(const UOWWorld* World, const FString& Id, {s}& {p});", "",
                f"\t/** The {p} as the JSON text a write node takes (CreateElement, UpsertElement, PatchElement, a "
                "BulkWrite item): every schema field, an unset one as keel serves it (\"\", null, []), never world, "
                "the id left out when \"\". A struct holds no extension key (x_*): UpsertElement of this text alone "
                "writes the element whole and clears the extensions keel holds for it. Merge it into the element as "
                "read first (UOWElementJson::MergeElementJson) and upsert that to keep them; PatchElement keeps them "
                "too (and replaces every field it sends, link lists whole). */",
                f'\tUFUNCTION(BlueprintPure, Category = "OnlyWorlds|{p}")',
                f"\tstatic FString Get{p}Json(const {s}& {p});", ""]
    out[-1:] = ["};", ""]
    return "\n".join(out)


def emit_json_h(schema: dict, tag: str) -> str:
    out = ["#pragma once", header(tag), "// An element's body (the core's OWJsonValue) into its struct, and a struct back into a body,",
           "// for C++ callers. OWToJson writes every schema field (an unset one as keel serves it: \"\", null, []), never",
           "// world, the id left out when it is \"\"; a struct holds no extension key, so a PUT of its body clears them.",
           "", '#include "OWJson.h"', '#include "OWModels.h"', ""]
    for slug in schema["types"]:
        out.append(f"ONLYWORLDS_API void OWFromJson(const OnlyWorlds::OWJsonValue& Body, {struct_name(slug)}& Out);")
    out.append("")
    for slug in schema["types"]:
        out.append(f"ONLYWORLDS_API OnlyWorlds::OWJsonValue OWToJson(const {struct_name(slug)}& In);")
    return "\n".join(out + [""])


def emit_cpp(schema: dict, tag: str) -> str:
    out = [header(tag), "", '#include "OWModels.h"', '#include "OWModelsJson.h"', "", '#include "OWModelRead.h"',
           '#include "OWModelWrite.h"', '#include "OWWorldObject.h"', ""]
    for slug, fields in schema["types"].items():
        s = struct_name(slug)
        out += [f"void OWFromJson(const OnlyWorlds::OWJsonValue& Body, {s}& Out)", "{"]
        for f in schema["base"] + fields:
            for _, member, key, read in members(f):
                out.append(f'\tOut.{member} = OWModelRead::{read}(Body, "{key}");')
        out += ["}", ""]
    for slug, fields in schema["types"].items():
        out += [f"OnlyWorlds::OWJsonValue OWToJson(const {struct_name(slug)}& In)", "{",
                "\tOnlyWorlds::OWJsonValue Json = OnlyWorlds::OWJsonValue::Object();"]
        for f in schema["base"] + fields:
            out += writes(f)
        out += ["\treturn Json;", "}", ""]
    for slug in schema["types"]:
        s, p, t = struct_name(slug), pascal(slug), plural(slug)
        out += [f"TArray<{s}> UOWElements::GetAll{t}(const UOWWorld* World)", "{", f"\tTArray<{s}> Out;",
                f'\tOWModelRead::Each(World, "{slug}", [&Out](const OnlyWorlds::OWJsonValue& Body) '
                "{ OWFromJson(Body, Out.AddDefaulted_GetRef()); });",
                "\treturn Out;", "}", "",
                f"bool UOWElements::Find{p}(const UOWWorld* World, const FString& Id, {s}& {p})", "{",
                f'\tconst OnlyWorlds::OWJsonValue* Body = OWModelRead::Find(World, Id, "{slug}");',
                "\tif (!Body) return false;", f"\t{p} = {s}();", f"\tOWFromJson(*Body, {p});", "\treturn true;", "}", "",
                f"FString UOWElements::Get{p}Json(const {s}& {p})", "{",
                f"\treturn OWModelWrite::Text(OWToJson({p}));", "}", ""]
    return "\n".join(out)


def outputs() -> dict[Path, str]:
    pin = read_pin()
    problems = verify_dist(pin)
    if problems:
        for p in problems:
            print("FAIL:", p)
        sys.exit("The vendored dist is not what the pin says. Re-vendor from the pinned tag; never edit a hash.")
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    tag = pin["tag"]
    return {OUT_H: emit_h(schema, tag), OUT_JSON_H: emit_json_h(schema, tag), OUT_CPP: emit_cpp(schema, tag)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if any output would change")
    a = ap.parse_args()
    pin = read_pin()
    drift = 0
    files = outputs()
    print(f"dist OK: {pin['tag']}, MANIFEST.json sha256 {pin['manifest_sha256'][:12]}... matches the pin")
    for path, text in files.items():
        data = text.encode("utf-8")
        if a.check:
            if not path.is_file() or path.read_bytes() != data:
                print("DRIFT:", path.relative_to(ROOT).as_posix())
                drift += 1
        elif not path.is_file() or path.read_bytes() != data:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print("wrote", path.relative_to(ROOT).as_posix())
    if a.check:
        print("no drift" if not drift else f"{drift} file(s) drifted: run codegen/generate_models.py")
    return 1 if drift else 0


if __name__ == "__main__":
    sys.exit(main())
