import json
from pathlib import Path
import struct
import re

from ..files import resolve_file
from .. import IMAGE_ABI_VERSION, ImagePatchResult
from .unwind import prepare_guest_unwind


class ImageOverlay:
    def __init__(self, raw):
        self.raw = raw
        self.data = bytearray(raw)
        self.changes = []

    def replace_bytes(self, offset, before, after, name, evidence=None):
        end = offset + len(before)
        if offset < 0 or end > len(self.raw) or not before or len(before) != len(after) or self.raw[offset:end] != before:
            raise ValueError('Unexpected patch bytes: ' + name)
        for item in self.changes:
            start = int(item['file_offset'],16)
            if offset < start + len(bytes.fromhex(item['before'])) and start < end:
                raise ValueError('Overlapping patches: ' + name)
        self.data[offset:end] = after
        self.changes.append(dict(name=name,file_offset=hex(offset),before=before.hex(),after=after.hex(),evidence=evidence))


def find_signature(data, signature):
    pattern = re.escape(signature) if isinstance(signature,bytes) else b''.join(
        b'.' if token=='?' else re.escape(bytes([int(token,16)])) for token in signature.split())
    if not pattern:
        raise ValueError('Empty patch signature')
    matches = list(re.finditer(b'(?=('+pattern+b'))',data,re.S))
    if len(matches)!=1:
        raise ValueError(f'Patch signature must be unique; found {len(matches)}')
    return matches[0].start()


def encode_arm64_branch(source, target, condition=None):
    delta = target-source
    bits = 26 if condition is None else 19
    if source%4 or target%4 or not -(1 << (bits+1))<=delta<(1 << (bits+1)) or (condition is not None and not 0<=condition<=15):
        raise ValueError('Invalid ARM64 branch range or condition')
    immediate=(delta//4)&((1 << bits)-1)
    return (0x14000000|immediate) if condition is None else (0x54000000|(immediate<<5)|condition)


def binding_offset(raw, image, address, expected):
    for segment in image.segments:
        if segment.virtual_address <= address and address+len(expected) <= segment.virtual_address+segment.file_size:
            offset=segment.file_offset+address-segment.virtual_address
            if expected and raw[offset:offset+len(expected)]==expected:
                return address-image.imagebase
            break
    raise ValueError(f'Native binding bytes differ at {address:#x}')


def prepare_image(raw, work, patch=None):
    import lief
    original = work / 'guest.bin'
    original.write_bytes(raw)
    parsed = lief.MachO.parse(str(original))
    if parsed is None or len(parsed) != 1:
        raise ValueError('A single ARM64 Mach-O image is required')
    image = parsed.at(0)
    if int(image.header.cpu_type) != 0x0100000c or (image.has_encryption_info and image.encryption_info.crypt_id):
        raise ValueError('Unsupported or encrypted Mach-O image')
    base = image.imagebase
    text, data, links = [image.get_segment(name) for name in ('__TEXT', '__DATA', '__LINKEDIT')]
    if any(segment is None for segment in (text, data, links)):
        raise ValueError('Image ABI requires __TEXT, __DATA and __LINKEDIT')
    supported = {'__PAGEZERO', '__TEXT', '__DATA', '__LINKEDIT'}
    if any(s.name not in supported and s.virtual_size for s in image.segments):
        raise ValueError('Additional mapped segments are unsupported by this image ABI')
    if text.file_offset != 0 or text.virtual_address != base or data.virtual_address != base + text.file_size:
        raise ValueError('Unsupported text/data layout')
    if data.file_offset != data.virtual_address - base:
        raise ValueError('Writable segment must preserve file/image offsets')
    gap = links.virtual_address - data.virtual_address - data.file_size
    if gap < 0 or any(s.file_offset + s.file_size > len(raw) for s in (text, data, links)):
        raise ValueError('Invalid segment bounds')
    executable = original
    if patch:
        result = patch(raw, image, work)
        if not isinstance(result, ImagePatchResult):
            raise ValueError('Adapter must return ImagePatchResult')
        executable = resolve_file(work, result.image)
        for name in result.files:
            resolve_file(work,name)
        if executable.stat().st_size != len(raw):
            raise ValueError('Image overlay must preserve the original file layout')
    assembly = []
    for section, flags, segment in [('text', 'ax', text), ('data', 'aw', data), ('links', 'a', links)]:
        assembly += [f'.section .guest_{section},"{flags}",%progbits', '.balign 16384',
                     f'.global guest_{section}', f'.hidden guest_{section}', f'guest_{section}:',
                     f'.incbin "{executable.relative_to(work.resolve()).as_posix()}",{segment.file_offset},{segment.file_size}']
    assembly += ['.section .guest_zero,"aw",%nobits', f'.space {gap}']
    (work / 'layout.ld').write_text(f'''SECTIONS {{
 .guest_text ALIGN(16384) : {{ KEEP(*(.guest_text)) }}
 .guest_data ADDR(.guest_text) + {data.virtual_address-base} : {{ KEEP(*(.guest_data)) }}
 .guest_zero ADDR(.guest_data) + {data.file_size} (NOLOAD) : {{ KEEP(*(.guest_zero)) }}
 .guest_links ADDR(.guest_text) + {links.virtual_address-base} : {{ KEEP(*(.guest_links)) }}
}} INSERT AFTER .bss;
''', encoding='utf-8')
    imports = sorted({binding.symbol.name for binding in image.bindings if not binding.symbol.value})
    if len(imports) > 65536:
        raise ValueError('Import gate count exceeds the image ABI')
    indices = {name: index for index, name in enumerate(imports)}
    stubs = image.get_section('__stubs')
    indirect = list(image.dynamic_symbol_command.indirect_symbols)
    if not stubs or not stubs.reserved2 or stubs.size % stubs.reserved2:
        raise ValueError('Unsupported import stubs')
    callable_names = {sym.name for sym in indirect[stubs.reserved1:stubs.reserved1+stubs.size//stubs.reserved2]}
    bindings = {}
    rebases = {item.address - base: None for item in image.relocations}
    for item in image.bindings:
        offset = item.address - base
        if item.symbol.value:
            rebases[offset] = item.symbol.value + item.addend - base
            bindings.pop(offset, None)
        else:
            rebases.pop(offset, None)
            bindings[offset] = (indices[item.symbol.name], item.addend)
    for offset in [*rebases, *bindings]:
        if not data.virtual_address-base <= offset <= data.virtual_address-base + data.file_size-8:
            raise ValueError(f'Fixup outside writable segment at {offset:x}')
    fixups = bytearray()
    for offset, local in sorted(rebases.items()):
        pointer = struct.unpack_from('<Q', raw, offset)[0] - base if local is None else local
        fixups.extend(struct.pack('<IQ', offset, pointer))
    (work / 'rebases.bin').write_bytes(fixups)
    (work / 'binds.bin').write_bytes(b''.join(struct.pack('<IIq', off, idx, add) for off, (idx, add) in sorted(bindings.items())))
    for name in ['rebases', 'binds']:
        assembly += ['.section .rodata', '.balign 8', f'.global {name}', f'.hidden {name}',
                     f'{name}:', f'.incbin "{name}.bin"']
    assembly += prepare_guest_unwind(image, work)
    assembly += ['.text', '.balign 4', '.global guest_gates', '.hidden guest_gates', 'guest_gates:']
    for index in range(len(imports)):
        assembly += [f'mov w0, #{index}', 'b missing_call']
    assembly += ['.section .note.GNU-stack,"",%progbits']
    (work / 'image.S').write_text('\n'.join(assembly)+'\n', encoding='utf-8')
    header = [f'#define I2A_IMAGE_ABI {IMAGE_ABI_VERSION}', f'#define GUEST_IMAGE_BASE {base}UL',
              f'#define GUEST_DATA_OFFSET {data.virtual_address-base}UL',
              f'#define GUEST_LINKS_OFFSET {links.virtual_address-base}UL',
              f'#define REBASE_COUNT {len(rebases)}', f'#define BIND_COUNT {len(bindings)}',
              f'#define ENTRY {image.entrypoint-base}', f'#define IMAGE_END {links.virtual_address-base+links.file_size}']
    for key, name in [('INIT','__mod_init_func'), ('LOAD_CLASSES','__objc_nlclslist'),
                      ('LOAD_CATEGORIES','__objc_nlcatlist'), ('CLASSES','__objc_classlist'),
                      ('CATEGORIES','__objc_catlist')]:
        section = image.get_section(name)
        header += [f'#define {key}_OFFSET {section.virtual_address-base if section else 0}',
                   f'#define {key}_COUNT {section.size//8 if section else 0}']
    header += ['static const char *const guest_dependencies[] = {',
               *[json.dumps(library.name)+',' for library in image.libraries], 'NULL};']
    header += ['static const char *const symbols[] = {', *[json.dumps(name)+',' for name in imports], '};',
               'static const unsigned char functions[] = {', ','.join(str(int(name in callable_names)) for name in imports), '};']
    (work / 'image.h').write_text('\n'.join(header)+'\n', encoding='utf-8')
    (work / 'imports.json').write_text(json.dumps(imports, indent=2), encoding='utf-8')
    return image
