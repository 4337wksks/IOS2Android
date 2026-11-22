import argparse
import json
import subprocess
from dataclasses import asdict, replace
from pathlib import Path

from .files import write_json
from .build import build, create_context, outside
from .bundle.identity import inspect_bundle


def main():
    parser = argparse.ArgumentParser(prog="porter")
    commands = parser.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("inspect")
    inspect.add_argument("ipa", type=Path)
    for command in ("build", "check-core"):
        build_parser = commands.add_parser(command)
        build_parser.add_argument("ipa", type=Path)
        build_parser.add_argument("--workspace", required=True, type=Path)
        build_parser.add_argument("--config", required=True, type=Path)
        build_parser.add_argument("--adapter", type=Path)
        build_parser.add_argument("--variant", choices=("dev", "release", "all"), default="all")
        build_parser.add_argument("--disable-policy", action="append", default=[])
        build_parser.add_argument("--enable-policy", action="append", default=[])
        build_parser.add_argument("--no-policies", action="store_true")
        build_parser.add_argument("--shader-diagnostics", action="store_true")
    configure_shaders(commands.add_parser("shader", help="Inspect and translate shader artifacts"))
    args = parser.parse_args()
    if args.command == "shader":
        run_shader(args)
        return
    if args.command == "inspect":
        identity, _ = inspect_bundle(args.ipa)
        print(json.dumps(asdict(identity), ensure_ascii=False, indent=2))
        return
    for variant in ("dev", "release") if args.variant == "all" else (args.variant,):
        if args.shader_diagnostics and (variant != "dev" or args.command != "build"):
            parser.error("--shader-diagnostics requires build --variant dev")
        context = create_context(
            args.ipa,
            args.workspace,
            args.config,
            variant,
            args.adapter,
            args.disable_policy,
            args.enable_policy,
            not args.no_policies,
        )
        build(replace(context, shader_diagnostics=args.shader_diagnostics), core_only=args.command == "check-core")


def configure_shaders(parser):
    commands = parser.add_subparsers(dest="shader_command", required=True)
    container = commands.add_parser("extract-air")
    container.add_argument("metallib", type=Path)
    container.add_argument("--output", required=True, type=Path)
    container.add_argument("--llvm-dis", type=Path)
    reflect = commands.add_parser("reflect-air")
    reflect.add_argument("ir", type=Path)
    reflect.add_argument("--stage", required=True, choices=("vertex", "fragment", "kernel"))
    reflect.add_argument("--output", required=True, type=Path)
    for command in ("translate-air", "translate-glsl"):
        translate = commands.add_parser(command)
        translate.add_argument("source", type=Path)
        translate.add_argument("--metadata", required=True, type=Path)
        translate.add_argument("--locations", required=True, type=Path)
        translate.add_argument("--output", required=True, type=Path)
        translate.add_argument("--tools", required=True, type=Path)
        if command == "translate-air":
            translate.add_argument("--round-fragment-inputs", action="store_true")


def run_shader(args):
    from .shaders.build import compile_glsl

    output = outside(args.output, "Shader output")
    if args.shader_command == "extract-air":
        from .shaders.metal import container as parse

        output.mkdir(parents=True, exist_ok=True)
        entries = parse(args.metallib.read_bytes())
        for entry in entries:
            binary = output / (entry["name"] + ".bc")
            binary.write_bytes(entry.pop("bitcode"))
            entry["bitcode"] = binary.name
            if args.llvm_dis:
                ir = output / (entry["name"] + ".ll")
                subprocess.run([str(args.llvm_dis), str(binary), "-o", str(ir)], check=True)
                entry.update(llvm_ir=ir.name)
        manifest = output / "manifest.json"
        write_json(manifest, dict(input=str(args.metallib), entries=entries))
        print(manifest)
        return
    if args.shader_command == "reflect-air":
        from .shaders.metal import reflect

        entries = reflect(args.ir, args.stage)
        for entry in entries:
            entry.update(llvm_ir=str(args.ir.resolve()))
        write_json(output, dict(entries=entries))
        print(output)
        return
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    locations = json.loads(args.locations.read_text(encoding="utf-8"))
    if args.shader_command == "translate-air":
        from .shaders.air import Translator

        entry = metadata["entries"][0] if len(metadata["entries"]) == 1 else None
        if entry is None:
            raise ValueError("Select exactly one AIR entry in metadata")
        translator = Translator(
            args.source.read_text(encoding="utf-8"), entry, locations, args.round_fragment_inputs
        )
        code, stage = translator.translate(), translator.stage
    else:
        from .shaders.glsl import derive_native

        code, _ = derive_native(
            args.source.name, args.source.read_text(encoding="utf-8"), locations, metadata
        )
        stage = args.source.suffix.lstrip(".")
    folder, binary, reflection = compile_glsl(code, stage, output, args.tools)
    print(binary)
    print(reflection)


if __name__ == "__main__":
    main()
