"""ctypes binding for the TINY C core (build/libtiny.dylib or .so)."""

from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAXQ = 72


class TinyError(RuntimeError):
    pass


def _find_lib() -> Path:
    name = "libtiny.dylib" if sys.platform == "darwin" else "libtiny.so"
    env = os.environ.get("TINY_LIB")
    path = Path(env) if env else ROOT / "build" / name
    if not path.exists():
        raise TinyError(f"{path} not found - run 'make' first")
    return path


def _load_lib() -> ctypes.CDLL:
    lib = ctypes.CDLL(str(_find_lib()))
    vp, sz, i, f = ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_float
    sigs = {
        "tiny_model_sizeof": ([], sz),
        "tiny_ctx_sizeof": ([], sz),
        "tiny_model_init": ([vp, vp, sz], i),
        "tiny_model_params": ([vp], i),
        "tiny_model_shape": ([vp, i], i),
        "tiny_ctx_mem_size": ([vp], sz),
        "tiny_ctx_init": ([vp, vp, vp, sz], i),
        "tiny_set_creativity": ([vp, i], None),
        "tiny_seed": ([vp, ctypes.c_uint32], None),
        "tiny_reset": ([vp], None),
        "tiny_feed": ([vp, i], i),
        "tiny_end_question": ([vp], i),
        "tiny_next": ([vp], i),
        "tiny_clean": ([ctypes.c_char_p, ctypes.c_char_p], i),
        "tiny_confidence": ([vp], f),
        "tiny_layer_activity": ([vp, i], f),
        "tiny_position": ([vp], i),
        "tiny_strerror": ([i], ctypes.c_char_p),
    }
    for name, (args, res) in sigs.items():
        fn = getattr(lib, name)
        fn.argtypes = args
        fn.restype = res
    return lib


class Tiny:
    """One network instance: model image + context with its letter memory."""

    def __init__(self, model_path: str | Path, creativity: int = 5, seed: int | None = None):
        self._lib = lib = _load_lib()
        data = Path(model_path).read_bytes()
        self._image = ctypes.create_string_buffer(data, len(data))
        self._model = ctypes.create_string_buffer(lib.tiny_model_sizeof())
        self._check(lib.tiny_model_init(self._model, self._image, len(data)))
        memlen = lib.tiny_ctx_mem_size(self._model)
        self._mem = ctypes.create_string_buffer(memlen)
        self._ctx = ctypes.create_string_buffer(lib.tiny_ctx_sizeof())
        self._check(lib.tiny_ctx_init(self._ctx, self._model, self._mem, memlen))
        self.creativity = creativity
        lib.tiny_seed(self._ctx, (seed if seed is not None else int.from_bytes(os.urandom(4), "little")) & 0xFFFFFFFF)
        self.width, self.layers, self.heads, self.hidden = (
            lib.tiny_model_shape(self._model, k) for k in range(4))
        self.params = lib.tiny_model_params(self._model)

    def _check(self, err: int) -> int:
        if err < 0:
            raise TinyError(self._lib.tiny_strerror(err).decode())
        return err

    @property
    def creativity(self) -> int:
        return self._creativity

    @creativity.setter
    def creativity(self, level: int) -> None:
        self._creativity = max(0, min(9, level))
        self._lib.tiny_set_creativity(self._ctx, self._creativity)

    # ---- asking, step by step ----
    def clean(self, line: str) -> str:
        out = ctypes.create_string_buffer(MAXQ + 1)
        raw = line.encode("ascii", "replace")
        self._lib.tiny_clean(raw, out)
        return out.value.decode("ascii")

    def reset(self) -> None:
        self._lib.tiny_reset(self._ctx)

    def feed(self, ch: str) -> None:
        self._check(self._lib.tiny_feed(self._ctx, ord(ch)))

    def end_question(self) -> None:
        self._check(self._lib.tiny_end_question(self._ctx))

    def next(self) -> str:
        """Next answer letter, '' at the end."""
        c = self._check(self._lib.tiny_next(self._ctx))
        return chr(c) if c else ""

    # ---- monitor ----
    @property
    def confidence(self) -> float:
        return self._lib.tiny_confidence(self._ctx)

    def layer_activity(self) -> list[float]:
        return [self._lib.tiny_layer_activity(self._ctx, i) for i in range(self.layers)]

    # ---- convenience ----
    def answer(self, question: str) -> str:
        q = self.clean(question)
        self.reset()
        for ch in q:
            self.feed(ch)
        self.end_question()
        out = []
        while ch := self.next():
            out.append(ch)
        return "".join(out)
