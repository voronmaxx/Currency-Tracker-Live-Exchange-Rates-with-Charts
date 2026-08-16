# CurrencyTracker.cs
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (C# Edition)
 * Advanced: live rates, historical charts, caching, colorful CLI, stats
 * Requires: .NET 6.0+
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

// ─── Data Classes ──────────────────────────────────────────────────────────

public class RateEntry
{
    [JsonPropertyName("date")]
    public string Date { get; set; } = "";
    
    [JsonPropertyName("rate")]
    public double Rate { get; set; }
}

public class CacheData
{
    [JsonPropertyName("base")]
    public string Base { get; set; } = "USD";
    
    [JsonPropertyName("target")]
    public string Target { get; set; } = "EUR";
    
    [JsonPropertyName("rates")]
    public List<RateEntry> Rates { get; set; } = new();
    
    [JsonPropertyName("last_update")]
    public string LastUpdate { get; set; } = "";
}

// ─── Main App ──────────────────────────────────────────────────────────────

public class CurrencyTracker
{
    // ─── Configuration ──────────────────────────────────────────────────────

    private const string DefaultBase = "USD";
    private const string DefaultTarget = "EUR";
    private const int HistoryDays = 30;
    private const string ApiBase = "https://api.exchangerate.host";

    // ─── Colors ─────────────────────────────────────────────────────────────

    private static readonly string Reset = "\u001B[0m";
    private static readonly string Bright = "\u001B[1m";
    private static readonly string Dim = "\u001B[2m";
    private static readonly string Red = "\u001B[31m";
    private static readonly string Green = "\u001B[32m";
    private static readonly string Yellow = "\u001B[33m";
    private static readonly string Blue = "\u001B[34m";
    private static readonly string Magenta = "\u001B[35m";
    private static readonly string Cyan = "\u001B[36m";

    private static string C(string text, string color) => color + text + Reset;

    // ─── API Client ─────────────────────────────────────────────────────────

    private static readonly HttpClient httpClient = new HttpClient();

    private static async Task<double> GetLatestAsync(string baseCurrency, string targetCurrency)
    {
        string url = $"{ApiBase}/latest?base={baseCurrency}&symbols={targetCurrency}";
        var response = await httpClient.GetStringAsync(url);
        using var doc = JsonDocument.Parse(response);
        var root = doc.RootElement;
        if (root.GetProperty("success").GetBoolean())
        {
            var rates = root.GetProperty("rates");
            return rates.GetProperty(targetCurrency).GetDouble();
        }
        throw new Exception("Failed to get rate");
    }

    private static async Task<List<RateEntry>> GetHistoryAsync(string baseCurrency, string targetCurrency, int days)
    {
        var end = DateTime.Now;
        var start = end.AddDays(-days);
        string url = $"{ApiBase}/timeseries?start_date={start:yyyy-MM-dd}&end_date={end:yyyy-MM-dd}&base={baseCurrency}&symbols={targetCurrency}";
        var response = await httpClient.GetStringAsync(url);
        using var doc = JsonDocument.Parse(response);
        var root = doc.RootElement;
        if (root.GetProperty("success").GetBoolean())
        {
            var ratesObj = root.GetProperty("rates");
            var entries = new List<RateEntry>();
            foreach (var prop in ratesObj.EnumerateObject())
            {
                string date = prop.Name;
                double rate = prop.Value.GetProperty(targetCurrency).GetDouble();
                entries.Add(new RateEntry { Date = date, Rate = rate });
            }
            entries = entries.OrderBy(e => e.Date).ToList();
            return entries;
        }
        throw new Exception("Failed to get history");
    }

    // ─── Cache Manager ─────────────────────────────────────────────────────

    private class Cache
    {
        private readonly string filePath;
        private CacheData data;

        public Cache()
        {
            string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            string dir = Path.Combine(home, ".currency_tracker");
            Directory.CreateDirectory(dir);
            filePath = Path.Combine(dir, "cache.json");
            Load();
        }

        private void Load()
        {
            if (File.Exists(filePath))
            {
                try
                {
                    string json = File.ReadAllText(filePath);
                    data = JsonSerializer.Deserialize<CacheData>(json) ?? new CacheData();
                    return;
                }
                catch { /* ignore */ }
            }
            data = new CacheData();
        }

        public void Save()
        {
            string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(filePath, json);
        }

        public List<RateEntry> Get(string baseCurrency, string targetCurrency)
        {
            if (data.Base == baseCurrency && data.Target == targetCurrency)
                return data.Rates;
            return null;
        }

        public void Set(string baseCurrency, string targetCurrency, List<RateEntry> rates)
        {
            data.Base = baseCurrency;
            data.Target = targetCurrency;
            data.Rates = rates;
            data.LastUpdate = DateTime.Now.ToString("yyyy-MM-dd");
            Save();
        }

        public void Clear()
        {
            data = new CacheData();
            Save();
        }
    }

    // ─── Chart Renderer ─────────────────────────────────────────────────────

    private static string DrawAsciiChart(List<RateEntry> rates, int width = 50, int height = 10)
    {
        if (rates == null || rates.Count == 0) return "No data available.";
        var values = rates.Select(r => r.Rate).ToArray();
        double minVal = values.Min();
        double maxVal = values.Max();
        double range = maxVal - minVal;
        if (range == 0) return $"Rate is constant at {values[0]:F4}";

        int[] normalized = new int[values.Length];
        for (int i = 0; i < values.Length; i++)
            normalized[i] = (int)Math.Floor((values[i] - minVal) / range * (height - 1));

        var lines = new List<string>();
        for (int row = height - 1; row >= 0; row--)
        {
            var line = new char[normalized.Length];
            for (int i = 0; i < normalized.Length; i++)
            {
                if (normalized[i] >= row)
                {
                    if (i > 0 && normalized[i-1] >= row)
                        line[i] = '─';
                    else
                        line[i] = '┌';
                }
                else
                {
                    line[i] = ' ';
                }
            }
            string str = new string(line);
            if (!string.IsNullOrWhiteSpace(str))
                lines.Add(str);
        }
        // X axis
        int step = Math.Max(1, rates.Count / 10);
        var xAxis = new System.Text.StringBuilder(" ");
        int lastPos = 0;
        for (int i = 0; i < rates.Count; i += step)
        {
            string label = rates[i].Date.Substring(5, 5);
            int spaces = i - lastPos;
            if (spaces > 0) xAxis.Append(' ', spaces);
            xAxis.Append(label);
            lastPos = i;
        }
        if (lastPos < rates.Count - 1)
            xAxis.Append(' ', rates.Count - 1 - lastPos);
        var result = string.Join("\n", lines) + "\n" + xAxis.ToString();
        result += $"\nMin: {minVal:F4}  Max: {maxVal:F4}";
        return result;
    }

    // ─── App State ──────────────────────────────────────────────────────────

    private readonly Cache cache;
    private string baseCurrency, targetCurrency;
    private List<RateEntry> rates;

    public CurrencyTracker()
    {
        cache = new Cache();
        baseCurrency = DefaultBase;
        targetCurrency = DefaultTarget;
        rates = new List<RateEntry>();
        LoadCachedOrFetchAsync().GetAwaiter().GetResult();
    }

    private async Task LoadCachedOrFetchAsync()
    {
        var cached = cache.Get(baseCurrency, targetCurrency);
        if (cached != null && cached.Count > 0)
        {
            rates = cached;
            Console.WriteLine(C($"📂 Loaded cached data for {baseCurrency}/{targetCurrency}", Dim));
        }
        else
        {
            await FetchHistoryAsync();
        }
    }

    private async Task FetchHistoryAsync()
    {
        Console.Write(C("Fetching historical data...", Dim));
        try
        {
            var history = await GetHistoryAsync(baseCurrency, targetCurrency, HistoryDays);
            if (history.Count > 0)
            {
                rates = history;
                cache.Set(baseCurrency, targetCurrency, rates);
                Console.WriteLine(C($" ✅ Retrieved {rates.Count} data points", Green));
            }
            else
            {
                Console.WriteLine(C(" ⚠️  No historical data retrieved.", Yellow));
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine(C($" ❌ Error: {ex.Message}", Red));
        }
    }

    private async Task<double> GetCurrentRateAsync()
    {
        try
        {
            return await GetLatestAsync(baseCurrency, targetCurrency);
        }
        catch
        {
            if (rates.Count > 0) return rates[rates.Count-1].Rate;
            return 0.0;
        }
    }

    private string Ask(string prompt)
    {
        Console.Write(prompt);
        return Console.ReadLine()?.Trim() ?? "";
    }

    private bool AskConfirm(string prompt)
    {
        string ans = Ask(prompt + " (yes/no): ").ToLower();
        return ans == "yes" || ans == "y";
    }

    private void ShowMenu()
    {
        double current = GetCurrentRateAsync().GetAwaiter().GetResult();
        string currentStr = current != 0.0 ? $"{current:F4}" : "N/A";
        Console.WriteLine("\n" + C(new string('═', 50), Cyan));
        Console.WriteLine(C("💱 CURRENCY TRACKER", Bright + Cyan));
        Console.WriteLine(C(new string('═', 50), Cyan));
        Console.WriteLine($"  Base: {baseCurrency}  Target: {targetCurrency}");
        Console.WriteLine($"  Current: {currentStr}");
        Console.WriteLine($"  Data points: {rates.Count}");
        Console.WriteLine(C(new string('═', 50), Cyan));
        Console.WriteLine("  1. 📈 Show chart");
        Console.WriteLine("  2. 💹 Show current rate");
        Console.WriteLine("  3. 🔄 Change currency pair");
        Console.WriteLine("  4. 📊 Show statistics");
        Console.WriteLine("  5. 🔄 Refresh data");
        Console.WriteLine("  6. 🗑️  Clear cache");
        Console.WriteLine("  0. 🚪 Exit");
        Console.WriteLine(C(new string('═', 50), Cyan));
    }

    private void ShowChart()
    {
        if (rates.Count == 0)
        {
            Console.WriteLine(C("No data available. Fetching...", Yellow));
            FetchHistoryAsync().GetAwaiter().GetResult();
        }
        if (rates.Count == 0)
        {
            Console.WriteLine(C("No data to display.", Red));
            return;
        }
        Console.WriteLine(C($"\n📈 Exchange Rate Chart (last {rates.Count} days)", Bright));
        Console.WriteLine(DrawAsciiChart(rates));
        Console.WriteLine(C($"\nLatest: {rates[rates.Count-1].Rate:F4}", Cyan));
    }

    private void ShowRate()
    {
        double rate = GetCurrentRateAsync().GetAwaiter().GetResult();
        if (rate != 0.0)
        {
            Console.WriteLine(C($"\n💹 Current {baseCurrency}/{targetCurrency}: {rate:F4}", Green));
        }
        else
        {
            Console.WriteLine(C("Could not fetch rate.", Red));
        }
    }

    private void ShowStats()
    {
        if (rates.Count == 0)
        {
            Console.WriteLine(C("No data available.", Yellow));
            return;
        }
        double sum = rates.Sum(r => r.Rate);
        double avg = sum / rates.Count;
        double minv = rates.Min(r => r.Rate);
        double maxv = rates.Max(r => r.Rate);
        double volatility = (maxv - minv) / avg * 100.0;
        double last = rates[rates.Count-1].Rate;
        Console.WriteLine("\n📊 STATISTICS");
        Console.WriteLine(C(new string('─', 30), Dim));
        Console.WriteLine($"  Period:      {rates.Count} days");
        Console.WriteLine($"  Average:     {avg:F4}");
        Console.WriteLine($"  Min:         {minv:F4}");
        Console.WriteLine($"  Max:         {maxv:F4}");
        Console.WriteLine($"  Volatility:  {volatility:F2}%");
        Console.WriteLine($"  Current:     {last:F4}");
    }

    private async Task ChangePairAsync()
    {
        string b = Ask($"Base currency (default {baseCurrency}): ");
        string t = Ask($"Target currency (default {targetCurrency}): ");
        string newBase = string.IsNullOrEmpty(b) ? baseCurrency : b.ToUpper();
        string newTarget = string.IsNullOrEmpty(t) ? targetCurrency : t.ToUpper();
        if (newBase == baseCurrency && newTarget == targetCurrency)
        {
            Console.WriteLine("Pair unchanged.");
            return;
        }
        baseCurrency = newBase;
        targetCurrency = newTarget;
        var cached = cache.Get(baseCurrency, targetCurrency);
        if (cached != null && cached.Count > 0)
        {
            rates = cached;
            Console.WriteLine(C($"📂 Loaded cached data for {baseCurrency}/{targetCurrency}", Dim));
        }
        else
        {
            await FetchHistoryAsync();
        }
    }

    private async Task RefreshAsync()
    {
        await FetchHistoryAsync();
        Console.WriteLine(C("✅ Data refreshed.", Green));
    }

    private void ClearCache()
    {
        if (!AskConfirm("🗑️  Delete all cached data?")) return;
        cache.Clear();
        rates.Clear();
        Console.WriteLine(C("Cache cleared.", Yellow));
    }

    public async Task RunAsync()
    {
        Console.Clear();
