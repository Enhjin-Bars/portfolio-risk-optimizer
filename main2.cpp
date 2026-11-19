#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>




struct StockRecord {
    double open;
    double high;
    double low;
    double close;
    long long volume;
};



std::vector<StockRecord> load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }

    std::vector<StockRecord> data;
    std::string line;

    // Skip header
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;

        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }

        if (fields.size() < 6) {
            continue;
        }

        StockRecord record;
        record.open = std::stod(fields[1]);
        record.high = std::stod(fields[2]);
        record.low = std::stod(fields[3]);
        record.close = std::stod(fields[4]);
        record.volume = std::stoll(fields[5]);

        data.push_back(record);
    }

    return data;
}

void report_close_diagnostics(const std::vector<StockRecord>& data) {
    if (data.empty()) {
        std::cout << "No rows loaded.\n";
        return;
    }

    double min_close = std::numeric_limits<double>::infinity();
    double max_close = -std::numeric_limits<double>::infinity();
    std::size_t max_index = 0;
    std::size_t non_finite = 0;
    std::size_t non_positive = 0;
    long double sum = 0.0L;
    std::size_t valid_count = 0;

    for (std::size_t i = 0; i < data.size(); ++i) {
        const double close = data[i].close;

        if (!std::isfinite(close)) {
            ++non_finite;
            continue;
        }

        if (close <= 0.0) {
            ++non_positive;
        }

        if (close < min_close) min_close = close;
        if (close > max_close) {
            max_close = close;
            max_index = i;
        }

        sum += static_cast<long double>(close);
        ++valid_count;
    }

    std::cout << std::scientific << std::setprecision(8)
              << "Minimum close: " << min_close << '\n'
              << "Maximum close: " << max_close << " at row index " << max_index << '\n'
              << "Last close: " << data.back().close << '\n'
              << "Mean close: "
              << (valid_count ? sum / valid_count : 0.0L) << '\n'
              << "Non-finite closes: " << non_finite << '\n'
              << "Non-positive closes: " << non_positive << '\n';
}


double sum_close_prices(const std::vector<StockRecord>& data) {
    double total = 0.0;

    const StockRecord* ptr = data.data();
    const StockRecord* end = ptr + data.size();

    while (ptr < end) {
        total += ptr->close;
        ++ptr;
    }

    return total;
}

std::vector<double> compute_daily_returns(const std::vector<StockRecord>& data) {
    if (data.size() < 2) {
    return {};
    }


    std::vector<double> returns;
    returns.reserve(data.size() - 1);

    const StockRecord* ptr = data.data();
    const StockRecord* end = ptr + data.size() - 1;

    while (ptr < end) {
        double prev = ptr->close;
        double curr = (ptr + 1)->close;
        returns.push_back((curr - prev) / prev);
        ++ptr;
    }

    return returns;
}

std::vector<double> moving_average(const std::vector<StockRecord>& data, int window) {
    std::vector<double> result;
    result.reserve(data.size());

    for (std::size_t i = 0; i < data.size(); ++i) {
        int start = static_cast<int>(i) - window + 1;
        if (start < 0) start = 0;

        double total = 0.0;
        int count = 0;

        const StockRecord* ptr = data.data() + start;
        const StockRecord* end = data.data() + i + 1;

        while (ptr < end) {
            total += ptr->close;
            ++ptr;
            ++count;
        }

        result.push_back(total / count);
    }

    return result;
}


//  1 = Bull, -1 = Bear, 0 = Neutral
std::vector<int> compute_threshold_regimes(const std::vector<StockRecord>& data, double threshold = 0.05) {
    std::vector<int> regimes;
    regimes.reserve(data.size());

    for (int i = 0; i < 20 && i < data.size(); ++i) {
        regimes.push_back(0); 
    }

    const StockRecord* ptr = data.data();
    
    // Calculate 20-day returns and classify regimes
    for (std::size_t i = 20; i < data.size(); ++i) {
        double past_close = (ptr + i - 20)->close;
        double current_close = (ptr + i)->close;

        // (Return)
        double twenty_day_return = (current_close - past_close) / past_close;

        if (twenty_day_return > threshold) {
            regimes.push_back(1);   // Bull 
        } else if (twenty_day_return < -threshold) {
            regimes.push_back(-1);  // Bear 
        } else {
            regimes.push_back(0);   // Neutral 
        }
    }

    return regimes;
}

int main() {
    std::string path = "sample_stock.csv";

    auto load_start = std::chrono::high_resolution_clock::now();
    std::vector<StockRecord> data = load_csv(path);
    auto load_end = std::chrono::high_resolution_clock::now();

    report_close_diagnostics(data); 

    std::cout << std::scientific << std::setprecision(8);
    std::cout << "First close: " << data.front().close << '\n';
    std::cout << "Last close:  " << data.back().close << '\n';

    double max_close = 0.0;
    std::size_t max_index = 0;

    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i].close > max_close) {
            max_close = data[i].close;
            max_index = i;
        }
    }

    std::cout << "Maximum close: " << max_close
            << " at row index " << max_index << '\n';


    std::cout << "Maximum close: " << max_close
            << " at row index " << max_index << '\n';
        


    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(load_end - load_start);

    auto compute_start = std::chrono::high_resolution_clock::now();
    double total_close = sum_close_prices(data);
    auto compute_end = std::chrono::high_resolution_clock::now();

    auto compute_time = std::chrono::duration_cast<std::chrono::microseconds>(compute_end - compute_start);

    auto return_start = std::chrono::high_resolution_clock::now();
    std::vector<double> returns = compute_daily_returns(data);
    auto return_end = std::chrono::high_resolution_clock::now();

    auto return_time = std::chrono::duration_cast<std::chrono::microseconds>(return_end - return_start);

    auto ma_start = std::chrono::high_resolution_clock::now();
    std::vector<double> ma = moving_average(data, 5);
    auto ma_end = std::chrono::high_resolution_clock::now();

    auto ma_time = std::chrono::duration_cast<std::chrono::microseconds>(ma_end - ma_start);

    std::cout << "C++ optimized benchmark" << std::endl;
    std::cout << "Rows loaded: " << data.size() << std::endl;
    std::cout << "Load time: " << load_time.count() << " microseconds" << std::endl;
    std::cout << "Sum close time: " << compute_time.count() << " microseconds" << std::endl;
    std::cout << "Return calculation time: " << return_time.count() << " microseconds" << std::endl;
    std::cout << "Moving average time: " << ma_time.count() << " microseconds" << std::endl;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Total close sum: " << total_close << std::endl;
    std::cout << "First 5 daily returns: ";
    for (std::size_t i = 0; i < std::min<std::size_t>(returns.size(), 5); ++i) {
        std::cout << returns[i] << " ";
    }
    std::cout << std::endl;

    std::cout << "First 5 moving averages: ";
    for (std::size_t i = 0; i < std::min<std::size_t>(ma.size(), 5); ++i) {
        std::cout << ma[i] << " ";
    }
    std::cout << std::endl;



       
    auto thresh_start = std::chrono::high_resolution_clock::now();
    std::vector<int> thresh_regimes = compute_threshold_regimes(data, 0.05); 
    auto thresh_end = std::chrono::high_resolution_clock::now();


    std::size_t bull_count = 0;
    std::size_t bear_count = 0;
    std::size_t neutral_count = 0;

    for (int regime : thresh_regimes) {
        if (regime == 1) ++bull_count;
        else if (regime == -1) ++bear_count;
        else ++neutral_count;
    }

    std::cout << "Regime counts — bull: " << bull_count
            << ", neutral: " << neutral_count
            << ", bear: " << bear_count << '\n';

    auto thresh_time = std::chrono::duration_cast<std::chrono::microseconds>(thresh_end - thresh_start);

    std::cout << "Threshold-based regime time: " << thresh_time.count() << " microseconds" << std::endl;
    std::cout << "First 5 regimes (20th day+): ";
    for (std::size_t i = 20; i < std::min<std::size_t>(thresh_regimes.size(), 25); ++i) {
        std::cout << thresh_regimes[i] << " ";
    }
    std::cout << std::endl;


    return 0;
}