# currency_tracker.go
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (Go Edition)
 * Advanced: live rates, historical charts, caching, colored output, stats
 */

package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"math"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

// ─── Types ──────────────────────────────────────────────────────────────────

type RateEntry struct {
	Date string  `json:"date"`
	Rate float64 `json:"rate"`
}

type CacheData struct {
	Base       string       `json:"base"`
	Target     string       `json:"target"`
	Rates      []RateEntry  `json:"rates"`
	LastUpdate string       `json:"last_update"`
}

// ─── Configuration ──────────────────────────────────────────────────────────

const (
	defaultBase   = "USD"
	defaultTarget = "EUR"
	historyDays   = 30
	apiBase       = "https://api.exchangerate.host"
)

// ─── Colors ──────────────────────────────────────────────────────────────────

const (
	reset  = "\x1b[0m"
	bright = "\x1b[1m"
	dim    = "\x1b[2m"
	red    = "\x1b[31m"
	green  = "\x1b[32m"
	yellow = "\x1b[33m"
	blue   = "\x1b[34m"
	magenta = "\x1b[35m"
	cyan   = "\x1b[36m"
)

func c(str, color string) string {
	return color + str + reset
}

// ─── API Client ─────────────────────────────────────────────────────────────

type ExchangeRateAPI struct{}

func (api *ExchangeRateAPI) GetLatest(base, target string) (float64, error) {
	url := fmt.Sprintf("%s/latest?base=%s&symbols=%s", apiBase, base, target)
	resp, err := http.Get(url)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()
	body, _ := ioutil.ReadAll(resp.Body)
	var data map[string]interface{}
	if err := json.Unmarshal(body, &data); err != nil {
		return 0, err
	}
	if success, ok := data["success"].(bool); ok && success {
		rates := data["rates"].(map[string]interface{})
		if rate, ok := rates[target].(float64); ok {
			return rate, nil
		}
	}
	return 0, fmt.Errorf("failed to get rate")
}

func (api *ExchangeRateAPI) GetHistory(base, target string, days int) ([]RateEntry, error) {
	end := time.Now()
	start := end.AddDate(0, 0, -days)
	format := func(t time.Time) string { return t.Format("2006-01-02") }
	url := fmt.Sprintf("%s/timeseries?start_date=%s&end_date=%s&base=%s&symbols=%s",
		apiBase, format(start), format(end), base, target)
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	body, _ := ioutil.ReadAll(resp.Body)
	var data map[string]interface{}
	if err := json.Unmarshal(body, &data); err != nil {
		return nil, err
	}
	if success, ok := data["success"].(bool); ok && success {
		ratesMap := data["rates"].(map[string]interface{})
		var entries []RateEntry
		for date, rateObj := range ratesMap {
			r := rateObj.(map[string]interface{})
			if rate, ok := r[target].(float64); ok {
				entries = append(entries, RateEntry{Date: date, Rate: rate})
			}
		}
		sort.Slice(entries, func(i, j int) bool { return entries[i].Date < entries[j].Date })
		return entries, nil
	}
	return nil, fmt.Errorf("failed to get history")
}

// ─── Cache Manager ─────────────────────────────────────────────────────────

type Cache struct {
	filePath string
	data     CacheData
}

func NewCache() *Cache {
	home, _ := os.UserHomeDir()
	cacheDir := filepath.Join(home, ".currency_tracker")
	os.MkdirAll(cacheDir, 0755)
	c := &Cache{filePath: filepath.Join(cacheDir, "cache.json")}
	c.load()
	return c
}

func (c *Cache) load() {
	if _, err := os.Stat(c.filePath); os.IsNotExist(err) {
		c.data = CacheData{Base: defaultBase, Target: defaultTarget, Rates: []RateEntry{}, LastUpdate: ""}
		return
	}
	raw, err := ioutil.ReadFile(c.filePath)
	if err != nil {
		c.data = CacheData{Base: defaultBase, Target: defaultTarget, Rates: []RateEntry{}, LastUpdate: ""}
		return
	}
	var data CacheData
	if err := json.Unmarshal(raw, &data); err != nil {
		c.data = CacheData{Base: defaultBase, Target: defaultTarget, Rates: []RateEntry{}, LastUpdate: ""}
		return
	}
	c.data = data
}

func (c *Cache) save() {
	raw, _ := json.MarshalIndent(c.data, "", "  ")
	ioutil.WriteFile(c.filePath, raw, 0644)
}

func (c *Cache) Get(base, target string) []RateEntry {
	if c.data.Base == base && c.data.Target == target {
		return c.data.Rates
	}
	return nil
}

func (c *Cache) Set(base, target string, rates []RateEntry) {
	c.data.Base = base
	c.data.Target = target
	c.data.Rates = rates
	c.data.LastUpdate = time.Now().Format(time.RFC3339)
	c.save()
}

func (c *Cache) Clear() {
	c.data = CacheData{Base: defaultBase, Target: defaultTarget, Rates: []RateEntry{}, LastUpdate: ""}
	c.save()
}

// ─── Chart Renderer ─────────────────────────────────────────────────────────

func drawAsciiChart(rates []RateEntry, width, height int) string {
	if len(rates) == 0 {
		return "No data available."
	}
	values := make([]float64, len(rates))
	for i, r := range rates {
		values[i] = r.Rate
	}
	minVal, maxVal := values[0], values[0]
	for _, v := range values {
		if v < minVal {
			minVal = v
		}
		if v > maxVal {
			maxVal = v
		}
	}
	rangeVal := maxVal - minVal
	if rangeVal == 0 {
		return fmt.Sprintf("Rate is constant at %.4f", values[0])
	}
	normalized := make([]int, len(values))
	for i, v := range values {
		normalized[i] = int(math.Floor((v - minVal) / rangeVal * float64(height-1)))
	}
	var lines []string
	for row := height - 1; row >= 0; row-- {
		var line strings.Builder
		for i, val := range normalized {
			if val >= row {
				if i > 0 && normalized[i-1] >= row {
					line.WriteString("─")
				} else {
					line.WriteString("┌")
				}
			} else {
				line.WriteString(" ")
			}
		}
		if strings.TrimSpace(line.String()) != "" {
			lines = append(lines, line.String())
		}
	}
	// X axis
	step := len(rates) / 10
	if step < 1 {
		step = 1
	}
	var xAxis strings.Builder
	xAxis.WriteString(" ")
	lastPos := 0
	for i := 0; i < len(rates); i += step {
		label := rates[i].Date[5:10]
		spaces := i - lastPos
		if spaces > 0 {
			xAxis.WriteString(strings.Repeat(" ", spaces))
		}
		xAxis.WriteString(label)
		lastPos = i
	}
	if lastPos < len(rates)-1 {
		xAxis.WriteString(strings.Repeat(" ", len(rates)-1-lastPos))
	}
	result := strings.Join(lines, "\n") + "\n" + xAxis.String()
	result += fmt.Sprintf("\nMin: %.4f  Max: %.4f", minVal, maxVal)
	return result
}

// ─── Main App ──────────────────────────────────────────────────────────────

type CurrencyTracker struct {
	api    *ExchangeRateAPI
	cache  *Cache
	base   string
	target string
	rates  []RateEntry
	reader *bufio.Reader
}

func NewCurrencyTracker() *CurrencyTracker {
	t := &CurrencyTracker{
		api:    &ExchangeRateAPI{},
		cache:  NewCache(),
		base:   defaultBase,
		target: defaultTarget,
		reader: bufio.NewReader(os.Stdin),
	}
	t.loadCachedOrFetch()
	return t
}

func (t *CurrencyTracker) loadCachedOrFetch() {
	if cached := t.cache.Get(t.base, t.target); cached != nil {
		t.rates = cached
		fmt.Println(c(fmt.Sprintf("📂 Loaded cached data for %s/%s", t.base, t.target), dim))
	} else {
		t.fetchHistory()
	}
}

func (t *CurrencyTracker) fetchHistory() {
	fmt.Print(c("Fetching historical data...", dim))
	rates, err := t.api.GetHistory(t.base, t.target, historyDays)
	if err != nil || len(rates) == 0 {
		fmt.Println(c(" ⚠️  No historical data retrieved.", yellow))
		return
	}
	t.rates = rates
	t.cache.Set(t.base, t.target, rates)
	fmt.Println(c(fmt.Sprintf(" ✅ Retrieved %d data points", len(rates)), green))
}

func (t *CurrencyTracker) getCurrentRate() (float64, error) {
	rate, err := t.api.GetLatest(t.base, t.target)
	if err == nil {
		return rate, nil
	}
	if len(t.rates) > 0 {
		return t.rates[len(t.rates)-1].Rate, nil
	}
	return 0, fmt.Errorf("no rate available")
}

func (t *CurrencyTracker) ask(prompt string) string {
	fmt.Print(prompt)
	line, _ := t.reader.ReadString('\n')
	return strings.TrimSpace(line)
}

func (t *CurrencyTracker) askConfirm(prompt string) bool {
	ans := t.ask(prompt + " (yes/no): ")
	ans = strings.ToLower(ans)
	return ans == "yes" || ans == "y"
}

func (t *CurrencyTracker) showMenu() {
	current, _ := t.getCurrentRate()
	currentStr := "N/A"
	if current != 0 {
		currentStr = fmt.Sprintf("%.4f", current)
	}
	fmt.Println("\n" + c(strings.Repeat("═", 50), cyan))
	fmt.Println(c("💱 CURRENCY TRACKER", bright+cyan))
	fmt.Println(c(strings.Repeat("═", 50), cyan))
	fmt.Printf("  Base: %s  Target: %s\n", t.base, t.target)
	fmt.Printf("  Current: %s\n", currentStr)
	fmt.Printf("  Data points: %d\n", len(t.rates))
	fmt.Println(c(strings.Repeat("═", 50), cyan))
	fmt.Println("  1. 📈 Show chart")
	fmt.Println("  2. 💹 Show current rate")
	fmt.Println("  3. 🔄 Change currency pair")
	fmt.Println("  4. 📊 Show statistics")
	fmt.Println("  5. 🔄 Refresh data")
	fmt.Println("  6. 🗑️  Clear cache")
	fmt.Println("  0. 🚪 Exit")
	fmt.Println(c(strings.Repeat("═", 50), cyan))
}

func (t *CurrencyTracker) showChart() {
	if len(t.rates) == 0 {
		fmt.Println(c("No data available. Fetching...", yellow))
		t.fetchHistory()
	}
	if len(t.rates) == 0 {
		fmt.Println(c("No data to display.", red))
		return
	}
	fmt.Println(c(fmt.Sprintf("\n📈 Exchange Rate Chart (last %d days)", len(t.rates)), bright))
	fmt.Println(drawAsciiChart(t.rates, 50, 10))
	fmt.Println(c(fmt.Sprintf("\nLatest: %.4f", t.rates[len(t.rates)-1].Rate), cyan))
}

func (t *CurrencyTracker) showRate() {
	rate, err := t.getCurrentRate()
	if err == nil {
		fmt.Println(c(fmt.Sprintf("\n💹 Current %s/%s: %.4f", t.base, t.target, rate), green))
	} else {
		fmt.Println(c("Could not fetch rate.", red))
	}
}

func (t *CurrencyTracker) showStats() {
	if len(t.rates) == 0 {
		fmt.Println(c("No data available.", yellow))
		return
	}
	var sum float64
	minVal := t.rates[0].Rate
	maxVal := t.rates[0].Rate
	for _, r := range t.rates {
		sum += r.Rate
		if r.Rate < minVal {
			minVal = r.Rate
		}
		if r.Rate > maxVal {
			maxVal = r.Rate
		}
	}
	avg := sum / float64(len(t.rates))
	volatility := (maxVal - minVal) / avg * 100
	last := t.rates[len(t.rates)-1].Rate
	fmt.Println("\n📊 STATISTICS")
	fmt.Println(c(strings.Repeat("─", 30), dim))
	fmt.Printf("  Period:      %d days\n", len(t.rates))
	fmt.Printf("  Average:     %.4f\n", avg)
	fmt.Printf("  Min:         %.4f\n", minVal)
	fmt.Printf("  Max:         %.4f\n", maxVal)
	fmt.Printf("  Volatility:  %.2f%%\n", volatility)
	fmt.Printf("  Current:     %.4f\n", last)
}

func (t *CurrencyTracker) changePair() {
	base := t.ask(fmt.Sprintf("Base currency (default %s): ", t.base))
	target := t.ask(fmt.Sprintf("Target currency (default %s): ", t.target))
	newBase := strings.ToUpper(base)
	if newBase == "" {
		newBase = t.base
	}
	newTarget := strings.ToUpper(target)
	if newTarget == "" {
		newTarget = t.target
	}
	if newBase == t.base && newTarget == t.target {
		fmt.Println("Pair unchanged.")
		return
	}
	t.base = newBase
	t.target = newTarget
	if cached := t.cache.Get(t.base, t.target); cached != nil {
		t.rates = cached
		fmt.Println(c(fmt.Sprintf("📂 Loaded cached data for %s/%s", t.base, t.target), dim))
	} else {
		t.fetchHistory()
	}
}

func (t *CurrencyTracker) refresh() {
	t.fetchHistory()
	fmt.Println(c("✅ Data refreshed.", green))
}

func (t *CurrencyTracker) clearCache() {
	if !t.askConfirm("🗑️  Delete all cached data?") {
		return
	}
	t.cache.Clear()
	t.rates = []RateEntry{}
	fmt.Println(c("Cache cleared.", yellow))
}

func (t *CurrencyTracker) run() {
	fmt.Print("\033[H\033[2J")
	fmt.Println(c("\n💱 Currency Tracker – Live Exchange Rates with Charts", bright+cyan))
	fmt.Println(c("Track the pulse of the global economy.", dim))

	for {
		t.showMenu()
		choice := t.ask("Your choice: ")
		switch choice {
		case "1":
			t.showChart()
		case "2":
			t.showRate()
		case "3":
			t.changePair()
		case "4":
			t.showStats()
		case "5":
			t.refresh()
		case "6":
			t.clearCache()
		case "0":
			fmt.Println(c("👋 Goodbye!", cyan))
			return
		default:
			fmt.Println(c("❌ Invalid choice.", red))
		}
		if choice != "0" {
			fmt.Print("\nPress Enter to continue...")
			t.reader.ReadString('\n')
		}
	}
}

// ─── Main ────────────────────────────────────────────────────────────────────

func main() {
	app := NewCurrencyTracker()
	app.run()
}
