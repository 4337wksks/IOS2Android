import re
import struct


def container(raw):
    if len(raw) < 88 or raw[:4] != b'MTLB' or struct.unpack_from('<Q', raw, 16)[0] != len(raw):
        raise ValueError('Unrecognized MTLB header/length')
    tables = struct.unpack_from('<8Q', raw, 24)
    table, table_size, public, public_size, private, private_size, code, code_size = tables
    if table + 4 + table_size > len(raw) or public + public_size > len(raw) or private + private_size > len(raw) or code + code_size > len(raw):
        raise ValueError('Section outside MTLB')
    count, = struct.unpack_from('<I', raw, table)
    if not 0 < count <= 4096:
        raise ValueError('Unexpected function count')
    cursor = table + 4
    result = []
    for index in range(count):
        size, = struct.unpack_from('<I', raw, cursor)
        end = cursor + size
        if size < 8 or end > table + 4 + table_size:
            raise ValueError('Function record outside section')
        position = cursor + 4
        tags = {}
        while position < end:
            tag = raw[position:position + 4].decode('ascii')
            position += 4
            if tag == 'ENDT':
                if position != end:
                    raise ValueError('Data after ENDT')
                break
            length, = struct.unpack_from('<H', raw, position)
            position += 2
            if position + length > end or tag in tags:
                raise ValueError('Invalid tag length/duplicate tag')
            tags[tag] = raw[position:position + length]
            position += length
        else:
            raise ValueError('Missing function terminator')
        name = tags['NAME'].removesuffix(b'\0').decode('utf-8')
        if not re.fullmatch(r'[A-Za-z0-9_.$-]+', name):
            raise ValueError(f'Unsupported function filename: {name!r}')
        stage_code = tags['TYPE'][0]
        offsets = struct.unpack('<3Q', tags['OFFT'])
        wrapper = code + offsets[2]
        if wrapper + 20 > code + code_size:
            raise ValueError('AIR wrapper outside section')
        magic, version, offset, length, cpu = struct.unpack_from('<5I', raw, wrapper)
        if magic != 0x0b17c0de or version != 0 or offset < 20 or wrapper + offset + length > code + code_size:
            raise ValueError('Unrecognized AIR wrapper')
        bitcode = raw[wrapper + offset:wrapper + offset + length]
        if bitcode[:4] != b'BC\xc0\xde':
            raise ValueError('Invalid LLVM bitcode magic')
        result.append({'name': name, 'type_tag': stage_code, 'stage': {0: 'vertex', 1: 'fragment', 2: 'kernel'}.get(stage_code, 'unknown'),
                       'record_offset': cursor, 'record_size': size, 'section_offsets': list(offsets), 'wrapper_offset': wrapper,
                       'bitcode_offset': wrapper + offset, 'bitcode_size': length,
                       'tags': {key: value.hex() for key, value in tags.items()}, 'bitcode': bitcode})
        cursor = end
    if cursor != table + table_size + 4:
        raise ValueError('Unparsed function table bytes')
    return result


def tokens(body):
    pieces = []
    begin = 0
    quoted = False
    escaped = False
    for index, ch in enumerate(body):
        if escaped:
            escaped = False
        elif ch == '\\' and quoted:
            escaped = True
        elif ch == '"':
            quoted = not quoted
        elif ch == ',' and not quoted:
            pieces.append(body[begin:index].strip())
            begin = index + 1
    pieces.append(body[begin:].strip())
    result = []
    for piece in pieces:
        if not piece:
            continue
        if piece.startswith('!"') and piece.endswith('"'):
            value = piece[2:-1]
            value = re.sub(r'\\([0-9A-Fa-f]{2})', lambda match: chr(int(match[1], 16)), value)
            result.append(value)
        elif re.fullmatch(r'!\d+', piece):
            result.append({'ref': int(piece[1:])})
        elif re.fullmatch(r'i\d+ -?\d+', piece):
            result.append(int(piece.split()[1]))
        else:
            result.append({'literal': piece})
    return result


def reflect(path, stage):
    text = path.read_text(encoding='utf-8')
    nodes = {int(match[1]): tokens(match[2]) for match in re.finditer(r'^!(\d+) = !\{(.*)\}$', text, re.M)}

    def node(value):
        return nodes[value['ref']]

    def argument(items):
        result = {'properties': {}, 'raw': items}
        if items and isinstance(items[0], int):
            result['argument'] = items[0]
        for index, item in enumerate(items):
            if not isinstance(item, str) or not item.startswith('air.'):
                continue
            following = items[index + 1] if index + 1 < len(items) else None
            if following is None or (isinstance(following, str) and following.startswith('air.')):
                following = True
            result['properties'][item] = following
        props = result['properties']
        result['name'] = props.get('air.arg_name')
        result['type'] = props.get('air.arg_type_name')
        result['index'] = props.get('air.location_index')
        roles = ['vertex_input', 'fragment_input', 'buffer', 'texture', 'sampler', 'position', 'render_target', 'point_size', 'vertex_id', 'instance_id', 'thread_position_in_grid', 'thread_position_in_threadgroup', 'threadgroup_position_in_grid', 'threads_per_threadgroup', 'depth']
        result['kind'] = next((role for role in roles if 'air.' + role in props), 'other')
        if 'air.struct_type_info' in props:
            fields = node(props['air.struct_type_info'])
            if len(fields) % 5:
                raise ValueError(f'Unsupported struct metadata: {path}: {fields}')
            result['members'] = []
            for offset in range(0, len(fields), 5):
                position, size, extra, kind, name = fields[offset:offset + 5]
                result['members'].append({'name': name, 'type': kind, 'offset': position, 'size': size, 'extra': extra})
            result['size'] = props.get('air.buffer_size', props.get('air.arg_type_size'))
        return result

    match = re.search(r'^!air\.' + stage + r' = !\{(.*)\}$', text, re.M)
    if not match:
        raise ValueError(f'No AIR {stage} entry: {path}')
    entries = []
    for ref in tokens(match[1]):
        entry = node(ref)
        function = entry[0]['literal'].split('@', 1)[1]
        outputs = [argument(node(ref)) for ref in node(entry[1])]
        inputs = [argument(node(ref)) for ref in node(entry[2])]
        pairs = []
        signature = re.search(r'^define .*?@' + re.escape(function) + r'\((.*)\).*?\{', text, re.M)
        if signature:
            parameters = []
            for param in signature[1].split(','):
                value = re.search(r'(%[A-Za-z0-9_.$]+)\s*$', param)
                parameters.append(value[1] if value else None)
            by_ssa = {parameters[arg['argument']]: arg for arg in inputs if 'argument' in arg and arg['argument'] < len(parameters)}
            for sample in re.finditer(r'@air\.sample_texture_[^(]+\(ptr[^,%]*(%[A-Za-z0-9_.$]+), ptr[^,%]*(%[A-Za-z0-9_.$]+),', text):
                texture, sampler = by_ssa.get(sample[1]), by_ssa.get(sample[2])
                if texture and sampler:
                    pair = {'texture': texture['name'], 'texture_index': texture['index'], 'sampler': sampler['name'], 'sampler_index': sampler['index']}
                    if pair not in pairs:
                        pairs.append(pair)
        entries.append({'entry': function, 'stage': stage, 'inputs': inputs, 'outputs': outputs, 'sampled_pairs': pairs})
    return entries


