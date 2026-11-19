#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct StockRecord {
    std::string date;
    double open;
    double high;
    double low;
    double close;
    long volume;
};

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string item;

    while (std::getline(ss, item, ',')) {
        fields.push_back(item);
    }

    return fields;
}

std::vector<StockRecord> loadCsv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }

    std::vector<StockRecord> data;
    std::string line;

    // Skip header
    std::getline(file, line);

    while (std::getline(file, line)) {
        auto fields = splitCsvLine(line);
        if (fields.size() != 6) {
            continue;  // skip malformed rows
        }

        StockRecord record;
        record.date = fields[0];
        record.open = std::stod(fields[1]);
        record.high = std::stod(fields[2]);
        record.low = std::stod(fields[3]);
        record.close = std::stod(fields[4]);
        record.volume = std::stol(fields[5]);

        data.push_back(record);
    }

    return data;
}

double sumCloseIndex(const std::vector<StockRecord>& data) {
    double total = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        total += data[i].close;
    }
    return total;
}

double sumClosePointer(const std::vector<StockRecord>& data) {
    double total = 0.0;
    const StockRecord* ptr = data.data();
    const StockRecord* end = ptr + data.size();

    while (ptr < end) {
        total += ptr->close;
        ++ptr;
    }

    return total;
}

double averageClose(const std::vector<StockRecord>& data) {
    double total = 0.0;
    for (const auto& record : data) {
        total += record.close;
    }
    return total / static_cast<double>(data.size());
}

std::vector<double> computeDailyReturns(const std::vector<StockRecord>& data) {
    std::vector<double> returns;
    returns.reserve(data.size() - 1);

    for (std::size_t i = 1; i < data.size(); ++i) {
        double prevClose = data[i - 1].close;
        double currentClose = data[i].close;
        returns.push_back((currentClose - prevClose) / prevClose);
    }

    return returns;
}

int main() {
    std::string path = "sample_stock.csv";

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<StockRecord> data = loadCsv(path);
    auto end = std::chrono::high_resolution_clock::now();

    auto loadMs = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    start = std::chrono::high_resolution_clock::now();
    double sumIndex = sumCloseIndex(data);
    end = std::chrono::high_resolution_clock::now();
    auto indexMs = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    start = std::chrono::high_resolution_clock::now();
    double sumPointer = sumClosePointer(data);
    end = std::chrono::high_resolution_clock::now();
    auto pointerMs = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    auto returns = computeDailyReturns(data);
    double avgClose = averageClose(data);

    std::cout << "C++ benchmark" << std::endl;
    std::cout << "Rows loaded: " << data.size() << std::endl;
    std::cout << "Load time: " << loadMs.count() << " us" << std::endl;
    std::cout << "Index-based sum time: " << indexMs.count() << " us" << std::endl;
    std::cout << "Pointer-based sum time: " << pointerMs.count() << " us" << std::endl;
    std::cout << "Index sum: " << std::fixed << std::setprecision(4) << sumIndex << std::endl;
    std::cout << "Pointer sum: " << std::fixed << std::setprecision(4) << sumPointer << std::endl;
    std::cout << "Average close: " << std::fixed << std::setprecision(4) << avgClose << std::endl;

    std::cout << "First 5 daily returns: ";
    for (std::size_t i = 0; i < std::min<std::size_t>(returns.size(), 5); ++i) {
        std::cout << std::fixed << std::setprecision(4) << returns[i] << " ";
    }
    std::cout << std::endl;

    return 0;
}