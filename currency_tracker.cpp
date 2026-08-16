# currency_tracker.cpp
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (C++ Edition)
 * Advanced: live rates, historical charts, caching, colored output, stats
 * Requires: libcurl, nlohmann/json (header-only)
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─── Configuration ──────────────────────────────────────────────────────────

const std::string DEFAULT_BASE = "USD";
const std::string DEFAULT_TARGET = "EUR";
const int HISTORY_DAYS = 30;
const std::string API_BASE = "https://api.exchangerate.host";

// ─── Colors ──────────────────────────────────────────────────────────────────

#ifdef _WIN32
#include <windows.h>
HANDLE hConsole;
void setColor(int color) { SetConsoleTextAttribute(hConsole, color); }
#define RESET_COLOR setColor(7)
#define COLOR_RED setColor(12)
#define COLOR_GREEN setColor(10)
#define COLOR_YELLOW setColor(14)
#define COLOR_BLUE setColor(9)
#define COLOR_MAGENTA setColor(13)
#define COLOR_CYAN setColor(11)
#define COLOR_BRIGHT setColor(15)
#define COLOR_DIM setColor(8)
#else
#define RESET_COLOR std::cout << "\x1b[0m"
#define COLOR_RED std::cout << "\x1b[31m"
#define COLOR_GREEN std::cout << "\x1b[32m"
#define COLOR_YELLOW std::cout << "\x1b[33m"
#define COLOR_BLUE std::cout << "\x1b[34m"
#define COLOR_MAGENTA std::cout << "\x1b[35m"
#define COLOR_CYAN std::cout << "\x1b[36m"
#define COLOR_BRIGHT std::cout << "\x1b[1m"
#define COLOR_DIM std::cout << "\x1b[2m"
#endif

#define C(str, color) color << str << RESET_COLOR

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string get_today_str() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

std::string get_home_dir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? std::string(h) : ".";
}

// ─── API Client ─────────────────────────────────────────────────────────────

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append((char*)contents, total);
    return total;
}

class ExchangeRateAPI {
public:
    static double get_latest(const std::string& base, const std::string& target) {
        std::string url = API_BASE + "/latest?base=" + base + "&symbols=" + target;
        std::string response = http_get(url);
        if (response.empty()) return 0.0;
        try {
            json data = json::parse(response);
            if (data["success"].get<bool>()) {
                double rate = data["rates"][target].get<double>();
                return rate;
            }
        } catch (...) {}
        return 0.0;
    }

    static std::vector<std::pair<std::string, double>> get_history(const std::string& base, const std::string& target, int days) {
        auto end = std::time(nullptr);
        auto start = end - days * 86400;
        auto tm_start = *std::localtime(&start);
        auto tm_end = *std::localtime(&end);
        std::ostringstream oss_start, oss_end;
        oss_start << std::put_time(&tm_start, "%Y-%m-%d");
        oss_end << std::put_time(&tm_end, "%Y-%m-%d");
        std::string url = API_BASE + "/timeseries?start_date=" + oss_start.str() + "&end_date=" + oss_end.str() + "&base=" + base + "&symbols=" + target;
        std::string response = http_get(url);
        std::vector<std::pair<std::string, double>> result;
        if (response.empty()) return result;
        try {
            json data = json::parse(response);
            if (data["success"].get<bool>()) {
                json rates = data["rates"];
                for (auto& [date, rate_map] : rates.items()) {
                    double rate = rate_map[target].get<double>();
                    result.push_back({date, rate});
                }
                std::sort(result.begin(), result.end());
            }
        } catch (...) {}
        return result;
    }

private:
    static std::string http_get(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) return "";
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) return "";
        return response;
    }
};

// ─── Cache Manager ─────────────────────────────────────────────────────────

struct RateEntry {
    std::string date;
    double rate;
};

struct CacheData {
    std::string base = DEFAULT_BASE;
    std::string target = DEFAULT_TARGET;
    std::vector<RateEntry> rates;
    std::string last_update;
};

class Cache {
public:
    Cache() {
        std::string home = get_home_dir();
        cache_dir = home + "/.currency_tracker";
        std::filesystem::create_directories(cache_dir);
        cache_file = cache_dir + "/cache.json";
        load();
    }

    void load() {
        std::ifstream file(cache_file);
        if (!file.is_open()) {
            data = CacheData();
            return;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        try {
            json j = json::parse(buffer.str());
            data.base = j.value("base", DEFAULT_BASE);
            data.target = j.value("target", DEFAULT_TARGET);
            data.last_update = j.value("last_update", "");
            data.rates.clear();
            if (j.contains("rates")) {
                for (auto& item : j["rates"]) {
                    RateEntry entry;
                    entry.date = item.value("date", "");
                    entry.rate = item.value("rate", 0.0);
                    data.rates.push_back(entry);
                }
            }
        } catch (...) {
            data = CacheData();
        }
    }

    void save() {
        json j;
        j["base"] = data.base;
        j["target"] = data.target;
        j["last_update"] = data.last_update;
        j["rates"] = json::array();
        for (const auto& r : data.rates) {
            json item;
            item["date"] = r.date;
            item["rate"] = r.rate;
            j["rates"].push_back(item);
        }
        std::ofstream file(cache_file);
        if (file.is_open()) {
            file << j.dump(2);
            file.close();
        }
    }

    std::vector<RateEntry> get(const std::string& base, const std::string& target) {
        if (data.base == base && data.target == target) {
            return data.rates;
        }
        return {};
    }

    void set(const std::string& base, const std::string& target, const std::vector<RateEntry>& rates) {
        data.base = base;
        data.target = target;
        data.rates = rates;
        data.last_update = get_today_str();
        save();
    }

    void clear() {
        data = CacheData();
        save();
    }

    CacheData data;

private:
    std::string cache_dir, cache_file;
};

// ─── Chart Renderer ─────────────────────────────────────────────────────────

std::string draw_ascii_chart(const std::vector<RateEntry>& rates, int width = 50, int height = 10) {
    if (rates.empty()) return "No data available.";
    std::vector<double> values;
    for (const auto& r : rates) values.push_back(r.rate);
    double min_val = *std::min_element(values.begin(), values.end());
    double max_val = *std::max_element(values.begin(), values.end());
    double range = max_val - min_val;
    if (range == 0.0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Rate is constant at %.4f", values[0]);
        return std::string(buf);
    }
    std::vector<int> normalized;
    for (double v : values) {
        normalized.push_back(static_cast<int>(std::floor((v - min_val) / range * (height - 1))));
    }
    std::vector<std::string> lines;
    for (int row = height - 1; row >= 0; --row) {
        std::string line;
        for (size_t i = 0; i < normalized.size(); ++i) {
            if (normalized[i] >= row) {
                if (i > 0 && normalized[i-1] >= row) line += "─";
                else line += "┌";
            } else {
                line += " ";
            }
        }
        if (!line.empty() && line.find_first_not_of(' ') != std::string::npos) {
            lines.push_back(line);
        }
    }
    // X axis
    int step = std::max(1, (int)rates.size() / 10);
    std::string x_axis = " ";
    int last_pos = 0;
    for (size_t i = 0; i < rates.size(); i += step) {
        std::string label = rates[i].date.substr(5, 5); // MM-DD
        int spaces = i - last_pos;
        if (spaces > 0) x_axis += std::string(spaces, ' ');
        x_axis += label;
        last_pos = i;
    }
    if (last_pos < (int)rates.size() - 1) {
        x_axis += std::string(rates.size() - 1 - last_pos, ' ');
    }
    std::string result;
    for (const auto& line : lines) result += line + "\n";
    result += x_axis + "\n";
    char buf[64];
    snprintf(buf, sizeof(buf), "Min: %.4f  Max: %.4f", min_val, max_val);
    result += buf;
    return result;
}

// ─── Main App ──────────────────────────────────────────────────────────────

class CurrencyTracker {
public:
    CurrencyTracker() : base(DEFAULT_BASE), target(DEFAULT_TARGET) {
        cache = new Cache();
        load_cached_or_fetch();
    }

    ~CurrencyTracker() { delete cache; }

    void run() {
        std::cout << "\033[2J\033[1;1H";
        std::cout << C("\n💱 Currency Tracker – Live Exchange Rates with Charts", COLOR_BRIGHT) << C("", COLOR_CYAN) << std::endl;
        std::cout << C("Track the pulse of the global economy.", COLOR_DIM) << std::endl;

        while (true) {
            show_menu();
            std::string choice = ask("Your choice: ");
            if (choice == "1") show_chart();
            else if (choice == "2") show_rate();
            else if (choice == "3") change_pair();
            else if (choice == "4") show_stats();
            else if (choice == "5") refresh();
            else if (choice == "6") clear_cache();
            else if (choice == "0") {
                std::cout << C("👋 Goodbye!", COLOR_CYAN) << std::endl;
                break;
            } else {
                std::cout << C("❌ Invalid choice.", COLOR_RED) << std::endl;
            }
            if (choice != "0") {
                std::cout << "\nPress Enter to continue...";
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                std::cin.get();
            }
        }
    }

private:
    Cache* cache;
    std::string base, target;
    std::vector<RateEntry> rates;

    void load_cached_or_fetch() {
        auto cached = cache->get(base, target);
        if (!cached.empty()) {
            rates = cached;
            std::cout << C("📂 Loaded cached data for " + base + "/" + target, COLOR_DIM) << std::endl;
        } else {
            fetch_history();
        }
    }

    void fetch_history() {
        std::cout << C("Fetching historical data...", COLOR_DIM) << std::flush;
        auto history = ExchangeRateAPI::get_history(base, target, HISTORY_DAYS);
        if (!history.empty()) {
            rates.clear();
            for (auto& [date, rate] : history) {
                rates.push_back({date, rate});
            }
            cache->set(base, target, rates);
            std::cout << C(" ✅ Retrieved " + std::to_string(rates.size()) + " data points", COLOR_GREEN) << std::endl;
        } else {
            std::cout << C(" ⚠️  No historical data retrieved.", COLOR_YELLOW) << std::endl;
        }
    }

    double get_current_rate() {
        double rate = ExchangeRateAPI::get_latest(base, target);
        if (rate != 0.0) return rate;
        if (!rates.empty()) return rates.back().rate;
        return 0.0;
    }

    std::string ask(const std::string& prompt) {
        std::cout << prompt;
        std::string line;
        std::getline(std::cin, line);
        return trim(line);
    }

    bool ask_confirm(const std::string& prompt) {
        std::string ans = ask(prompt + " (yes/no): ");
        std::string lower = ans;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "yes" || lower == "y";
    }

    void show_menu() {
        double current = get_current_rate();
        std::string current_str = (current != 0.0) ? std::to_string(current).substr(0, 6) : "N/A";
        std::cout << "\n" << C(std::string(50, '═'), COLOR_CYAN) << std::endl;
        std::cout << C("💱 CURRENCY TRACKER", COLOR_BRIGHT) << C("", COLOR_CYAN) << std::endl;
        std::cout << C(std::string(50, '═'), COLOR_CYAN) << std::endl;
        std::cout << "  Base: " << base << "  Target: " << target << std::endl;
        std::cout << "  Current: " << current_str << std::endl;
        std::cout << "  Data points: " << rates.size() << std::endl;
        std::cout << C(std::string(50, '═'), COLOR_CYAN) << std::endl;
        std::cout << "  1. 📈 Show chart" << std::endl;
        std::cout << "  2. 💹 Show current rate" << std::endl;
        std::cout << "  3. 🔄 Change currency pair" << std::endl;
        std::cout << "  4. 📊 Show statistics" << std::endl;
        std::cout << "  5. 🔄 Refresh data" << std::endl;
        std::cout << "  6. 🗑️  Clear cache" << std::endl;
        std::cout << "  0. 🚪 Exit" << std::endl;
        std::cout << C(std::string(50, '═'), COLOR_CYAN) << std::endl;
    }

    void show_chart() {
        if (rates.empty()) {
            std::cout << C("No data available. Fetching...", COLOR_YELLOW) << std::endl;
            fetch_history();
        }
        if (rates.empty()) {
            std::cout << C("No data to display.", COLOR_RED) << std::endl;
            return;
        }
        std::cout << C("\n📈 Exchange Rate Chart (last " + std::to_string(rates.size()) + " days)", COLOR_BRIGHT) << std::endl;
        std::cout << draw_ascii_chart(rates) << std::endl;
        std::cout << C("\nLatest: " + std::to_string(rates.back().rate), COLOR_CYAN) << std::endl;
    }

    void show_rate() {
        double rate = get_current_rate();
        if (rate != 0.0) {
            std::cout << C("\n💹 Current " + base + "/" + target + ": " + std::to_string(rate), COLOR_GREEN) << std::endl;
        } else {
            std::cout << C("Could not fetch rate.", COLOR_RED) << std::endl;
        }
    }

    void show_stats() {
        if (rates.empty()) {
            std::cout << C("No data available.", COLOR_YELLOW) << std::endl;
            return;
        }
        double sum = 0.0, minv = rates[0].rate, maxv = rates[0].rate;
        for (const auto& r : rates) {
            sum += r.rate;
            if (r.rate < minv) minv = r.rate;
            if (r.rate > maxv) maxv = r.rate;
        }
        double avg = sum / rates.size();
        double volatility = (maxv - minv) / avg * 100.0;
        double last = rates.back().rate;
        std::cout << "\n📊 STATISTICS" << std::endl;
        std::cout << C(std::string(30, '─'), COLOR_DIM) << std::endl;
        std::cout << "  Period:      " << rates.size() << " days" << std::endl;
        std::cout << "  Average:     " << std::fixed << std::setprecision(4) << avg << std::endl;
        std::cout << "  Min:         " << minv << std::endl;
        std::cout << "  Max:         " << maxv << std::endl;
        std::cout << "  Volatility:  " << volatility << "%" << std::endl;
        std::cout << "  Current:     " << last << std::endl;
    }

    void change_pair() {
        std::string b = ask("Base currency (default " + base + "): ");
        std::string t = ask("Target currency (default " + target + "): ");
        std::string new_base = b.empty() ? base : b;
        std::string new_target = t.empty() ? target : t;
        std::transform(new_base.begin(), new_base.end(), new_base.begin(), ::toupper);
        std::transform(new_target.begin(), new_target.end(), new_target.begin(), ::toupper);
        if (new_base == base && new_target == target) {
            std::cout << "Pair unchanged." << std::endl;
            return;
        }
        base = new_base;
        target = new_target;
        auto cached = cache->get(base, target);
        if (!cached.empty()) {
            rates = cached;
            std::cout << C("📂 Loaded cached data for " + base + "/" + target, COLOR_DIM) << std::endl;
        } else {
            fetch_history();
        }
    }

    void refresh() {
        fetch_history();
        std::cout << C("✅ Data refreshed.", COLOR_GREEN) << std::endl;
    }

    void clear_cache() {
        if (!ask_confirm("🗑️  Delete all cached data?")) return;
        cache->clear();
        rates.clear();
        std::cout << C("Cache cleared.", COLOR_YELLOW) << std::endl;
    }
};

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
#ifdef _WIN32
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    curl_global_init(CURL_GLOBAL_DEFAULT);
    try {
        CurrencyTracker app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << C("❌ Unexpected error: ", COLOR_RED) << e.what() << std::endl;
        curl_global_cleanup();
        return 1;
    }
    curl_global_cleanup();
    return 0;
}
