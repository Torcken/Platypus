# A monolithic class that handles both data parsing and calculation.
# The goal is to refactor this into a Strategy pattern.

#include <iostream>
#include <vector>
#include <string>
#include <numeric>

class DataProcessor {
public:
    void processData(const std::vector<double>& data) {
        // Parsing logic (simplified)
        std::cout << "Parsing data..." << std::endl;
        
        // Calculation logic - Average
        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        double avg = sum / data.size();
        std::cout << "Average: " << avg << std::endl;

        // Calculation logic - Max
        double max_val = *std::max_element(data.begin(), data.end());
        std::cout << "Max: " << max_val << std::endl;
    }
};

int main() {
    DataProcessor processor;
    std::vector<double> data = {10.5, 20.0, 30.5, 40.0};
    processor.processData(data);
    return 0;
}
