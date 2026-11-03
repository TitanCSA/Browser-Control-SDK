"""Observe, pause for a person, then observe the same tab. Never submits a form."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "SDK" / "Python"))
from browser_control import Browser, BrowserError


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python Examples/human_checkpoint.py TAB_ID")
    tab = Browser.connect().tab(sys.argv[1])
    print(tab.observe(max_text=1200))
    try:
        answer = input("Finish the authorized manual step in this tab, then type continue: ")
    except EOFError:
        raise SystemExit("No human response; nothing was resumed.")
    if answer.strip() != "continue":
        raise SystemExit("Cancelled; nothing was resumed.")
    # Manual navigation can invalidate old element refs. Observe again.
    print(tab.observe(max_text=1200))
    print("Review the fresh state before adding the next authorized action.")


if __name__ == "__main__":
    try:
        main()
    except BrowserError as error:
        print(f"{error}\nInspect the browser before retrying any earlier write.", file=sys.stderr)
        raise SystemExit(1)
