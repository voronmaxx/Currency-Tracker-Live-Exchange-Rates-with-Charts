# CurrencyTracker.java
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (Java Edition)
 * Advanced: live rates, historical charts, caching, colorful CLI, stats
 * Requires: Java 17+ (uses java.net.http and java.time)
 */

import java.io.*;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.*;
import java.time.LocalDate;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.stream.Collectors;

// ─── Data Classes ──────────────────────────────────────────────────────────

class RateEntry {
    public String date;
    public double rate;
    public RateEntry(String date, double rate) { this.date = date; this.rate = rate; }
}

class CacheData {
    public String base = "USD";
    public String target = "EUR";
    public List<RateEntry> rates = new ArrayList<>();
    public String lastUpdate = "";
}

// ─── Main App ──────────────────────────────────────────────────────────────

public class CurrencyTracker {
    // ─── Configuration ──────────────────────────────────────────────────────

    private static final String DEFAULT_BASE = "USD";
    private static final String DEFAULT_TARGET = "EUR";
    private static final int HISTORY_DAYS = 30;
    private static final String API_BASE = "https://api.exchangerate.host";

    // ─── Colors ─────────────────────────────────────────────────────────────

    private static final String RESET = "\u001B[0m";
    private static final String BRIGHT = "\u001B[1m";
    private static final String DIM = "\u001B[2m";
    private static final String RED = "\u001B[31m";
    private static final String GREEN = "\u001B[32m";
    private static final String YELLOW = "\u001B[33m";
    private static final String BLUE = "\u001B[34m";
    private static final String MAGENTA = "\u001B[35m";
    private static final String CYAN = "\u001B[36m";

    private static String c(String text, String color) { return color + text + RESET; }

    // ─── API Client ─────────────────────────────────────────────────────────

    private static class ExchangeRateAPI {
        private static final HttpClient client = HttpClient.newHttpClient();

        static double getLatest(String base, String target) throws Exception {
            String url = API_BASE + "/latest?base=" + base + "&symbols=" + target;
            HttpRequest req = HttpRequest.newBuilder().uri(URI.create(url)).GET().build();
            HttpResponse<String> resp = client.send(req, HttpResponse.BodyHandlers.ofString());
            if (resp.statusCode() == 200) {
                var json = new com.google.gson.JsonParser().parse(resp.body()).getAsJsonObject();
                if (json.get("success").getAsBoolean()) {
                    return json.getAsJsonObject("rates").get(target).getAsDouble();
                }
            }
            throw new RuntimeException("Failed to get rate");
        }

        static List<RateEntry> getHistory(String base, String target, int days) throws Exception {
            LocalDate end = LocalDate.now();
            LocalDate start = end.minusDays(days);
            DateTimeFormatter fmt = DateTimeFormatter.ISO_LOCAL_DATE;
            String url = String.format("%s/timeseries?start_date=%s&end_date=%s&base=%s&symbols=%s",
                    API_BASE, start.format(fmt), end.format(fmt), base, target);
            HttpRequest req = HttpRequest.newBuilder().uri(URI.create(url)).GET().build();
            HttpResponse<String> resp = client.send(req, HttpResponse.BodyHandlers.ofString());
            if (resp.statusCode() == 200) {
                var json = new com.google.gson.JsonParser().parse(resp.body()).getAsJsonObject();
                if (json.get("success").getAsBoolean()) {
                    var ratesObj = json.getAsJsonObject("rates");
                    List<RateEntry> entries = new ArrayList<>();
                    for (var date : ratesObj.keySet()) {
                        double rate = ratesObj.getAsJsonObject(date).get(target).getAsDouble();
                        entries.add(new RateEntry(date, rate));
                    }
                    entries.sort(Comparator.comparing(a -> a.date));
                    return entries;
                }
            }
            throw new RuntimeException("Failed to get history");
        }
    }

    // ─── Cache Manager ─────────────────────────────────────────────────────

    private static class Cache {
        private final Path filePath;
        private CacheData data;

        Cache() throws IOException {
            String home = System.getProperty("user.home");
            Path dir = Paths.get(home, ".currency_tracker");
            Files.createDirectories(dir);
            filePath = dir.resolve("cache.json");
            load();
        }

        private void load() {
            data = new CacheData();
            if (Files.exists(filePath)) {
                try {
                    String json = new String(Files.readAllBytes(filePath));
                    var obj = new com.google.gson.JsonParser().parse(json).getAsJsonObject();
                    data.base = obj.get("base").getAsString();
                    data.target = obj.get("target").getAsString();
                    data.lastUpdate = obj.get("last_update").getAsString();
                    var ratesArr = obj.getAsJsonArray("rates");
                    for (var item : ratesArr) {
                        var obj2 = item.getAsJsonObject();
                        data.rates.add(new RateEntry(obj2.get("date").getAsString(), obj2.get("rate").getAsDouble()));
                    }
                } catch (Exception e) { data = new CacheData(); }
            }
        }

        void save() throws IOException {
            var obj = new com.google.gson.JsonObject();
            obj.addProperty("base", data.base);
            obj.addProperty("target", data.target);
            obj.addProperty("last_update", data.lastUpdate);
            var ratesArr = new com.google.gson.JsonArray();
            for (var r : data.rates) {
                var item = new com.google.gson.JsonObject();
                item.addProperty("date", r.date);
                item.addProperty("rate", r.rate);
                ratesArr.add(item);
            }
            obj.add("rates", ratesArr);
            Files.write(filePath, obj.toString().getBytes());
        }

        List<RateEntry> get(String base, String target) {
            if (data.base.equals(base) && data.target.equals(target)) {
                return data.rates;
            }
            return null;
        }

        void set(String base, String target, List<RateEntry> rates) throws IOException {
            data.base = base;
            data.target = target;
            data.rates = rates;
            data.lastUpdate = LocalDate.now().toString();
            save();
        }

        void clear() throws IOException {
            data = new CacheData();
            save();
        }
    }

    // ─── Chart Renderer ─────────────────────────────────────────────────────

    private static String drawAsciiChart(List<RateEntry> rates, int width, int height) {
        if (rates.isEmpty()) return "No data available.";
        double[] values = rates.stream().mapToDouble(r -> r.rate).toArray();
        double minVal = Arrays.stream(values).min().getAsDouble();
        double maxVal = Arrays.stream(values).max().getAsDouble();
        double range = maxVal - minVal;
        if (range == 0) return String.format("Rate is constant at %.4f", values[0]);
        int[] normalized = new int[values.length];
        for (int i = 0; i < values.length; i++) {
            normalized[i] = (int) Math.floor((values[i] - minVal) / range * (height - 1));
        }
        StringBuilder sb = new StringBuilder();
        for (int row = height - 1; row >= 0; row--) {
            StringBuilder line = new StringBuilder();
            for (int i = 0; i < normalized.length; i++) {
                if (normalized[i] >= row) {
                    if (i > 0 && normalized[i-1] >= row) line.append('─');
                    else line.append('┌');
                } else {
                    line.append(' ');
                }
            }
            if (line.toString().trim().length() > 0) sb.append(line).append('\n');
        }
        // X axis
        int step = Math.max(1, rates.size() / 10);
        StringBuilder xAxis = new StringBuilder(" ");
        int lastPos = 0;
        for (int i = 0; i < rates.size(); i += step) {
            String label = rates.get(i).date.substring(5, 10);
            int spaces = i - lastPos;
            if (spaces > 0) xAxis.append(" ".repeat(spaces));
            xAxis.append(label);
            lastPos = i;
        }
        if (lastPos < rates.size() - 1) {
            xAxis.append(" ".repeat(rates.size() - 1 - lastPos));
        }
        sb.append(xAxis).append('\n');
        sb.append(String.format("Min: %.4f  Max: %.4f", minVal, maxVal));
        return sb.toString();
    }

    // ─── Main App ──────────────────────────────────────────────────────────

    private final Scanner scanner;
    private final Cache cache;
    private String base, target;
    private List<RateEntry> rates;

    public CurrencyTracker() throws Exception {
        scanner = new Scanner(System.in);
        cache = new Cache();
        base = DEFAULT_BASE;
        target = DEFAULT_TARGET;
        rates = new ArrayList<>();
        loadCachedOrFetch();
    }

    private void loadCachedOrFetch() {
        List<RateEntry> cached = cache.get(base, target);
        if (cached != null && !cached.isEmpty()) {
            rates = cached;
            System.out.println(c("📂 Loaded cached data for " + base + "/" + target, DIM));
        } else {
            fetchHistory();
        }
    }

    private void fetchHistory() {
        System.out.print(c("Fetching historical data...", DIM));
        try {
            List<RateEntry> history = ExchangeRateAPI.getHistory(base, target, HISTORY_DAYS);
            if (!history.isEmpty()) {
                rates = history;
                cache.set(base, target, rates);
                System.out.println(c(" ✅ Retrieved " + rates.size() + " data points", GREEN));
            } else {
                System.out.println(c(" ⚠️  No historical data retrieved.", YELLOW));
            }
        } catch (Exception e) {
            System.out.println(c(" ❌ Error fetching data: " + e.getMessage(), RED));
        }
    }

    private double getCurrentRate() {
        try {
            return ExchangeRateAPI.getLatest(base, target);
        } catch (Exception e) {
            if (!rates.isEmpty()) return rates.get(rates.size()-1).rate;
            return 0.0;
        }
    }

    private String ask(String prompt) {
        System.out.print(prompt);
        return scanner.nextLine().trim();
    }

    private boolean askConfirm(String prompt) {
        String ans = ask(prompt + " (yes/no): ").toLowerCase();
        return ans.equals("yes") || ans.equals("y");
    }

    private void showMenu() {
        double current = getCurrentRate();
        String currentStr = current != 0.0 ? String.format("%.4f", current) : "N/A";
        System.out.println("\n" + c("═".repeat(50), CYAN));
        System.out.println(c("💱 CURRENCY TRACKER", BRIGHT + CYAN));
        System.out.println(c("═".repeat(50), CYAN));
        System.out.printf("  Base: %s  Target: %s\n", base, target);
        System.out.printf("  Current: %s\n", currentStr);
        System.out.printf("  Data points: %d\n", rates.size());
        System.out.println(c("═".repeat(50), CYAN));
        System.out.println("  1. 📈 Show chart");
        System.out.println("  2. 💹 Show current rate");
        System.out.println("  3. 🔄 Change currency pair");
        System.out.println("  4. 📊 Show statistics");
        System.out.println("  5. 🔄 Refresh data");
        System.out.println("  6. 🗑️  Clear cache");
        System.out.println("  0. 🚪 Exit");
        System.out.println(c("═".repeat(50), CYAN));
    }

    private void showChart() {
        if (rates.isEmpty()) {
            System.out.println(c("No data available. Fetching...", YELLOW));
            fetchHistory();
        }
        if (rates.isEmpty()) {
            System.out.println(c("No data to display.", RED));
            return;
        }
        System.out.println(c("\n📈 Exchange Rate Chart (last " + rates.size() + " days)", BRIGHT));
        System.out.println(drawAsciiChart(rates, 50, 10));
        System.out.println(c("\nLatest: " + String.format("%.4f", rates.get(rates.size()-1).rate), CYAN));
    }

    private void showRate() {
        double rate = getCurrentRate();
        if (rate != 0.0) {
            System.out.println(c("\n💹 Current " + base + "/" + target + ": " + String.format("%.4f", rate), GREEN));
        } else {
            System.out.println(c("Could not fetch rate.", RED));
        }
    }

    private void showStats() {
        if (rates.isEmpty()) {
            System.out.println(c("No data available.", YELLOW));
            return;
        }
        double sum = rates.stream().mapToDouble(r -> r.rate).sum();
        double avg = sum / rates.size();
        double minv = rates.stream().mapToDouble(r -> r.rate).min().getAsDouble();
        double maxv = rates.stream().mapToDouble(r -> r.rate).max().getAsDouble();
        double volatility = (maxv - minv) / avg * 100.0;
        double last = rates.get(rates.size()-1).rate;
        System.out.println("\n📊 STATISTICS");
        System.out.println(c("─".repeat(30), DIM));
        System.out.printf("  Period:      %d days\n", rates.size());
        System.out.printf("  Average:     %.4f\n", avg);
        System.out.printf("  Min:         %.4f\n", minv);
        System.out.printf("  Max:         %.4f\n", maxv);
        System.out.printf("  Volatility:  %.2f%%\n", volatility);
        System.out.printf("  Current:     %.4f\n", last);
    }

    private void changePair() {
        String b = ask("Base currency (default " + base + "): ");
        String t = ask("Target currency (default " + target + "): ");
        String newBase = b.isEmpty() ? base : b.toUpperCase();
        String newTarget = t.isEmpty() ? target : t.toUpperCase();
        if (newBase.equals(base) && newTarget.equals(target)) {
            System.out.println("Pair unchanged.");
            return;
        }
        base = newBase;
        target = newTarget;
        List<RateEntry> cached = cache.get(base, target);
        if (cached != null && !cached.isEmpty()) {
            rates = cached;
            System.out.println(c("📂 Loaded cached data for " + base + "/" + target, DIM));
        } else {
            fetchHistory();
        }
    }

    private void refresh() {
        fetchHistory();
        System.out.println(c("✅ Data refreshed.", GREEN));
    }

    private void clearCache() {
        if (!askConfirm("🗑️  Delete all cached data?")) return;
        try {
            cache.clear();
            rates.clear();
            System.out.println(c("Cache cleared.", YELLOW));
        } catch (Exception e) {
            System.out.println(c("❌ Error clearing cache: " + e.getMessage(), RED));
        }
    }

    public void run() {
        System.out.print("\033[H\033[2J");
        System.out.flush();
        System.out.println(c("\n💱 Currency Tracker – Live Exchange Rates with Charts", BRIGHT + CYAN));
        System.out.println(c("Track the pulse of the global economy.", DIM));

        while (true) {
            showMenu();
            String choice = ask("Your choice: ");
            switch (choice) {
                case "1": showChart(); break;
                case "2": showRate(); break;
                case "3": changePair(); break;
                case "4": showStats(); break;
                case "5": refresh(); break;
                case "6": clearCache(); break;
                case "0":
                    System.out.println(c("👋 Goodbye!", CYAN));
                    return;
                default:
                    System.out.println(c("❌ Invalid choice.", RED));
            }
            if (!choice.equals("0")) {
                System.out.print("\nPress Enter to continue...");
                scanner.nextLine();
            }
        }
    }

    // ─── Main ──────────────────────────────────────────────────────────────

    public static void main(String[] args) {
        try {
            // Gson is needed for JSON parsing; include in classpath
            // For simplicity, we use a minimal JSON parser or require Gson library.
            // In a real project, add dependency on Gson.
            // We'll use a small hack: we assume Gson is available.
            // If not, we could implement a simple parser, but for brevity we add a comment.
            // This code uses Gson; please add the library.
            // To compile: javac -cp gson.jar CurrencyTracker.java
            // To run: java -cp .:gson.jar CurrencyTracker
            CurrencyTracker app = new CurrencyTracker();
            app.run();
        } catch (Exception e) {
            System.err.println(c("❌ Unexpected error: " + e.getMessage(), RED));
            e.printStackTrace();
            System.exit(1);
        }
    }
}
