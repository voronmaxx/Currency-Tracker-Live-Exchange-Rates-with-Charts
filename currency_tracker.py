# currency_tracker.py
#!/usr/bin/env python3
"""
💱 Currency Tracker – Live Exchange Rates with ASCII Charts (Python Edition)
Advanced: live rates, historical charts, caching, colored output, stats
"""

import json
import os
import sys
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import requests

try:
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.prompt import Prompt, Confirm
    from rich.progress import Progress, SpinnerColumn, TextColumn
    from rich import box
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False
    print("⚠️  Install 'rich' for enhanced UI: pip install rich")


# ─── Configuration ──────────────────────────────────────────────────────────

CACHE_DIR = Path.home() / ".currency_tracker"
CACHE_FILE = CACHE_DIR / "cache.json"
API_BASE = "https://api.exchangerate.host"
DEFAULT_BASE = "USD"
DEFAULT_TARGET = "EUR"
HISTORY_DAYS = 30


# ─── Colors ──────────────────────────────────────────────────────────────────

def c(text: str, color: str) -> str:
    colors = {
        "reset": "\033[0m", "bright": "\033[1m", "dim": "\033[2m",
        "red": "\033[31m", "green": "\033[32m", "yellow": "\033[33m",
        "blue": "\033[34m", "magenta": "\033[35m", "cyan": "\033[36m"
    }
    return f"{colors.get(color, '')}{text}{colors['reset']}"


# ─── API Client ─────────────────────────────────────────────────────────────

class ExchangeRateAPI:
    @staticmethod
    def get_latest(base: str, target: str) -> Optional[float]:
        """Get current exchange rate for base→target."""
        url = f"{API_BASE}/latest?base={base}&symbols={target}"
        try:
            resp = requests.get(url, timeout=10)
            resp.raise_for_status()
            data = resp.json()
            if data.get("success", False):
                return data["rates"].get(target)
            return None
        except Exception as e:
            print(c(f"❌ API error: {e}", "red"))
            return None

    @staticmethod
    def get_history(base: str, target: str, days: int = HISTORY_DAYS) -> List[Tuple[str, float]]:
        """Get historical rates for the last N days."""
        end = datetime.now()
        start = end - timedelta(days=days)
        url = (f"{API_BASE}/timeseries?start_date={start.strftime('%Y-%m-%d')}"
               f"&end_date={end.strftime('%Y-%m-%d')}&base={base}&symbols={target}")
        try:
            resp = requests.get(url, timeout=15)
            resp.raise_for_status()
            data = resp.json()
            if data.get("success", False):
                rates = data.get("rates", {})
                # Sort by date
                items = sorted(rates.items())
                return [(date, rates[date][target]) for date, rate in items if target in rates[date]]
            return []
        except Exception as e:
            print(c(f"❌ API error: {e}", "red"))
            return []


# ─── Cache Manager ──────────────────────────────────────────────────────────

class Cache:
    def __init__(self):
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        self.data = self._load()

    def _load(self) -> Dict:
        if CACHE_FILE.exists():
            try:
                with open(CACHE_FILE, 'r', encoding='utf-8') as f:
                    return json.load(f)
            except Exception:
                return {"base": DEFAULT_BASE, "target": DEFAULT_TARGET, "rates": [], "last_update": ""}
        return {"base": DEFAULT_BASE, "target": DEFAULT_TARGET, "rates": [], "last_update": ""}

    def save(self) -> None:
        with open(CACHE_FILE, 'w', encoding='utf-8') as f:
            json.dump(self.data, f, indent=2, ensure_ascii=False)

    def get(self, base: str, target: str) -> Optional[List[Tuple[str, float]]]:
        if self.data["base"] == base and self.data["target"] == target:
            return [(r["date"], r["rate"]) for r in self.data["rates"]]
        return None

    def set(self, base: str, target: str, rates: List[Tuple[str, float]]) -> None:
        self.data["base"] = base
        self.data["target"] = target
        self.data["rates"] = [{"date": d, "rate": r} for d, r in rates]
        self.data["last_update"] = datetime.now().isoformat()
        self.save()

    def clear(self) -> None:
        self.data = {"base": DEFAULT_BASE, "target": DEFAULT_TARGET, "rates": [], "last_update": ""}
        self.save()


# ─── Chart Renderer ─────────────────────────────────────────────────────────

def draw_ascii_chart(rates: List[Tuple[str, float]], width: int = 50, height: int = 10) -> str:
    """Render a simple ASCII line chart."""
    if not rates:
        return "No data available."

    values = [r for _, r in rates]
    min_val = min(values)
    max_val = max(values)
    range_val = max_val - min_val
    if range_val == 0:
        return f"Rate is constant at {values[0]:.4f}"

    # Normalize to height
    normalized = [int((v - min_val) / range_val * (height - 1)) for v in values]
    # Build chart lines bottom-up
    chart_lines = []
    for row in range(height - 1, -1, -1):
        line = ""
        for i, val in enumerate(normalized):
            if val >= row:
                # Draw line: if previous also high, draw '─', else draw '┌' or '└' depending
                # Simplify: use '*' for points, '-' for lines
                if i > 0 and normalized[i-1] >= row:
                    line += "─"
                else:
                    line += "┌"
            else:
                line += " "
        # If there is any non-space, add it, else skip
        if any(ch != ' ' for ch in line):
            chart_lines.append(line)
    # Add X axis with dates
    date_labels = []
    step = max(1, len(rates) // 10)
    for i in range(0, len(rates), step):
        date_labels.append((i, rates[i][0][5:10]))  # MM-DD
    x_axis = " " * 1
    last_pos = 0
    for pos, label in date_labels:
        x_axis += " " * (pos - last_pos) + label
        last_pos = pos
    # If last label not at end, add padding
    if last_pos < len(rates) - 1:
        x_axis += " " * (len(rates) - last_pos - 1)

    result = "\n".join(chart_lines) + "\n" + x_axis
    # Add min/max labels on Y axis
    result += f"\nMin: {min_val:.4f}  Max: {max_val:.4f}"
    return result


# ─── Main App ──────────────────────────────────────────────────────────────

class CurrencyTracker:
    def __init__(self):
        self.console = Console() if RICH_AVAILABLE else None
        self.cache = Cache()
        self.base = DEFAULT_BASE
        self.target = DEFAULT_TARGET
        self.rates: List[Tuple[str, float]] = []
        self._load_cached_or_fetch()

    def _load_cached_or_fetch(self) -> None:
        cached = self.cache.get(self.base, self.target)
        if cached:
            self.rates = cached
            if self.console:
                self.console.print(c(f"📂 Loaded cached data for {self.base}/{self.target}", "dim"))
        else:
            self._fetch_history()

    def _fetch_history(self) -> None:
        if self.console:
            with Progress(SpinnerColumn(), TextColumn("[progress.description]{task.description}"), transient=True) as prog:
                prog.add_task(description="Fetching historical data...", total=None)
                self.rates = ExchangeRateAPI.get_history(self.base, self.target)
        else:
            print(c("Fetching historical data...", "dim"))
            self.rates = ExchangeRateAPI.get_history(self.base, self.target)
        if self.rates:
            self.cache.set(self.base, self.target, self.rates)
            if self.console:
                self.console.print(c(f"✅ Retrieved {len(self.rates)} data points", "green"))
        else:
            print(c("⚠️  No historical data retrieved. Using cache if available.", "yellow"))

    def _get_current_rate(self) -> Optional[float]:
        rate = ExchangeRateAPI.get_latest(self.base, self.target)
        if rate is not None:
            return rate
        # fallback to latest from cache
        if self.rates:
            return self.rates[-1][1]
        return None

    def show_menu(self) -> None:
        if self.console:
            panel = Panel(
                f"[bold cyan]💱 Currency Tracker[/bold cyan]\n"
                f"  Base: {self.base}  Target: {self.target}\n"
                f"  Current: {self._get_current_rate() or 'N/A'}\n"
                f"  Data points: {len(self.rates)}",
                title="📊 Main Menu",
                border_style="blue"
            )
            self.console.print(panel)
            self.console.print("[1] 📈 Show chart")
            self.console.print("[2] 💹 Show current rate")
            self.console.print("[3] 🔄 Change currency pair")
            self.console.print("[4] 📊 Show statistics")
            self.console.print("[5] 🔄 Refresh data")
            self.console.print("[6] 🗑️  Clear cache")
            self.console.print("[0] 🚪 Exit")
        else:
            print("\n" + "="*50)
            print(c("💱 CURRENCY TRACKER", "bright"))
            print("="*50)
            print(f"  Base: {self.base}  Target: {self.target}")
            print(f"  Current: {self._get_current_rate() or 'N/A'}")
            print(f"  Data points: {len(self.rates)}")
            print("="*50)
            print("  1. 📈 Show chart")
            print("  2. 💹 Show current rate")
            print("  3. 🔄 Change currency pair")
            print("  4. 📊 Show statistics")
            print("  5. 🔄 Refresh data")
            print("  6. 🗑️  Clear cache")
            print("  0. 🚪 Exit")
            print("="*50)

    def show_chart(self) -> None:
        if not self.rates:
            print(c("No data available. Fetching...", "yellow"))
            self._fetch_history()
        if not self.rates:
            print(c("No data to display.", "red"))
            return
        print(c("\n📈 Exchange Rate Chart (last {} days)".format(len(self.rates)), "bright"))
        print(draw_ascii_chart(self.rates))
        print(c("\nLatest: {:.4f}".format(self.rates[-1][1]), "cyan"))

    def show_rate(self) -> None:
        rate = self._get_current_rate()
        if rate is not None:
            print(c(f"\n💹 Current {self.base}/{self.target}: {rate:.4f}", "green"))
        else:
            print(c("Could not fetch rate.", "red"))

    def show_stats(self) -> None:
        if not self.rates:
            print(c("No data available.", "yellow"))
            return
        values = [r for _, r in self.rates]
        avg = sum(values) / len(values)
        minv = min(values)
        maxv = max(values)
        volatility = (maxv - minv) / avg * 100
        last = values[-1]
        if self.console:
            table = Table(title="📊 Statistics", box=box.ROUNDED)
            table.add_column("Metric", style="cyan")
            table.add_column("Value", style="green")
            table.add_row("Period", f"{len(self.rates)} days")
            table.add_row("Average", f"{avg:.4f}")
            table.add_row("Min", f"{minv:.4f}")
            table.add_row("Max", f"{maxv:.4f}")
            table.add_row("Volatility", f"{volatility:.2f}%")
            table.add_row("Current", f"{last:.4f}")
            self.console.print(table)
        else:
            print("\n📊 STATISTICS")
            print("-"*30)
            print(f"  Period:      {len(self.rates)} days")
            print(f"  Average:     {avg:.4f}")
            print(f"  Min:         {minv:.4f}")
            print(f"  Max:         {maxv:.4f}")
            print(f"  Volatility:  {volatility:.2f}%")
            print(f"  Current:     {last:.4f}")

    def change_pair(self) -> None:
        if self.console:
            base = Prompt.ask("Base currency (e.g., USD)", default=self.base)
            target = Prompt.ask("Target currency (e.g., EUR)", default=self.target)
        else:
            base = input(f"Base currency (default {self.base}): ").strip().upper() or self.base
            target = input(f"Target currency (default {self.target}): ").strip().upper() or self.target
        if base == self.base and target == self.target:
            print("Pair unchanged.")
            return
        self.base = base.upper()
        self.target = target.upper()
        # Check cache for this pair
        cached = self.cache.get(self.base, self.target)
        if cached:
            self.rates = cached
            print(c(f"📂 Loaded cached data for {self.base}/{self.target}", "dim"))
        else:
            self._fetch_history()

    def refresh(self) -> None:
        self._fetch_history()
        print(c("✅ Data refreshed.", "green"))

    def clear_cache(self) -> None:
        if self.console:
            if not Confirm.ask("🗑️  Delete all cached data?"):
                return
        else:
            resp = input("Delete all cached data? (yes/no): ").strip().lower()
            if resp != "yes":
                return
        self.cache.clear()
        self.rates = []
        print(c("Cache cleared.", "yellow"))

    def run(self) -> None:
        while True:
            self.show_menu()
            choice = self._get_choice()
            if choice == "1":
                self.show_chart()
            elif choice == "2":
                self.show_rate()
            elif choice == "3":
                self.change_pair()
            elif choice == "4":
                self.show_stats()
            elif choice == "5":
                self.refresh()
            elif choice == "6":
                self.clear_cache()
            elif choice == "0":
                print(c("👋 Goodbye!", "cyan"))
                break
            else:
                print(c("❌ Invalid choice.", "red"))
            if choice != "0":
                if self.console:
                    self.console.print("\n[dim]Press Enter to continue...[/dim]")
                    input()
                else:
                    input("\nPress Enter to continue...")

    def _get_choice(self) -> str:
        if self.console:
            return Prompt.ask("Your choice", choices=["0","1","2","3","4","5","6"])
        return input("Your choice: ").strip()


# ─── Main ────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    try:
        app = CurrencyTracker()
        app.run()
    except KeyboardInterrupt:
        print("\n👋 Goodbye!")
        sys.exit(0)
    except Exception as e:
        print(c(f"❌ Unexpected error: {e}", "red"))
        sys.exit(1)
