#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct StockRecord {
    std::string date;
    double open;
    double high;
    double low;
    double close;
    long long volume;
};

enum class Regime {
    Unavailable = -1,
    Bear = 0,
    Neutral = 1,
    Bull = 2
};

using CountMatrix = std::array<std::array<std::size_t, 3>, 3>;
using ProbabilityMatrix = std::array<std::array<double, 3>, 3>;

struct TransitionModel {
    CountMatrix counts{};
    ProbabilityMatrix probabilities{};
    std::array<std::size_t, 3> next_state_counts{};
    int fallback_state = -1;
};

const char* regime_name(Regime regime) {
    switch (regime) {
        case Regime::Bear:    return "Bear";
        case Regime::Neutral: return "Neutral";
        case Regime::Bull:    return "Bull";
        default:              return "Unavailable";
    }
}

int state_index(Regime regime) {
    switch (regime) {
        case Regime::Bear:    return 0;
        case Regime::Neutral: return 1;
        case Regime::Bull:    return 2;
        default:              return -1;
    }
}

std::vector<std::string> split_csv_line(const std::string& line) {
    // Suitable for this downloaded CSV, which has no quoted fields with commas.
    std::stringstream stream(line);
    std::vector<std::string> fields;
    std::string field;

    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }

    return fields;
}

std::vector<StockRecord> load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path);
    }

    std::string line;
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty: " + path);
    }

    std::vector<StockRecord> data;
    std::size_t line_number = 1;

    while (std::getline(file, line)) {
        ++line_number;

        const auto fields = split_csv_line(line);
        if (fields.size() < 6) {
            continue;
        }

        try {
            StockRecord row;
            row.date = fields[0];
            row.open = std::stod(fields[1]);
            row.high = std::stod(fields[2]);
            row.low = std::stod(fields[3]);
            row.close = std::stod(fields[4]);
            row.volume = std::stoll(fields[5]);

            if (!std::isfinite(row.close) || row.close <= 0.0) {
                throw std::runtime_error("Close must be finite and positive.");
            }

            data.push_back(row);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Invalid CSV data on line " + std::to_string(line_number) +
                ": " + error.what()
            );
        }
    }

    if (data.empty()) {
        throw std::runtime_error("CSV contains no data rows.");
    }

    return data;
}

void report_close_diagnostics(const std::vector<StockRecord>& data) {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    long double sum = 0.0L;
    std::size_t maximum_index = 0;

    for (std::size_t i = 0; i < data.size(); ++i) {
        const double close = data[i].close;
        minimum = std::min(minimum, close);

        if (close > maximum) {
            maximum = close;
            maximum_index = i;
        }

        sum += close;
    }

    std::cout << "\n=== DATA DIAGNOSTICS ===\n"
              << std::scientific << std::setprecision(8)
              << "Rows: " << data.size() << '\n'
              << "Date range: " << data.front().date
              << " to " << data.back().date << '\n'
              << "Minimum close: " << minimum << '\n'
              << "Maximum close: " << maximum
              << " at row index " << maximum_index << '\n'
              << "Mean close: " << sum / data.size() << '\n'
              << "========================\n";
}

double sum_close_prices(const std::vector<StockRecord>& data) {
    double sum = 0.0;
    for (const auto& row : data) {
        sum += row.close;
    }
    return sum;
}

std::vector<double> compute_daily_returns(
    const std::vector<StockRecord>& data
) {
    std::vector<double> returns;
    if (data.size() < 2) {
        return returns;
    }

    returns.reserve(data.size() - 1);

    for (std::size_t i = 1; i < data.size(); ++i) {
        const double previous = data[i - 1].close;
        const double current = data[i].close;
        returns.push_back((current - previous) / previous);
    }

    return returns;
}

std::vector<double> moving_average(
    const std::vector<StockRecord>& data,
    std::size_t window
) {
    if (window == 0) {
        throw std::invalid_argument("Moving-average window must be positive.");
    }

    std::vector<double> averages;
    averages.reserve(data.size());

    double rolling_sum = 0.0;

    for (std::size_t i = 0; i < data.size(); ++i) {
        rolling_sum += data[i].close;

        if (i >= window) {
            rolling_sum -= data[i - window].close;
        }

        const std::size_t count = (i + 1 < window) ? i + 1 : window;
        averages.push_back(rolling_sum / count);
    }

    return averages;
}

std::vector<Regime> compute_threshold_regimes(
    const std::vector<StockRecord>& data,
    std::size_t lookback,
    double threshold
) {
    if (lookback == 0) {
        throw std::invalid_argument("Regime lookback must be positive.");
    }
    if (!std::isfinite(threshold) || threshold < 0.0) {
        throw std::invalid_argument("Threshold must be finite and non-negative.");
    }

    std::vector<Regime> regimes(data.size(), Regime::Unavailable);

    for (std::size_t i = lookback; i < data.size(); ++i) {
        const double past_close = data[i - lookback].close;
        const double current_close = data[i].close;
        const double period_return =
            (current_close - past_close) / past_close;

        if (period_return > threshold) {
            regimes[i] = Regime::Bull;
        } else if (period_return < -threshold) {
            regimes[i] = Regime::Bear;
        } else {
            regimes[i] = Regime::Neutral;
        }
    }

    return regimes;
}

TransitionModel fit_transition_model(
    const std::vector<Regime>& regimes,
    std::size_t train_end
) {
    if (train_end > regimes.size()) {
        throw std::invalid_argument("Training boundary exceeds the data size.");
    }

    TransitionModel model{};

    // Count only transitions fully inside the training portion.
    for (std::size_t i = 1; i < train_end; ++i) {
        const int from = state_index(regimes[i - 1]);
        const int to = state_index(regimes[i]);

        if (from >= 0 && to >= 0) {
            ++model.counts[from][to];
            ++model.next_state_counts[to];
        }
    }

    // Normalize each row to get conditional transition probabilities.
    for (int from = 0; from < 3; ++from) {
        std::size_t row_total = 0;
        for (int to = 0; to < 3; ++to) {
            row_total += model.counts[from][to];
        }

        if (row_total == 0) {
            continue;
        }

        for (int to = 0; to < 3; ++to) {
            model.probabilities[from][to] =
                static_cast<double>(model.counts[from][to]) / row_total;
        }
    }

    // Fallback prediction: most common next state in training.
    for (int state = 0; state < 3; ++state) {
        if (model.fallback_state < 0 ||
            model.next_state_counts[state] >
                model.next_state_counts[model.fallback_state]) {
            model.fallback_state = state;
        }
    }

    return model;
}

int predict_next_state(const TransitionModel& model, int current_state) {
    if (current_state < 0 || current_state >= 3) {
        return model.fallback_state;
    }

    std::size_t row_total = 0;
    for (std::size_t count : model.counts[current_state]) {
        row_total += count;
    }

    if (row_total == 0) {
        return model.fallback_state;
    }

    int best_next_state = 0;
    for (int state = 1; state < 3; ++state) {
        if (model.probabilities[current_state][state] >
            model.probabilities[current_state][best_next_state]) {
            best_next_state = state;
        }
    }

    return best_next_state;
}

void print_transition_matrix(const TransitionModel& model) {
    std::cout << "\nTraining transition counts"
              << " (rows = current state, columns = next state)\n";

    std::cout << std::setw(12) << "";
    for (int state = 0; state < 3; ++state) {
        std::cout << std::setw(10)
                  << regime_name(static_cast<Regime>(state));
    }
    std::cout << '\n';

    for (int from = 0; from < 3; ++from) {
        std::cout << std::setw(12)
                  << regime_name(static_cast<Regime>(from));
        for (int to = 0; to < 3; ++to) {
            std::cout << std::setw(10) << model.counts[from][to];
        }
        std::cout << '\n';
    }

    std::cout << "\nTraining transition probabilities\n";
    std::cout << std::setw(12) << "";
    for (int state = 0; state < 3; ++state) {
        std::cout << std::setw(10)
                  << regime_name(static_cast<Regime>(state));
    }
    std::cout << '\n';

    for (int from = 0; from < 3; ++from) {
        std::cout << std::setw(12)
                  << regime_name(static_cast<Regime>(from));
        for (int to = 0; to < 3; ++to) {
            std::cout << std::setw(10) << std::fixed
                      << std::setprecision(3)
                      << model.probabilities[from][to];
        }
        std::cout << '\n';
    }
}

int main() {
    const std::string path =
        R"(C:\Users\user\python\sample_stock.csv)";
    constexpr std::size_t lookback = 20;
    constexpr double threshold = 0.05;
    constexpr std::size_t moving_average_window = 5;

    try {
        const auto load_start = std::chrono::steady_clock::now();
        const std::vector<StockRecord> data = load_csv(path);
        const auto load_end = std::chrono::steady_clock::now();

        std::cout << "Reading: " << path << '\n';
        report_close_diagnostics(data);

        const auto sum_start = std::chrono::steady_clock::now();
        const double close_sum = sum_close_prices(data);
        const auto sum_end = std::chrono::steady_clock::now();

        const auto returns_start = std::chrono::steady_clock::now();
        const std::vector<double> returns = compute_daily_returns(data);
        const auto returns_end = std::chrono::steady_clock::now();

        const auto average_start = std::chrono::steady_clock::now();
        const std::vector<double> averages =
            moving_average(data, moving_average_window);
        const auto average_end = std::chrono::steady_clock::now();

        const auto regime_start = std::chrono::steady_clock::now();
        const std::vector<Regime> regimes =
            compute_threshold_regimes(data, lookback, threshold);
        const auto regime_end = std::chrono::steady_clock::now();

        const std::size_t train_end = data.size() * 70 / 100;
        if (train_end < 2 || train_end >= data.size()) {
            throw std::runtime_error(
                "Not enough data for a 70/30 chronological split."
            );
        }

        const TransitionModel model =
            fit_transition_model(regimes, train_end);

        std::array<std::size_t, 3> training_state_counts{};
        for (std::size_t i = 0; i < train_end; ++i) {
            const int state = state_index(regimes[i]);
            if (state >= 0) {
                ++training_state_counts[state];
            }
        }

        std::size_t tested = 0;
        std::size_t model_correct = 0;
        std::size_t persistence_correct = 0;
        std::size_t majority_correct = 0;

        // Test transitions begin at the 70% boundary.
        for (std::size_t i = train_end; i + 1 < regimes.size(); ++i) {
            const int current = state_index(regimes[i]);
            const int actual_next = state_index(regimes[i + 1]);

            if (current < 0 || actual_next < 0) {
                continue;
            }

            const int model_prediction =
                predict_next_state(model, current);
            const int persistence_prediction = current;
            const int majority_prediction = model.fallback_state;

            ++tested;
            model_correct += (model_prediction == actual_next);
            persistence_correct += (persistence_prediction == actual_next);
            majority_correct += (majority_prediction == actual_next);
        }

        if (tested == 0) {
            throw std::runtime_error("No valid test transitions were available.");
        }

        const auto elapsed_us = [](auto start, auto end) {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                end - start
            ).count();
        };

        std::cout << "\nTraining labels — "
                  << "Bear: " << training_state_counts[0]
                  << ", Neutral: " << training_state_counts[1]
                  << ", Bull: " << training_state_counts[2] << '\n';

        print_transition_matrix(model);

        std::cout << "\nChronological validation (first 70% train, last 30% test)\n"
                  << "Training rows: " << train_end << '\n'
                  << "Test rows: " << data.size() - train_end << '\n'
                  << "Test transitions evaluated: " << tested << '\n'
                  << std::fixed << std::setprecision(3)
                  << "Transition-model accuracy: "
                  << static_cast<double>(model_correct) / tested << '\n'
                  << "Persistence-baseline accuracy: "
                  << static_cast<double>(persistence_correct) / tested << '\n'
                  << "Majority-baseline accuracy: "
                  << static_cast<double>(majority_correct) / tested << '\n';

        std::cout << "\n=== Summary ===\n"
                  << "Rows loaded: " << data.size() << '\n'
                  << "Lookback: " << lookback << " observations\n"
                  << "Threshold: " << threshold * 100.0 << "%\n"
                  << "Close-price sum: " << std::setprecision(2)
                  << close_sum << '\n'
                  << "Load time: " << elapsed_us(load_start, load_end)
                  << " microseconds\n"
                  << "Sum time: " << elapsed_us(sum_start, sum_end)
                  << " microseconds\n"
                  << "Return calculation time: "
                  << elapsed_us(returns_start, returns_end)
                  << " microseconds\n"
                  << "Moving-average time: "
                  << elapsed_us(average_start, average_end)
                  << " microseconds\n"
                  << "Regime calculation time: "
                  << elapsed_us(regime_start, regime_end)
                  << " microseconds\n";

        if (!returns.empty()) {
            std::cout << "\nFirst five daily returns: ";
            for (std::size_t i = 0; i < std::min<std::size_t>(5, returns.size()); ++i) {
                std::cout << std::setprecision(4) << returns[i] << ' ';
            }
            std::cout << '\n';
        }

        if (!averages.empty()) {
            std::cout << "First five moving averages: ";
            for (std::size_t i = 0; i < std::min<std::size_t>(5, averages.size()); ++i) {
                std::cout << std::setprecision(4) << averages[i] << ' ';
            }
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}