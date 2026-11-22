from dataclasses import dataclass

IMAGE_ABI_VERSION = 1
RUNTIME_INTERFACE_VERSION = 1
SHADER_PACK_VERSION = 1
ADAPTER_INTERFACE_VERSION = 2


def versions():
    return dict(image=IMAGE_ABI_VERSION, runtime=RUNTIME_INTERFACE_VERSION,
                shaders=SHADER_PACK_VERSION, adapter=ADAPTER_INTERFACE_VERSION)


@dataclass(frozen=True)
class BundleIdentity:
    prefix: str
    directory: str
    executable: str
    identifier: str
    version: str
    build: str
    display_name: str
    binary_sha256: str


@dataclass(frozen=True)
class ImagePatchResult:
    image: str
    files: tuple[str, ...] = ()
