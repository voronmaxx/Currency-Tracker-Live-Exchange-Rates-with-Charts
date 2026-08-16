# currency_tracker.ts
/**
 * 💱 Currency Tracker – Live Exchange Rates with ASCII Charts (TypeScript Edition)
 * Fully typed, live rates, historical charts, caching, colorful CLI
 */

import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import * as readline from 'readline';
import fetch from 'node-fetch';

// ─── Types ──────────────────────────────────────────────────────────────────

interface CacheData {
    base: string;
    target: string;
    rates: { date: string; rate: number }[];
    last_update: string;
}

interface RateEntry {
    date: string;
    rate: number;
}

// ─── Configuration ──────────────────────────────────────────────────────────

const CONFIG = {
    cacheDir: path.join(os.homedir(), '.currency_tracker'),
    cacheFile: 'cache.json',
    apiBase: 'https://api.exchangerate.host',
    defaultBase: 'USD',
    defaultTarget: 'EUR',
    historyDays: 30,
};

// ─── Colors ──────────────────────────────────────────────────────────────────

const colors = {
    reset: '\x1b[0m',
    bright: '\x1b[1m',
    dim: '\x1b[2m',
    red: '\x1b[31m',
    green: '\x1b[32m',
    yellow: '\x1b[33m',
    blue: '\x1b[34m',
    magenta: '\x1b[35m',
    cyan: '\x1b[36m',
};

const c = (str: string, color: string): string => `${color}${str}${colors.reset}`;

// ─── API Client ─────────────────────────────────────────────────────────────

class ExchangeRateAPI {
    static async getLatest(base: string, target: string): Promise<number | null> {
        const url = `${CONFIG.apiBase}/latest?base=${base}&symbols=${target}`;
        try {
            const resp = await fetch(url);
            const data: any = await resp.json();
            if (data.success) return data.rates[target] as number;
            return null;
        } catch (e: any) {
            console.error(c(`❌ API error: ${e.message}`, colors.red));
            return null;
        }
    }

    static async getHistory(base: string, target: string, days: number = CONFIG.historyDays): Promise<[string, number][]> {
        const end = new Date();
        const start = new Date();
        start.setDate(end.getDate() - days);
        const format = (d: Date) => d.toISOString().slice(0, 10);
        const url = `${CONFIG.apiBase}/timeseries?start_date=${format(start)}&end_date=${format(end)}&base=${base}&symbols=${target}`;
        try {
            const resp = await fetch(url);
            const data: any = await resp.json();
            if (data.success) {
                const rates = data.rates as Record<string, Record<string, number>>;
                const entries = Object.entries(rates)
                    .sort((a, b) => a[0].localeCompare(b[0]))
                    .map(([date, ratesObj]) => [date, ratesObj[target]] as [string, number]);
                return entries;
            }
            return [];
        } catch (e: any) {
            console.error(c(`❌ API error: ${e.message}`, colors.red));
            return [];
        }
    }
}

// ─── Cache Manager ──────────────────────────────────────────────────────────

class Cache {
    private filePath: string;
    private data: CacheData;

    constructor() {
        if (!fs.existsSync(CONFIG.cacheDir)) fs.mkdirSync(CONFIG.cacheDir, { recursive: true });
        this.filePath = path.join(CONFIG.cacheDir, CONFIG.cacheFile);
        this.data = this._load();
    }

    private _load(): CacheData {
        if (fs.existsSync(this.filePath)) {
            try {
                return JSON.parse(fs.readFileSync(this.filePath, 'utf8'));
            } catch (_) { /* ignore */ }
        }
        return { base: CONFIG.defaultBase, target: CONFIG.defaultTarget, rates: [], last_update: '' };
    }

    save(): void {
        fs.writeFileSync(this.filePath, JSON.stringify(this.data, null, 2));
    }

    get(base: string, target: string): [string, number][] | null {
        if (this.data.base === base && this.data.target === target) {
            return this.data.rates.map(r => [r.date, r.rate]);
        }
        return null;
    }

    set(base: string, target: string, rates: [string, number][]): void {
        this.data.base = base;
        this.data.target = target;
        this.data.rates = rates.map(([date, rate]) => ({ date, rate }));
        this.data.last_update = new Date().toISOString();
        this.save();
    }

    clear(): void {
        this.data = { base: CONFIG.defaultBase, target: CONFIG.defaultTarget, rates: [], last_update: '' };
        this.save();
    }
}

// ─── Chart Renderer ─────────────────────────────────────────────────────────

function drawAsciiChart(rates: [string, number][], width: number = 50, height: number = 10): string {
    if (!rates || rates.length === 0) return 'No data available.';

    const values = rates.map(([_, r]) => r);
    const minVal = Math.min(...values);
    const maxVal = Math.max(...values);
    const range = maxVal - minVal;
    if (range === 0) return `Rate is constant at ${values[0].toFixed(4)}`;

    const normalized = values.map(v => Math.floor((v - minVal) / range * (height - 1)));
    const lines: string[] = [];
    for (let row = height - 1; row >= 0; row--) {
        let line = '';
        for (let i = 0; i < normalized.length; i++) {
            if (normalized[i] >= row) {
                if (i > 0 && normalized[i-1] >= row) line += '─';
                else line += '┌';
            } else {
                line += ' ';
            }
        }
        if (line.trim() !== '') lines.push(line);
    }
    const step = Math.max(1, Math.floor(rates.length / 10));
    let xAxis = ' ';
    let lastPos = 0;
    for (let i = 0; i < rates.length; i += step) {
        const label = rates[i][0].slice(5, 10);
        xAxis += ' '.repeat(i - lastPos) + label;
        lastPos = i;
    }
    if (lastPos < rates.length - 1) {
        xAxis += ' '.repeat(rates.length - 1 - lastPos);
    }
    return lines.join('\n') + '\n' + xAxis + `\nMin: ${minVal.toFixed(4)}  Max: ${maxVal.toFixed(4)}`;
}

// ─── Main App ──────────────────────────────────────────────────────────────

class CurrencyTracker {
    private rl: readline.Interface;
    private cache: Cache;
    private base: string;
    private target: string;
    private rates: [string, number][];

    constructor() {
        this.rl = readline.createInterface({ input: process.stdin, output: process.stdout });
        this.cache = new Cache();
        this.base = CONFIG.defaultBase;
        this.target = CONFIG.defaultTarget;
        this.rates = [];
        this._loadCachedOrFetch();
    }

    private async _loadCachedOrFetch(): Promise<void> {
        const cached = this.cache.get(this.base, this.target);
        if (cached) {
            this.rates = cached;
            console.log(c(`📂 Loaded cached data for ${this.base}/${this.target}`, colors.dim));
        } else {
            await this._fetchHistory();
        }
    }

    private async _fetchHistory(): Promise<void> {
        process.stdout.write(c('Fetching historical data...', colors.dim));
        this.rates = await ExchangeRateAPI.getHistory(this.base, this.target);
        if (this.rates && this.rates.length) {
            this.cache.set(this.base, this.target, this.rates);
            console.log(c(` ✅ Retrieved ${this.rates.length} data points`, colors.green));
        } else {
            console.log(c(' ⚠️  No historical data retrieved. Using cache if available.', colors.yellow));
        }
    }

    private async _getCurrentRate(): Promise<number | null> {
        const rate = await ExchangeRateAPI.getLatest(this.base, this.target);
        if (rate !== null) return rate;
        if (this.rates.length) return this.rates[this.rates.length-1][1];
        return null;
    }

    private _ask(prompt: string): Promise<string> {
        return new Promise(resolve => this.rl.question(prompt, resolve));
    }

    private async _askConfirm(prompt: string): Promise<boolean> {
        const ans = await this._ask(prompt + ' (yes/no): ');
        return ans.trim().toLowerCase() === 'yes' || ans.trim().toLowerCase() === 'y';
    }

    private async showMenu(): Promise<void> {
        const current = await this._getCurrentRate() || 'N/A';
        console.log('\n' + c('═'.repeat(50), colors.cyan));
        console.log(c('💱 CURRENCY TRACKER', colors.bright + colors.cyan));
        console.log(c('═'.repeat(50), colors.cyan));
        console.log(`  Base: ${this.base}  Target: ${this.target}`);
        console.log(`  Current: ${typeof current === 'number' ? current.toFixed(4) : current}`);
        console.log(`  Data points: ${this.rates.length}`);
        console.log(c('═'.repeat(50), colors.cyan));
        console.log('  1. 📈 Show chart');
        console.log('  2. 💹 Show current rate');
        console.log('  3. 🔄 Change currency pair');
        console.log('  4. 📊 Show statistics');
        console.log('  5. 🔄 Refresh data');
        console.log('  6. 🗑️  Clear cache');
        console.log('  0. 🚪 Exit');
        console.log(c('═'.repeat(50), colors.cyan));
    }

    private showChart(): void {
        if (!this.rates || this.rates.length === 0) {
            console.log(c('No data available. Fetching...', colors.yellow));
            return;
        }
        console.log(c(`\n📈 Exchange Rate Chart (last ${this.rates.length} days)`, colors.bright));
        console.log(drawAsciiChart(this.rates));
        console.log(c(`\nLatest: ${this.rates[this.rates.length-1][1].toFixed(4)}`, colors.cyan));
    }

    private async showRate(): Promise<void> {
        const rate = await this._getCurrentRate();
        if (rate !== null) {
            console.log(c(`\n💹 Current ${this.base}/${this.target}: ${rate.toFixed(4)}`, colors.green));
        } else {
            console.log(c('Could not fetch rate.', colors.red));
        }
    }

    private showStats(): void {
        if (!this.rates || this.rates.length === 0) {
            console.log(c('No data available.', colors.yellow));
            return;
        }
        const values = this.rates.map(([_, r]) => r);
        const avg = values.reduce((a, b) => a + b, 0) / values.length;
        const minv = Math.min(...values);
        const maxv = Math.max(...values);
        const volatility = (maxv - minv) / avg * 100;
        const last = values[values.length-1];
        console.log('\n📊 STATISTICS');
        console.log(c('─'.repeat(30), colors.dim));
        console.log(`  Period:      ${this.rates.length} days`);
        console.log(`  Average:     ${avg.toFixed(4)}`);
        console.log(`  Min:         ${minv.toFixed(4)}`);
        console.log(`  Max:         ${maxv.toFixed(4)}`);
        console.log(`  Volatility:  ${volatility.toFixed(2)}%`);
        console.log(`  Current:     ${last.toFixed(4)}`);
    }

    private async changePair(): Promise<void> {
        const base = await this._ask(`Base currency (default ${this.base}): `);
        const target = await this._ask(`Target currency (default ${this.target}): `);
        const newBase = base.trim().toUpperCase() || this.base;
        const newTarget = target.trim().toUpperCase() || this.target;
        if (newBase === this.base && newTarget === this.target) {
            console.log('Pair unchanged.');
            return;
        }
        this.base = newBase;
        this.target = newTarget;
        const cached = this.cache.get(this.base, this.target);
        if (cached) {
            this.rates = cached;
            console.log(c(`📂 Loaded cached data for ${this.base}/${this.target}`, colors.dim));
        } else {
            await this._fetchHistory();
        }
    }

    private async refresh(): Promise<void> {
        await this._fetchHistory();
        console.log(c('✅ Data refreshed.', colors.green));
    }

    private async clearCache(): Promise<void> {
        const ok = await this._askConfirm('🗑️  Delete all cached data?');
        if (!ok) return;
        this.cache.clear();
        this.rates = [];
        console.log(c('Cache cleared.', colors.yellow));
    }

    async run(): Promise<void> {
        console.clear();
        console.log(c('\n💱 Currency Tracker – Live Exchange Rates with Charts', colors.bright + colors.cyan));
        console.log(c('Track the pulse of the global economy.', colors.dim));

        while (true) {
            await this.showMenu();
            const choice = await this._ask('Your choice: ');
            switch (choice.trim()) {
                case '1': this.showChart(); break;
                case '2': await this.showRate(); break;
                case '3': await this.changePair(); break;
                case '4': this.showStats(); break;
                case '5': await this.refresh(); break;
                case '6': await this.clearCache(); break;
                case '0':
                    console.log(c('👋 Goodbye!', colors.cyan));
                    this.rl.close();
                    return;
                default:
                    console.log(c('❌ Invalid choice.', colors.red));
            }
            if (choice !== '0') {
                console.log('\nPress Enter to continue...');
                await this._ask('');
            }
        }
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────

const main = async (): Promise<void> => {
    try {
        const app = new CurrencyTracker();
        await app.run();
    } catch (e: any) {
        console.error(c(`❌ Unexpected error: ${e.message}`, colors.red));
        process.exit(1);
    }
};

main();
