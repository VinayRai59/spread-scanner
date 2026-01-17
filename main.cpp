#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

/* ================= TELEGRAM ================= */

const string BOT_TOKEN = "8031357753:AAF75pKyoKQNPi_q6bw2PsV8aeoPqjAV4y8";
const string CHANNEL   = "@testing505050";

void send_telegram(const string& text)
{
    if (text.empty()) return;

    CURL* curl = curl_easy_init();
    if (!curl) return;

    char* esc = curl_easy_escape(curl, text.c_str(), text.length());
    if (!esc) { curl_easy_cleanup(curl); return; }

    string url =
        "https://api.telegram.org/bot" + BOT_TOKEN +
        "/sendMessage?chat_id=" + CHANNEL +
        "&text=" + esc;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_perform(curl);

    curl_free(esc);
    curl_easy_cleanup(curl);
}

/* ================= FORMAT ================= */

string fmt2(double v)
{
    ostringstream ss;
    ss << fixed << setprecision(2) << v;
    return ss.str();
}

/* ================= SPREAD STRUCT ================= */

struct SpreadRow
{
    string type;   // CE / PE
    int buy_strike;
    int sell_strike;
    double buy_prem;
    double sell_prem;
    double distance;
    double max_gain; // Net Credit
    double max_loss;
};

string format_spread(
    const SpreadRow& s,
    double spot,
    const string& expiry
)
{
    double breakeven;
    string expectation;

    if (s.type == "PE")
    {
        // ✅ PUT CREDIT SPREAD
        breakeven = s.sell_strike - s.max_gain;
        expectation = "📈 Expectations : Nifty will go UP";
    }
    else
    {
        // ✅ CALL CREDIT SPREAD
        breakeven = s.sell_strike + s.max_gain;
        expectation = "📉 Expectations : Nifty will go DOWN";
    }

    string msg;
    msg += "📅 Expire - " + expiry + "\n";
    msg += expectation + "\n";
    msg += (s.type == "PE" ? "🔴 PE " : "🟢 CE ");
    msg += "Pair  : " +
           to_string(s.buy_strike) + " (" + fmt2(s.buy_prem) + ") Buy  +  " +
           to_string(s.sell_strike) + " (" + fmt2(s.sell_prem) + ") Sell\n";
    msg += "💰 Max Gain : " + fmt2(s.max_gain) + "\n";
    msg += "🛑 Max Loss : " + fmt2(s.max_loss) + "\n";
    msg += "🎯 Breakeven : " + fmt2(breakeven) + "\n";
    msg += "📉 Distance : " + fmt2(s.distance) + "\n";
    msg += "📍 Spot     : " + fmt2(spot) + "\n";
    msg += "=================================\n";
    return msg;
}

/* ================= HTTP ================= */

size_t write_cb(void* c, size_t s, size_t n, string* out)
{
    out->append((char*)c, s * n);
    return s * n;
}

string read_token()
{
    ifstream f("access_token.txt");
    string t;
    getline(f, t);
    return t;
}

string http_get(const string& url)
{
    string res;
    CURL* c = curl_easy_init();
    if (!c) return "";

    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, ("Authorization: Bearer " + read_token()).c_str());
    h = curl_slist_append(h, "Accept: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);

    curl_easy_perform(c);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return res;
}

/* ================= CONFIG ================= */

struct Config
{
    string instrument;
    string expiry;
    int strike_step;
    int itm_depth;
    int sleep_sec;
};

Config load_config()
{
    ifstream f("config.json");
    json j; f >> j;

    return {
        j["instrument_token"],
        j["expiry"],
        j["strike_step"],
        j["itm_depth"],
        j["sleep_seconds"]
    };
}

/* ================= ENGINE ================= */

class SpreadScanner
{
    Config cfg;
    double spot = 0.0;
    map<int,double> ce, pe;

    int atm() const
    {
        return ((int)(spot / cfg.strike_step)) * cfg.strike_step;
    }

    double intrinsic_ce(int strike) const
    {
        return max(0.0, spot - strike);
    }

    double intrinsic_pe(int strike) const
    {
        return max(0.0, strike - spot);
    }

public:
    SpreadScanner(const Config& c): cfg(c) {}

    void run()
    {
        while (true)
        {
            ce.clear(); pe.clear(); spot = 0.0;

            CURL* c = curl_easy_init();
            char* esc = curl_easy_escape(c, cfg.instrument.c_str(), 0);

            string url =
                "https://api.upstox.com/v2/option/chain?instrument_key=" +
                string(esc ? esc : "") +
                "&expiry_date=" + cfg.expiry;

            if (esc) curl_free(esc);
            curl_easy_cleanup(c);

            auto j = json::parse(http_get(url), nullptr, false);
            if (j.is_discarded() || !j.contains("data"))
            {
                sleep(cfg.sleep_sec);
                continue;
            }

            for (auto& d : j["data"])
            {
                if (spot == 0 && d.contains("underlying_spot_price"))
                    spot = d["underlying_spot_price"];

                int strike = d["strike_price"];
                if (d.contains("call_options") && !d["call_options"].is_null())
                    ce[strike] = d["call_options"]["market_data"]["ltp"];
                if (d.contains("put_options") && !d["put_options"].is_null())
                    pe[strike] = d["put_options"]["market_data"]["ltp"];
            }

            if (spot != 0)
                write_output();

            sleep(cfg.sleep_sec);
        }
    }

    void write_output()
    {
        int atm_strike = atm();
        vector<int> buys = { atm_strike, atm_strike + cfg.strike_step };
        vector<SpreadRow> spreads;

        for (int buy : buys)
        {
            if (ce.count(buy))
            {
                double bp = ce[buy];
                for (int i = 1; i <= cfg.itm_depth; i++)
                {
                    int sell = buy - i * cfg.strike_step;
                    if (!ce.count(sell)) continue;

                    double sp = ce[sell];
                    double dist = (sp - intrinsic_ce(sell)) - bp;
                    double credit = sp - bp;
                    double max_loss = abs(buy - sell) - credit;

                    spreads.push_back({"CE", buy, sell, bp, sp, dist, credit, max_loss});
                }
            }

            if (pe.count(buy))
            {
                double bp = pe[buy];
                for (int i = 1; i <= cfg.itm_depth; i++)
                {
                    int sell = buy + i * cfg.strike_step;
                    if (!pe.count(sell)) continue;

                    double sp = pe[sell];
                    double dist = (sp - intrinsic_pe(sell)) - bp;
                    double credit = sp - bp;
                    double max_loss = abs(buy - sell) - credit;

                    spreads.push_back({"PE", buy, sell, bp, sp, dist, credit, max_loss});
                }
            }
        }

        sort(spreads.begin(), spreads.end(),
             [](const SpreadRow& a, const SpreadRow& b)
             {
                 return a.distance > b.distance;
             });

        const size_t TG_LIMIT = 3500;
        string tg;

        for (const auto& s : spreads)
        {
            tg += format_spread(s, spot, cfg.expiry);
            if (tg.size() > TG_LIMIT)
            {
                send_telegram(tg);
                tg.clear();
            }
        }

        if (!tg.empty())
            send_telegram(tg);
    }
};

/* ================= MAIN ================= */

int main()
{
    curl_global_init(CURL_GLOBAL_ALL);
    SpreadScanner engine(load_config());
    engine.run();
    curl_global_cleanup();
    return 0;
}