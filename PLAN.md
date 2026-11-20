# PLAN

Status 2026-11-20: the C core, CLI and Textual TUI are done (`core/`, `cli/`,
`tui/`), and `dos/` has been removed. The next steps are listed in order.
Nothing below is implemented yet.

## 1. Clean up the training facts (remove the DOS persona)

TINY still believes it runs under DOS. It answers "C, built with Turbo C 2.0"
and suggests the DOS `TIME` and `DATE` commands.

- [ ] `training/facts/dos.txt`: about 20 persona facts and DOS how-tos (DIR,
      CD, MEM, COPY, Turbo C, ...). Decide for each: drop it, keep it as
      general knowledge ("what is dos"), or rewrite it for the new platform.
- [ ] `training/facts/talk.txt`: lines 13, 21 and 31 ("an old DOS laptop",
      "DOS blue", "ask DOS, it has a clock").
- [ ] `training/facts/tech.txt`: "what is dos" and "what is turbo c" are
      general knowledge, so probably keep them.
- [ ] New persona facts: what TINY runs on now (terminal / microcontroller),
      what it is written in, and its time/date answers.
- [ ] Grep the facts and `gen2.py` templates again for `DOS`, `Turbo`, `MEM`,
      `CLS` and `prompt`.
- Open question: should the persona be generic ("a small computer"), or
  depend on the build target? The second option means one model per target,
  or none at all.

## 2. Training and test pipeline with metrics

Today, `train2.py eval` scores the float/quantized PyTorch model on a
generated set (about 780 questions: 120 fact questions plus 60 for each
generator kind). It prints the share correct for each kind. Gaps:

- It doesn't test the real C core (int8 weights plus the int8 cache of
  earlier letters).
- It saves no reports and compares no runs.
- The only metric is correct / not correct.

Goal: one command that does data → train → export → evaluate on the C core
→ write a report.

- [ ] **Fixed, versioned test sets** in `tests/data/`, generated once and
      checked in, so scores can be compared across runs:
  - `heldout`: new wordings and new combinations, never trained on
    (today's `eval_set`, frozen).
  - `seen`: wordings taken from the training distribution (the README
    claims about 90%).
  - `unknown`: made-up names and nonsense, where the right answer is "I do
    not know".
  - `robustness`: typos, CAPS, "hey tiny ... please", extra spaces.
  - `regression`: hand-picked questions with exact expected answers
    (`tests/questions.txt` plus `tests/expected.txt` today).
  - Check that the train and test questions don't overlap (no leakage),
    and fail loudly if they do.
- [ ] **Run the evaluation on the C core** through `build/libtiny`. Python's
      ctypes binding works, or a batch mode in `build/tiny`. The scores then
      reflect exactly what ships.
- [ ] **Metrics:**
  - Accuracy overall and per category/kind: geo, animals, elements, maths,
    words, unknown, and so on.
  - Exact-match rate, plus the existing keyword-based match.
  - Hallucination rate: confident wrong answers on the `unknown` set.
  - Confidence calibration: per-letter and per-answer confidence against
    correctness (reliability bins, ECE). This also checks that the monitor's
    "confidence" means something.
  - Accuracy at creativity 0, 5 and 9 (averaged over seeds) to show the
    variety/accuracy trade-off.
  - Float vs int8 gap: accuracy of the PyTorch model against the C core.
  - Speed: letters/s on the host. Memory: weights size and RAM need.
- [ ] **Report:** `runs/<date>-<name>/` with `metrics.json`, `report.md`
      (tables, worst failures per category) and a diff against a baseline
      run, flagging categories that got worse.
- [ ] **Repeatable training:** fixed seeds and a recorded config (sizes,
      learning rate, minutes, data hash, git commit). Pin the PyTorch and
      NumPy versions in `training/requirements.txt`.
- [ ] **Make targets:** `make data`, `make train`, `make eval`, `make report`.
      `make test` stays fast (C build plus regression set).
- [ ] Optional: GitHub Actions that run `make test` and an eval of the
      checked-in model on every push.

## 3. Retrain

- [ ] Baseline: run the new eval on today's `model/TINY.BIN` and check it in
      as the reference (`runs/baseline-2.1`).
- [ ] Fine-tune from `tiny2.pt` on the cleaned facts, and compare with the
      baseline per category. There should be no loss outside the persona
      facts.
- [ ] If the embedded RAM/flash budget changes the model size (see 4), train
      at the target size, possibly with distillation from the 96-wide model.
- [ ] Update `model/TINY.BIN`, `tests/expected.txt`, and the README numbers
      (weights, accuracy).

## 4. Port to embedded platforms

The core is already C99 with no heap or I/O. Weights (about 380 KB) are
used in place from flash, and RAM is about 100 KB for the letter cache plus
6 KB for `tiny_ctx`.

- [ ] **Pick target boards.** Possible candidates: RP2040/RP2350 (264/520 KB
      RAM), ESP32-S3 (PSRAM), STM32H7 / Cortex-M7, Cortex-M4F. Check flash
      and RAM against the model size.
- [ ] **Weights as a C array:** a `tools/bin2c.py` script that writes a
      `const uint8_t tiny_bin[]` placed in flash. Optionally check the header
      at build time.
- [ ] **RAM reductions:**
  - Build with exact `TINY_MAX*` values for the model.
  - Optionally make `TINY_CTX` smaller (shorter answers).
  - The letter cache is already int8.
- [ ] **Speed:**
  - An int8 × float or int8 × int8 `matvec` with SIMD (CMSIS-NN / Helium,
    ESP32-S3 PIE).
  - Optional fixed-point build for chips without a floating-point unit,
    behind the same API.
  - Measure letters/s on each target.
- [ ] **Correctness on target:** run the regression set on the device, or in
      QEMU for Cortex-M. Different floating-point hardware can change rare
      close calls, so compare against expected answers, not bit-exact output.
- [ ] **I/O front end per target:** UART console like `cli/tiny_cli.c`;
      optionally a display or keyboard UI.
- [ ] **Example projects** (`ports/<board>/`) with build instructions in the
      README.

## Open questions

- Persona wording after the DOS clean-up (see 1).
- Embedded target boards and their RAM/flash budgets. This decides whether
  the model must shrink.
- Is the existing keyword-based "correct" check good enough, or do we want
  stricter exact-match targets for some categories (maths, spelling)?
