from contextlib import contextmanager
import json
import os
from pathlib import Path
import tempfile

def file_metadata(paths):
    return [[str(Path(path).resolve()), Path(path).stat().st_size, Path(path).stat().st_mtime_ns]
            for path in paths]


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8', newline='\n') as stream:
            json.dump(value, stream, ensure_ascii=True, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def cache_matches(stamp, inputs, outputs):
    try:
        return json.loads(Path(stamp).read_text(encoding='utf-8')) == inputs and all(Path(p).is_file() for p in outputs)
    except (OSError, ValueError):
        return False


def resolve_file(root, name):
    root = Path(root).resolve()
    path = (root / name).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f'Expected a file inside {root}: {name}')
    return path


def require_tokens(data, present=(), absent=()):
    for token in present:
        if token.encode() not in data:
            raise ValueError('Missing build capability: ' + token)
    for token in absent:
        if token.encode() in data:
            raise ValueError('Excluded build capability is present: ' + token)


@contextmanager
def build_lock(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / 'build.lock').open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            stream.seek(0)
            if os.fstat(stream.fileno()).st_size == 0:
                stream.write(b'0')
                stream.flush()
            stream.seek(0)
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise RuntimeError(f'Build directory is busy: {folder}') from error
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)
