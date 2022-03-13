
#include "Cec22.h"
#include "GeneticAlgorithm.h"

#include <iostream>

int main()
{
    ga::functions::sanity_check();
    ga::GeneticAlgorithm ga = ga::getDefault("zakharov_func");
    ga.sanityCheck();
    return 0;
}
