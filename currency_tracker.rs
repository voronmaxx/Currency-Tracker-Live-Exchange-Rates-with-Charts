# currency_tracker.rs
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (Rust Edition)
 * Advanced: live rates, historical charts, caching, colored output, stats
 * Dependencies: reqwest, serde, serde_json, chrono
 */

use chrono::{Duration, Utc};
use reqwest;
use serde::Deserialize;
use serde_json;
use std::collections::HashMap;
use std::fs;
use std::io::{self, Write, BufRead};
use std::path::PathBuf;

// ─── Types ──────────────────────────────────────────────────────────────────

#[derive(Debug, Deserialize)]
struct ApiResponse {
    success: bool,
    rates: Option<HashMap<String, f64>>,
}

#[derive(Debug, Deserialize)]
struct TimeseriesResponse {
    success: bool,
    rates: Option<HashMap<String, HashMap<String, f64>>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct RateEntry {
    date: String,
    rate: f64,
}

#[derive(Debug, Serialize, Deserialize)]
struct CacheData {
    base: String,
    target: String,
    rates: Vec<RateEntry>,
    last_update: String,
}

// ─── Configuration ──────────────────────────────────────────────────────────

const DEFAULT_BASE: &str = "USD";
const DEFAULT_TARGET: &str = "EUR";
const HISTORY_DAYS: i64 = 30;
const API_BASE: &str = "https://api.exchangerate.host";

// ─── Colors ──────────────────────────────────────────────────────────────────

fn c(text: &str, color: &str) -> String {
    format!("{}{}{}", color, text, "\x1b[0m")
}

const RESET: &str = "\x1b[0m";
const BRIGHT: &str = "\x1b[1m";
const DIM: &str = "\x1b[2m";
const RED: &str = "\x1b[31m";
const GREEN: &str = "\x1b[32m";
const YELLOW: &str = "\x1b[33m";
const BLUE: &str = "\x1b[34m";
const MAGENTA: &str = "\x1b[35m";
const CYAN: &str = "\x1b[36m";

// ─── API Client ─────────────────────────────────────────────────────────────

struct ExchangeRateAPI;

impl ExchangeRateAPI {
    async fn get_latest(base: &str, target: &str) -> Result<f64, Box<dyn std::error::Error>> {
        let url = format!("{}/latest?base={}&symbols={}", API_BASE, base, target);
        let resp = reqwest::get(&url).await?;
        let data: ApiResponse = resp.json().await?;
        if data.success {
            if let Some(rates) = data.rates {
                if let Some(rate) = rates.get(target) {
                    return Ok(*rate);
                }
            }
        }
        Err("Failed to get rate".into())
    }

    async fn get_history(base: &str, target: &str, days: i64) -> Result<Vec<RateEntry>, Box<dyn std::error::Error>> {
        let end = Utc::now();
        let start = end - Duration::days(days);
        let url = format!(
            "{}/timeseries?start_date={}&end_date={}&base={}&symbols={}",
            API_BASE,
            start.format("%Y-%m-%d"),
            end.format("%Y-%m-%d"),
            base,
            target
        );
        let resp = reqwest::get(&url).await?;
        let data: TimeseriesResponse = resp.json().await?;
        if data.success {
            if let Some(rates) = data.rates {
                let mut entries: Vec<RateEntry> = Vec::new();
                for (date, rate_map) in rates {
                    if let Some(rate) = rate_map.get(target) {
                        entries.push(RateEntry { date, rate: *rate });
                    }
                }
                entries.sort_by(|a, b| a.date.cmp(&b.date));
                return Ok(entries);
            }
        }
        Err("Failed to get history".into())
    }
}

// ─── Cache Manager ─────────────────────────────────────────────────────────

struct Cache {
    file_path: PathBuf,
    data: CacheData,
}

impl Cache {
    fn new() -> Self {
        let home = std::env::var("HOME").or_else(|_| std::env::var("USERPROFILE")).unwrap_or_else(|_| ".".to_string());
        let cache_dir = PathBuf::from(home).join(".currency_tracker");
        fs::create_dir_all(&cache_dir).unwrap();
        let file_path = cache_dir.join("cache.json");
        let mut cache = Cache { file_path, data: CacheData { base: DEFAULT_BASE.to_string(), target: DEFAULT_TARGET.to_string(), rates: Vec::new(), last_update: String::new() } };
        cache.load();
        cache
    }

    fn load(&mut self) {
        if let Ok(raw) = fs::read_to_string(&self.file_path) {
            if let Ok(data) = serde_json::from_str::<CacheData>(&raw) {
                self.data = data;
                return;
            }
        }
        self.data = CacheData { base: DEFAULT_BASE.to_string(), target: DEFAULT_TARGET.to_string(), rates: Vec::new(), last_update: String::new() };
    }

    fn save(&self) {
        let raw = serde_json::to_string_pretty(&self.data).unwrap();
        let _ = fs::write(&self.file_path, raw);
    }

    fn get(&self, base: &str, target: &str) -> Option<Vec<RateEntry>> {
        if self.data.base == base && self.data.target == target {
            Some(self.data.rates.clone())
        } else {
            None
        }
    }

    fn set(&mut self, base: &str, target: &str, rates: Vec<RateEntry>) {
        self.data.base = base.to_string();
        self.data.target = target.to_string();
        self.data.rates = rates;
        self.data.last_update = Utc::now().to_rfc3339();
        self.save();
    }

    fn clear(&mut self) {
        self.data = CacheData { base: DEFAULT_BASE.to_string(), target: DEFAULT_TARGET.to_string(), rates: Vec::new(), last_update: String::new() };
        self.save();
    }
}

// ─── Chart Renderer ─────────────────────────────────────────────────────────

fn draw_ascii_chart(rates: &[RateEntry], width: usize, height: usize) -> String {
    if rates.is_empty() {
        return "No data available.".to_string();
    }
    let values: Vec<f64> = rates.iter().map(|r| r.rate).collect();
    let min_val = values.iter().fold(f64::INFINITY, |a, &b| a.min(b));
    let max_val = values.iter().fold(f64::NEG_INFINITY, |a, &b| a.max(b));
    let range = max_val - min_val;
    if range == 0.0 {
        return format!("Rate is constant at {:.4}", values[0]);
    }
    let normalized: Vec<usize> = values.iter().map(|v| (((v - min_val) / range * (height - 1) as f64) as usize)).collect();
    let mut lines = Vec::new();
    for row in (0..height).rev() {
        let mut line = String::new();
        for (i, &val) in normalized.iter().enumerate() {
            if val >= row {
                if i > 0 && normalized[i-1] >= row {
                    line.push('─');
                } else {
                    line.push('┌');
                }
            } else {
                line.push(' ');
            }
        }
        if line.trim() != "" {
            lines.push(line);
        }
    }
    // X axis
    let step = if rates.len() / 10 > 0 { rates.len() / 10 } else { 1 };
    let mut x_axis = String::from(" ");
    let mut last_pos = 0;
    for i in (0..rates.len()).step_by(step) {
        let label = &rates[i].date[5..10];
        let spaces = i - last_pos;
        if spaces > 0 {
            x_axis.push_str(&" ".repeat(spaces));
        }
        x_axis.push_str(label);
        last_pos = i;
    }
    if last_pos < rates.len() - 1 {
        x_axis.push_str(&" ".repeat(rates.len() - 1 - last_pos));
    }
    let mut result = lines.join("\n") + "\n" + &x_axis;
    result.push_str(&format!("\nMin: {:.4}  Max: {:.4}", min_val, max_val));
    result
}

// ─── Main App ──────────────────────────────────────────────────────────────

struct CurrencyTracker {
    cache: Cache,
    base: String,
    target: String,
    rates: Vec<RateEntry>,
}

impl CurrencyTracker {
    fn new() -> Self {
        let mut app = CurrencyTracker {
            cache: Cache::new(),
            base: DEFAULT_BASE.to_string(),
            target: DEFAULT_TARGET.to_string(),
            rates: Vec::new(),
        };
        app.load_cached_or_fetch_blocking();
        app
    }

    fn load_cached_or_fetch_blocking(&mut self) {
        if let Some(cached) = self.cache.get(&self.base, &self.target) {
            self.rates = cached;
            println!("{}", c(&format!("📂 Loaded cached data for {}/{}", self.base, self.target), DIM));
        } else {
            self.fetch_history_blocking();
        }
    }

    fn fetch_history_blocking(&mut self) {
        print!("{}", c("Fetching historical data...", DIM));
        io::stdout().flush().unwrap();
        let rt = tokio::runtime::Runtime::new().unwrap();
        let rates = rt.block_on(ExchangeRateAPI::get_history(&self.base, &self.target, HISTORY_DAYS));
        match rates {
            Ok(rates) if !rates.is_empty() => {
                self.rates = rates;
                self.cache.set(&self.base, &self.target, self.rates.clone());
                println!("{}", c(&format!(" ✅ Retrieved {} data points", self.rates.len()), GREEN));
            }
            _ => {
                println!("{}", c(" ⚠️  No historical data retrieved.", YELLOW));
            }
        }
    }

    fn get_current_rate_blocking(&self) -> Option<f64> {
        let rt = tokio::runtime::Runtime::new().unwrap();
        let rate = rt.block_on(ExchangeRateAPI::get_latest(&self.base, &self.target));
        if let Ok(r) = rate {
            Some(r)
        } else if !self.rates.is_empty() {
            Some(self.rates[self.rates.len()-1].rate)
        } else {
            None
        }
    }

    fn ask(&self, prompt: &str) -> String {
        print!("{}", prompt);
        io::stdout().flush().unwrap();
        let stdin = io::stdin();
        let mut line = String::new();
        stdin.lock().read_line(&mut line).unwrap();
        line.trim().to_string()
    }

    fn ask_confirm(&self, prompt: &str) -> bool {
        let ans = self.ask(&format!("{} (yes/no): ", prompt));
        let a = ans.to_lowercase();
        a == "yes" || a == "y"
    }

    fn show_menu(&self) {
        let current = self.get_current_rate_blocking();
        let current_str = match current {
            Some(r) => format!("{:.4}", r),
            None => "N/A".to_string(),
        };
        println!("\n{}", c(&"=".repeat(50), CYAN));
        println!("{}", c("💱 CURRENCY TRACKER", &format!("{}{}", BRIGHT, CYAN)));
        println!("{}", c(&"=".repeat(50), CYAN));
        println!("  Base: {}  Target: {}", self.base, self.target);
        println!("  Current: {}", current_str);
        println!("  Data points: {}", self.rates.len());
        println!("{}", c(&"=".repeat(50), CYAN));
        println!("  1. 📈 Show chart");
        println!("  2. 💹 Show current rate");
        println!("  3. 🔄 Change currency pair");
        println!("  4. 📊 Show statistics");
        println!("  5. 🔄 Refresh data");
        println!("  6. 🗑️  Clear cache");
        println!("  0. 🚪 Exit");
        println!("{}", c(&"=".repeat(50), CYAN));
    }

    fn show_chart(&self) {
        if self.rates.is_empty() {
            println!("{}", c("No data available. Fetching...", YELLOW));
            return;
        }
        println!("{}", c(&format!("\n📈 Exchange Rate Chart (last {} days)", self.rates.len()), BRIGHT));
        println!("{}", draw_ascii_chart(&self.rates, 50, 10));
        println!("{}", c(&format!("\nLatest: {:.4}", self.rates[self.rates.len()-1].rate), CYAN));
    }

    fn show_rate(&self) {
        if let Some(rate) = self.get_current_rate_blocking() {
            println!("{}", c(&format!("\n💹 Current {}/{}: {:.4}", self.base, self.target, rate), GREEN));
        } else {
            println!("{}", c("Could not fetch rate.", RED));
        }
    }

    fn show_stats(&self) {
        if self.rates.is_empty() {
            println!("{}", c("No data available.", YELLOW));
            return;
        }
        let values: Vec<f64> = self.rates.iter().map(|r| r.rate).collect();
        let sum: f64 = values.iter().sum();
        let avg = sum / values.len() as f64;
        let min_val = values.iter().fold(f64::INFINITY, |a, &b| a.min(b));
        let max_val = values.iter().fold(f64::NEG_INFINITY, |a, &b| a.max(b));
        let volatility = (max_val - min_val) / avg * 100.0;
        let last = values[values.len()-1];
        println!("\n📊 STATISTICS");
        println!("{}", c(&"-".repeat(30), DIM));
        println!("  Period:      {} days", self.rates.len());
        println!("  Average:     {:.4}", avg);
        println!("  Min:         {:.4}", min_val);
        println!("  Max:         {:.4}", max_val);
        println!("  Volatility:  {:.2}%", volatility);
        println!("  Current:     {:.4}", last);
    }

    fn change_pair(&mut self) {
        let base = self.ask(&format!("Base currency (default {}): ", self.base));
        let target = self.ask(&format!("Target currency (default {}): ", self.target));
        let new_base = if base.is_empty() { self.base.clone() } else { base.to_uppercase() };
        let new_target = if target.is_empty() { self.target.clone() } else { target.to_uppercase() };
        if new_base == self.base && new_target == self.target {
            println!("Pair unchanged.");
            return;
        }
        self.base = new_base;
        self.target = new_target;
        if let Some(cached) = self.cache.get(&self.base, &self.target) {
            self.rates = cached;
            println!("{}", c(&format!("📂 Loaded cached data for {}/{}", self.base, self.target), DIM));
        } else {
            self.fetch_history_blocking();
        }
    }

    fn refresh(&mut self) {
        self.fetch_history_blocking();
        println!("{}", c("✅ Data refreshed.", GREEN));
    }

    fn clear_cache(&mut self) {
        if !self.ask_confirm("🗑️  Delete all cached data?") {
            return;
        }
        self.cache.clear();
        self.rates = Vec::new();
        println!("{}", c("Cache cleared.", YELLOW));
    }

    fn run(&mut self) {
        println!("{}", c("\n💱 Currency Tracker – Live Exchange Rates with Charts", &format!("{}{}", BRIGHT, CYAN)));
        println!("{}", c("Track the pulse of the global economy.", DIM));

        loop {
            self.show_menu();
            let choice = self.ask("Your choice: ");
            match choice.as_str() {
                "1" => self.show_chart(),
                "2" => self.show_rate(),
                "3" => self.change_pair(),
                "4" => self.show_stats(),
                "5" => self.refresh(),
                "6" => self.clear_cache(),
                "0" => {
                    println!("{}", c("👋 Goodbye!", CYAN));
                    break;
                }
                _ => println!("{}", c("❌ Invalid choice.", RED)),
            }
            if choice != "0" {
                print!("\nPress Enter to continue...");
                io::stdout().flush().unwrap();
                let mut _dummy = String::new();
                io::stdin().read_line(&mut _dummy).unwrap();
            }
        }
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut app = CurrencyTracker::new();
    app.run();
    Ok(())
}
