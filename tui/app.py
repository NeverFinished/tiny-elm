"""TINY 2 - Textual terminal interface for the TINY C core."""

from __future__ import annotations

import random
import time
from datetime import datetime

from rich.text import Text
from textual import events, work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.widgets import Input, Static

from .tiny import MAXQ, Tiny

# neon palette
CYAN = "#33ffff"
CYAN2 = "#0f8c9c"
PINK = "#ff55dd"
AMBER = "#ffd23f"
MINT = "#4dffa6"
RED = "#ff4f4f"
DIM = "#4a5880"
TEXT = "#b8c2dc"
GLOW = "bold #ffffff"

HELP_LINES = [
    "Ask a question and press Enter.",
    "Try: capital of peru · is a whale a fish",
    "     what is 7 times 8 · spell elephant",
    "     which countries border estonia",
    "     how do you say thank you in spanish",
    "F4 creativity · ↑ last question",
    "Esc quit · CLS clears · HELP shows this",
]

DEMO_QUESTIONS = [
    "what is the capital of estonia", "is a whale a fish", "what is 7 times 8",
    "which countries border estonia", "spell computer", "what is the symbol for gold",
    "how do you say thank you in spanish", "what is a group of lions called",
    "what comes after 199", "what time is it", "tell me a joke", "name the continents",
]


def conf_color(v: float) -> str:
    return MINT if v > 0.8 else AMBER if v > 0.5 else RED


# ---------------------------------------------------------------- widgets

class TitleBar(Static):
    def __init__(self, params: int) -> None:
        super().__init__()
        self.params = params

    def on_mount(self) -> None:
        self.set_interval(1, self.refresh)

    def render(self) -> Text:
        t = Text()
        t.append(" ◄► ", CYAN)
        t.append("T I N Y", "bold #ffffff")
        t.append(f"  ·  NEURAL INTERFACE 2.1  ·  {self.params:,} WEIGHTS", CYAN)
        right = Text()
        right.append("■ ", MINT)
        right.append("ONLINE   ", CYAN)
        right.append(datetime.now().strftime("%H:%M "), "bold #ffffff")
        pad = max(1, self.size.width - t.cell_len - right.cell_len)
        return t + Text(" " * pad) + right


class Turn(Horizontal):
    """One line of the conversation: a prefix and a wrapping body."""

    def __init__(self, prefix: Text | str, body: Text | str = "") -> None:
        super().__init__(classes="turn")
        self._prefix = prefix
        self._body = body

    def compose(self) -> ComposeResult:
        yield Static(self._prefix, classes="prefix")
        yield Static(self._body, classes="body")

    def set_body(self, body: Text) -> None:
        self.query_one(".body", Static).update(body)


class Monitor(Static):
    """The NEURAL MONITOR panel."""

    def __init__(self, tiny: Tiny) -> None:
        super().__init__()
        self.tiny = tiny
        self.act = [0.0] * tiny.layers
        self.act_max = [0.0] * tiny.layers
        self.conf = 0.0
        self.heat: list[float] = []
        self.speed = 0.0

    def bar(self, t: Text, width: int, v: float, color: str) -> None:
        n = round(max(0.0, min(1.0, v)) * width)
        t.append("█" * n, color)
        t.append("·" * (width - n), DIM)

    def update_live(self, act: list[float] | None = None, conf: float | None = None,
                    letter: bool = False) -> None:
        if act is not None:
            self.act = act
        if conf is not None:
            self.conf = conf
        if letter:
            self.heat = (self.heat + [self.conf])[-18:]
        self.refresh()

    def render(self) -> Text:
        w = max(10, self.size.width)
        t = Text()
        t.append("LAYER ACTIVITY\n", DIM)
        for i, a in enumerate(self.act[:8]):
            self.act_max[i] = max(self.act_max[i], a)
            v = a / self.act_max[i] if self.act_max[i] > 0 else 0.0
            self.act_max[i] *= 0.995
            t.append(f"L{i + 1} ", CYAN)
            self.bar(t, w - 3, v, CYAN if i % 2 else MINT)
            t.append("\n")
        t.append("\nCONFIDENCE\n", DIM)
        self.bar(t, w - 6, self.conf, conf_color(self.conf))
        t.append(f" {round(self.conf * 100):3d}%\n", "bold #ffffff")
        t.append("LAST LETTERS\n", DIM)
        for i in range(min(18, w)):
            if i >= len(self.heat):
                t.append("·", DIM)
                continue
            v = self.heat[i]
            t.append("█" if v > 0.9 else "▓" if v > 0.7 else "▒" if v > 0.45 else "░", conf_color(v))
        t.append("\n\nSPEED\n", DIM)
        t.append(f"{self.speed:,.0f} letters/s\n" if self.speed else "-\n", TEXT)
        t.append("NETWORK\n", DIM)
        t.append(f"{self.tiny.layers} layers · {self.tiny.width} wide\n", TEXT)
        t.append("\nMODE\n", DIM)
        t.append(f"creativity {self.tiny.creativity}", TEXT)
        return t


class StatusBar(Static):
    def __init__(self) -> None:
        super().__init__()
        self.creativity = 0
        self.info = ""

    def render(self) -> Text:
        t = Text()
        for key, label, value in (("F1", " Help", None), ("F4", " Creativity ", str(self.creativity)),
                                  ("Esc", " Quit", None)):
            t.append(" ")
            t.append(key, "bold #ffffff")
            t.append(label, "#d8f6ff")
            if value is not None:
                t.append(value, f"bold {AMBER}")
            t.append(" ")
        pad = max(1, self.size.width - t.cell_len - len(self.info) - 1)
        return t + Text(" " * pad) + Text(self.info + " ", "#d8f6ff")


class AskInput(Input):
    BINDINGS = [Binding("up", "last_question", "last question", show=False)]

    def action_last_question(self) -> None:
        self.app.action_last_question()


# ---------------------------------------------------------------- the app

class TinyApp(App):
    TITLE = "TINY 2"
    CSS = f"""
    Screen {{ background: #060a24; color: {TEXT}; }}
    TitleBar {{ height: 1; background: #0b1a6b; }}
    #main {{ height: 1fr; }}
    #chat {{
        width: 1fr; border: double {CYAN}; padding: 0 1;
        border-title-color: #ffffff; border-title-background: {CYAN2}; border-title-style: bold;
        scrollbar-color: {CYAN2}; scrollbar-background: #060a24; scrollbar-size-vertical: 1;
    }}
    #monitor-box {{
        width: 28; border: double {CYAN2}; padding: 0 1;
        border-title-color: #ffffff; border-title-background: {CYAN2}; border-title-style: bold;
    }}
    .turn {{ height: auto; }}
    .turn .prefix {{ width: 7; }}
    .turn .body {{ width: 1fr; }}
    .gap {{ height: 1; }}
    #ask {{
        height: 3; border: double {CYAN2};
        border-title-color: #ffffff; border-title-background: {CYAN2}; border-title-style: bold;
    }}
    #ask-prompt {{ width: 2; color: {PINK}; padding: 0 0 0 1; }}
    AskInput {{ width: 1fr; border: none; height: 1; padding: 0; background: #060a24; color: #ffffff; }}
    AskInput:focus {{ border: none; background-tint: #000000 0%; }}
    #ask-hint {{ width: auto; color: {DIM}; padding: 0 1; }}
    StatusBar {{ height: 1; background: #0f6f7f; }}
    """

    BINDINGS = [
        Binding("f1", "help", "help", priority=True),
        Binding("f4", "creativity", "creativity", priority=True),
        Binding("escape", "quit", "quit", priority=True),
    ]

    def __init__(self, tiny: Tiny, demo: bool = False, pace: float = 0.03) -> None:
        super().__init__()
        self.tiny = tiny
        self.demo = demo
        self.pace = pace
        self.busy = False
        self.last_q = ""

    # ---- layout ----
    def compose(self) -> ComposeResult:
        yield TitleBar(self.tiny.params)
        with Horizontal(id="main"):
            yield VerticalScroll(id="chat")
            with Vertical(id="monitor-box"):
                yield Monitor(self.tiny)
        with Horizontal(id="ask"):
            yield Static("►", id="ask-prompt")
            yield AskInput(max_length=MAXQ, id="input")
            yield Static("↑ last  F1 help", id="ask-hint")
        yield StatusBar()

    def on_mount(self) -> None:
        self.query_one("#chat").border_title = " CONVERSATION "
        self.query_one("#monitor-box").border_title = " NEURAL MONITOR "
        self.query_one("#ask").border_title = " ASK TINY "
        self.status.creativity = self.tiny.creativity
        self.greet()
        self.query_one(AskInput).focus()
        if self.demo:
            self.run_demo()

    @property
    def chat(self) -> VerticalScroll:
        return self.query_one("#chat", VerticalScroll)

    @property
    def monitor(self) -> Monitor:
        return self.query_one(Monitor)

    @property
    def status(self) -> StatusBar:
        return self.query_one(StatusBar)

    def add(self, *widgets) -> None:
        self.chat.mount(*widgets)
        self.chat.scroll_end(animate=False)

    def greet(self) -> None:
        self.add(Static(Text("Hello! I am TINY, a neural network in your computer.\n"
                             "Ask about countries, animals, space, words or sums.", DIM)))

    # ---- actions ----
    def action_help(self) -> None:
        t = Text()
        t.append(" HELP ", "bold #ffffff on " + CYAN2)
        for line in HELP_LINES:
            t.append("\n " + line, TEXT)
        self.add(Static(""), Static(t))

    def action_creativity(self) -> None:
        self.tiny.creativity = (self.tiny.creativity + 1) % 10 if not self.busy else self.tiny.creativity
        self.status.creativity = self.tiny.creativity
        self.status.refresh()
        self.monitor.refresh()

    def action_last_question(self) -> None:
        inp = self.query_one(AskInput)
        inp.value = self.last_q
        inp.cursor_position = len(inp.value)

    def on_key(self, event: events.Key) -> None:
        if self.demo:                       # any key takes over from the demo
            self.demo = False

    # ---- asking ----
    def on_input_submitted(self, event: Input.Submitted) -> None:
        if self.busy:
            return
        line = event.value
        event.input.value = ""
        self.ask_line(line)

    def ask_line(self, line: str) -> None:
        q = self.tiny.clean(line)
        if not q:
            return
        word = q.lower()
        if word in ("quit", "exit"):
            self.exit()
            return
        if word == "help":
            self.action_help()
            return
        if word == "cls":
            self.chat.remove_children()
            return
        self.last_q = q
        self.busy = True
        you = Text.assemble((" YOU ", PINK), ("» ", CYAN2))
        tiny = Text.assemble(("TINY ", CYAN), ("» ", CYAN2))
        answer = Turn(tiny)
        self.add(Static(classes="gap"), Turn(you, Text(q, AMBER)), answer)
        self.status.info = "thinking..."
        self.status.refresh()
        self.think(q, answer)

    @work(thread=True, exclusive=True)
    def think(self, q: str, turn: Turn) -> None:
        tiny = self.tiny
        call = self.call_from_thread
        compute = 0.0
        tiny.reset()
        n = len(q)
        for i, ch in enumerate(q):                      # reading: scanner sweep
            t0 = time.perf_counter()
            tiny.feed(ch)
            compute += time.perf_counter() - t0
            call(self.show_scan, turn, i, n, tiny.layer_activity())
            if self.pace:
                time.sleep(self.pace / 3)
        t0 = time.perf_counter()
        tiny.end_question()
        compute += time.perf_counter() - t0
        out = ""
        while True:                                     # answering: letter by letter
            t0 = time.perf_counter()
            ch = tiny.next()
            compute += time.perf_counter() - t0
            if not ch:
                break
            if self.pace and ch != " ":                 # decode flicker
                call(self.show_answer, turn, out + chr(33 + random.randrange(90)), CYAN)
                time.sleep(self.pace / 2)
            out += ch
            call(self.show_answer, turn, out, GLOW, tiny.layer_activity(), tiny.confidence)
            if self.pace:
                time.sleep(self.pace / 2)
        if not out:
            out = "."
        letters = n + 1 + len(out)
        call(self.finish, turn, out, letters / compute if compute else 0.0, len(out), compute)

    def show_scan(self, turn: Turn, i: int, n: int, act: list[float]) -> None:
        head = i % 18
        head = 17 - head if head >= 9 else head
        t = Text()
        for k in range(10):
            d = abs(head - k)
            t.append("█▓▒"[d] if d < 3 else "·", ["bold #ffffff", PINK, "#8f2f8f"][d] if d < 3 else DIM)
        t.append(f" reading {100 * (i + 1) // n}%", DIM)
        turn.set_body(t)
        self.monitor.update_live(act=act)

    def show_answer(self, turn: Turn, text: str, last_style: str,
                    act: list[float] | None = None, conf: float | None = None) -> None:
        t = Text(text[:-1], MINT)
        t.append(text[-1:], last_style)
        turn.set_body(t)
        self.chat.scroll_end(animate=False)
        if conf is not None:
            self.monitor.update_live(act=act, conf=conf, letter=True)

    def finish(self, turn: Turn, out: str, speed: float, letters: int, secs: float) -> None:
        turn.set_body(Text(out, MINT))
        self.monitor.speed = speed
        self.monitor.refresh()
        self.status.info = f"{letters} ltr {secs * 1000:.0f}ms"
        self.status.refresh()
        self.busy = False

    # ---- demo ----
    @work(exclusive=False, group="demo")
    async def run_demo(self) -> None:
        import asyncio
        inp = self.query_one(AskInput)
        questions = DEMO_QUESTIONS[:]
        random.shuffle(questions)
        for q in questions:
            for k in range(len(q)):
                if not self.demo:
                    return
                inp.value = q[: k + 1]
                inp.cursor_position = k + 1
                await asyncio.sleep(random.uniform(0.045, 0.095))
            await asyncio.sleep(0.25)
            if not self.demo:
                return
            inp.value = ""
            self.ask_line(q)
            while self.busy:
                await asyncio.sleep(0.05)
            await asyncio.sleep(2.0)
        self.demo = False
