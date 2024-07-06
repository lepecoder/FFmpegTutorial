#include <iostream>
#include "io_data.h"

int main() {
    std::cout << "Hello, World!!!" << std::endl;
    char input_file[] = "input.txt";
    char output_file[] = "output.txt";
    int32_t result = 12;
    result = open_input_output_files(input_file, output_file);
    std::cout << "result: " << result << std::endl;


    return 0;
}


