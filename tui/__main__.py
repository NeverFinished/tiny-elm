"""python -m tui [0-9] [--model TINY.BIN] [--demo] [--fast] [--seed N]"""

import argparse
import sys

from .tiny import ROOT, Tiny, TinyError


def main() -> int:
    p = argparse.ArgumentParser(prog="python -m tui", description="TINY 2 terminal interface")
    p.add_argument("creativity", nargs="?", type=int, default=5, choices=range(10),
                   help="0 = always the surest answer, 5 = default, 9 = most varied")
    p.add_argument("--model", default=str(ROOT / "model" / "TINY.BIN"), help="network file")
    p.add_argument("--demo", action="store_true", help="TINY asks itself sample questions")
    p.add_argument("--fast", action="store_true", help="no letter-by-letter animation")
    p.add_argument("--seed", type=int, help="random seed (repeatable answers)")
    a = p.parse_args()
    try:
        tiny = Tiny(a.model, a.creativity, a.seed)
    except (TinyError, OSError) as e:
        print(f"TINY: {e}", file=sys.stderr)
        return 1
    from .app import TinyApp
    TinyApp(tiny, demo=a.demo, pace=0.0 if a.fast else 0.03).run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
