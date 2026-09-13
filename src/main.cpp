#include <EASTL/vector.h>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <vector>


int main(){
    eastl::vector<int> eastl_vals{1,2,3,4, 5};
    std::vector<int> std_vals{1,2,3,4};

    const bool values_match = std::equal(
        eastl_vals.begin(),
        eastl_vals.end(),
        std_vals.begin(),
        std_vals.end()
    );

    if (values_match){
        std::cout << "EASTL and STD Vals match .\n";
        return EXIT_SUCCESS;
    }
    std::cout << "EASTL and STD Vals DO NOT match .\n";
    return EXIT_FAILURE;
}