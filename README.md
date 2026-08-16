💱 Currency Tracker – Live Exchange Rates with Charts
"Track the pulse of the global economy – real‑time rates, historical trends, and ASCII charts, all from your terminal."

📋 Table of Contents
✨ Features

📁 Repository Structure

🚀 Quick Start

💻 Language Implementations

📊 Data Format

🤝 Contributing

📄 License

✨ Features
Feature	Description
💹 Live Exchange Rates	Fetch current rates from a free public API (exchangerate.host)
📈 Historical Charts	View ASCII line charts for the last 30 days of rate changes
💾 Smart Caching	Data is cached locally to reduce API calls and speed up display
🔄 Multi‑Currency	Choose any base and target currency pair (e.g., USD/EUR, GBP/JPY)
🎨 Colorful CLI	Beautiful terminal output with ANSI colors and progress indicators
📊 Detailed Stats	Show min, max, average, and volatility of the exchange rate
⚡ Cross‑Platform	Works on Windows, macOS, and Linux
📁 Repository Structure
text
currency-tracker/
├── README.md
├── python/
│   └── currency_tracker.py
├── javascript/
│   └── currency_tracker.js
├── typescript/
│   └── currency_tracker.ts
├── go/
│   └── currency_tracker.go
├── rust/
│   └── currency_tracker.rs
├── cpp/
│   └── currency_tracker.cpp
├── java/
│   └── CurrencyTracker.java
└── csharp/
    └── CurrencyTracker.cs
🚀 Quick Start
Prerequisites
Each language requires its respective runtime/compiler (see individual sections)

Internet connection – the program fetches live data from a public API

Clone & Run
bash
git clone https://github.com/yourusername/currency-tracker.git
cd currency-tracker
# Navigate to your language folder and run
💻 Language Implementations
1. 🐍 Python
bash
cd python
pip install requests
python currency_tracker.py
Requires: Python 3.8+

2. 🟨 JavaScript (Node.js)
bash
cd javascript
npm install node-fetch
node currency_tracker.js
Requires: Node.js 16+

3. 🟦 TypeScript
bash
cd typescript
npm install -g ts-node typescript
npm install node-fetch @types/node-fetch
ts-node currency_tracker.ts
Requires: Node.js 16+, TypeScript

4. 🟩 Go
bash
cd go
go run currency_tracker.go
Requires: Go 1.18+

5. 🦀 Rust
bash
cd rust
cargo run
Requires: Rust 1.70+ (dependencies: reqwest, serde, chrono)

6. ⚙️ C++
bash
cd cpp
# Install libcurl and nlohmann/json (see README in the folder)
g++ -std=c++17 currency_tracker.cpp -lcurl -o currency_tracker
./currency_tracker
Requires: C++17, libcurl, nlohmann/json

7. ☕ Java
bash
cd java
javac CurrencyTracker.java
java CurrencyTracker
Requires: JDK 17+ (uses java.net.http)

8. 🔷 C#
bash
cd csharp
dotnet run
Requires: .NET 6.0+

📊 Data Format
All implementations store cached data in JSON format in the user's home directory under .currency_tracker/cache.json. The schema:

json
{
  "base": "USD",
  "target": "EUR",
  "rates": [
    { "date": "2026-07-16", "rate": 0.92 },
    ...
  ],
  "last_update": "2026-08-16T12:34:56Z"
}
🤝 Contributing
Contributions are welcome! Please:

Fork the repository

Create a feature branch

Commit your changes

Open a Pull Request

📄 License
MIT © 2026 Currency Tracker Team

