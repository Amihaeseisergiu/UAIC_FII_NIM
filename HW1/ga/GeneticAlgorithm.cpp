#include "GeneticAlgorithm.h"
#include "Constants.h"

#include <cmath>
#include <execution>
#include <iostream>
#include <ranges>

namespace ranges = std::ranges;
namespace exec = std::execution;
namespace cst = ga::constants;

namespace ga {

namespace {

double decodeBinaryVariable(const chromozome_cit begin)
{
    // Nice, except the formatting
    return std::accumulate(begin, std::next(begin, cst::bitsPerVariable), 0LL,
                           [](auto f, auto elem) { return f * 2 + elem; }) /
               (cst::discriminator) * (cst::maximum - cst::minimum) +
           cst::minimum;
}

} // namespace

GeneticAlgorithm getDefault(std::string&& functionName)
{
    return {0.7,   // crossoverProbability
            0.001, // mutationProbability
            0.01,  // hypermutationRate
            0.04,  // elitesPercentage
            10,    // selectionPressure
            0.1,   // encodingChangeRate
            100,   // populationSize
            20,    // dimensions
            // TODO: seems to fail with dimensions = 100, check why
            10,   // stepsToHypermutation
            1000, // maxNoImprovementSteps
            std::move(functionName),
            false,  // applyShift
            false}; // applyRotation
}

GeneticAlgorithm::GeneticAlgorithm(
    double crossoverProbability, double mutationProbability,
    double hypermutationRate, double elitesPercentage, double selectionPressure,
    double encodingChangeRate, int populationSize, int dimensions,
    int stepsToHypermutation, int maxNoImprovementSteps,
    std::string&& functionName, bool applyShift, bool applyRotation)
    // clang-format off
    : crossoverProbability{crossoverProbability}
    , mutationProbability{mutationProbability}
    , hypermutationRate{hypermutationRate}
    , elitesPercentage{elitesPercentage}
    , selectionPressure{selectionPressure}
    , encodingChangeRate{encodingChangeRate}
    , maxSteps{dimensions == 10 ? 200'000 : 1'000'000}
    , populationSize{populationSize}
    , dimensions{dimensions}
    , bitsPerChromozome{dimensions * cst::bitsPerVariable}
    , stepsToHypermutation{stepsToHypermutation}
    , maxNoImprovementSteps{maxNoImprovementSteps}
    , elitesNumber{static_cast<int>(elitesPercentage * populationSize)}
    , function{std::move(functionName), dimensions, applyShift, applyRotation}
// clang-format on
{
    std::cout << "Using " << cst::bitsPerVariable << " bits per variable\n";
    std::cout << "Using " << cst::discriminator << " discriminator\n";
    std::cout << "Using " << bitsPerChromozome << " bits per chromozome\n";
    for (auto i = 0; i < populationSize; ++i) {
        population.push_back(chromozome(bitsPerChromozome, true));
        newPopulation.push_back(chromozome(bitsPerChromozome, true));
        // will be randomized at each run call
        decodings.push_back(std::vector<double>(dimensions, 0.0));
    }

    fitnesses.resize(populationSize);
    selectionProbabilities.resize(populationSize);
    indices.resize(populationSize);
    std::iota(indices.begin(), indices.end(), 0);

    decodingStrategy = decodeBinaryVariable;
    radomChromozome = std::uniform_int_distribution<>{0, populationSize - 1};
    randomSlice = std::uniform_int_distribution<>{0, bitsPerChromozome - 1};
}

void GeneticAlgorithm::sanityCheck()
{
    std::cout << "GeneticAlgorithm::sanityCheck" << '\n';
    std::cout << evaluateChromozome(0) << '\n';
}

void GeneticAlgorithm::randomizePopulationAndInitBest()
{
    for (auto& chromozome : population) {
        for (auto i = 0; i < bitsPerChromozome; ++i) {
            chromozome[i] = randomBool(gen);
        }
    }
    bestChromozome = population[0];
    bestValue = evaluateChromozome(0);
}

std::vector<double>& GeneticAlgorithm::decodeChromozome(std::size_t index)
{
    auto it = population[index].cbegin();
    for (auto i = 0; i < dimensions; ++i) {
        decodings[index][i] = decodingStrategy(it);
        it = std::next(it, bitsPerChromozome);
    }
    // TODO: Refactor to use std algorithm
    return decodings[index];
}

std::vector<double>
GeneticAlgorithm::decodeChromozome(const chromozome& chromozome) const
{
    std::vector<double> x;
    x.reserve(dimensions);
    auto it = chromozome.cbegin();
    for (auto i = 0; i < dimensions; ++i) {
        x.push_back(decodingStrategy(it));
        it = std::next(it, bitsPerChromozome);
    }
    return x;
}

double GeneticAlgorithm::evaluateChromozome(std::size_t index)
{
    return function(decodeChromozome(index));
}

double GeneticAlgorithm::evaluateChromozomeAndUpdateBest(std::size_t index)
{
    // might be better to use iterator instead of index
    auto ret = evaluateChromozome(index);
    if (ret < bestValue) {
        bestValue = ret;
        bestChromozome = population[index];
        lastImprovement = epoch;
    }
    return ret;
}

void GeneticAlgorithm::evaluatePopulation()
{
    fitnesses[0] = evaluateChromozomeAndUpdateBest(0);
    auto min = fitnesses[0];
    auto max = fitnesses[0];

    std::for_each(indices.begin(), indices.end(), [&](auto i) {
        fitnesses[i] = evaluateChromozomeAndUpdateBest(i);
        if (fitnesses[i] < min) {
            min = fitnesses[i];
        }
        if (fitnesses[i] > max) {
            max = fitnesses[i];
        }
    });

    computeSelectionProbabilities(normalizeFitness(min, max));
}

double GeneticAlgorithm::normalizeFitness(double min, double max)
{
    constexpr auto epsilon = 0.00001;
    auto total = 0.0;
    std::for_each(indices.begin(), indices.end(), [&](auto i) {
        fitnesses[i] =
            std::pow((max - fitnesses[i]) / (max - min + epsilon) + 1,
                     selectionPressure);
        total += fitnesses[i];
    });
    return total;
}

void GeneticAlgorithm::computeSelectionProbabilities(double total)
{
    // we reduce the scope of prev
    std::transform(fitnesses.begin(), fitnesses.end(),
                   selectionProbabilities.begin(),
                   [prev = 0.0, total](auto elem) mutable {
                       prev += elem / total;
                       return prev;
                   });
}

chromozome GeneticAlgorithm::selectChromozome()
{
    const auto random = randomDouble(gen);
    for (auto i = 0; i < populationSize; ++i) {
        if (random <= selectionProbabilities[i]) {
            return population[i];
        }
    }
    // this is returned by value hoping for RVO
    return population[populationSize - 1];
}

void GeneticAlgorithm::selectNewPopulation()
{
    if (elitesNumber > 0) {
        // using indices for partial sorting
        std::nth_element(
            indices.begin(), std::next(indices.begin(), elitesNumber),
            indices.end(), [this](auto i, auto j) {
                return selectionProbabilities[i] > selectionProbabilities[j];
            });

        // moving best elitesNumber chromozomes to elites
        std::transform(
            indices.begin(), std::next(indices.begin(), elitesNumber),
            newPopulation.begin(), [this](auto i) { return population[i]; });

        // reseting indices
        std::iota(indices.begin(), indices.end(), 0);
    }
    // skipping elites number for both iterators
    std::transform(
        std::next(population.begin(), elitesNumber), population.end(),
        std::next(newPopulation.begin(), elitesNumber),
        [this]([[maybe_unused]] auto& elem) { return selectChromozome(); });
    population.swap(newPopulation);
}

bool GeneticAlgorithm::stop() const
{
    return (epoch - lastImprovement > maxNoImprovementSteps);
    // TODO: Add condition to check that global optimum has been achieved.
}

void GeneticAlgorithm::mutatePopulation()
{
    // skipping half the elites
    const auto begin = std::next(population.begin(), elitesNumber / 2);
    std::for_each(begin, population.end(),
                  [&](auto& x) { mutateChromozome(x); });
    // This can be vectorized (std::execution::unseq), and tested if it brings
    // any benefit for such a small population. Might do in the long run.
}

void GeneticAlgorithm::mutateChromozome(chromozome& chromozome)
{
    for (auto i = 0; i < bitsPerChromozome; ++i) {
        if (randomDouble(gen) < mutationProbability) {
            chromozome[i] = not chromozome[i];
        }
    }
    // Not using std algorithm to iterate over bool container because it's not a
    // container. Might do so for char.
}

void GeneticAlgorithm::crossoverPopulation()
{
    std::for_each(indices.begin(), indices.end(), [this](auto i) {
        if (randomDouble(gen) < crossoverProbability) {
            crossoverChromozomes(i, radomChromozome(gen));
        }
    });
    // should not be parallelized because it has side effects
}

void GeneticAlgorithm::crossoverChromozomes(std::size_t i, std::size_t j)
{
    // TODO: Maybe we could do crossover by using different operations,
    // for example xor and or nor
    // Try to add crossover strategies and use them

    const auto slicePosition = randomSlice(gen);
    for (auto k = 0; k < slicePosition; ++k) {
        population[i].swap(population[i][k], population[j][k]);
    }

    // Swap version for vector of char, might not work for bool because
    // iterator is not LegacyForwardIterator
    //
    // const auto end = std::next(population[i].begin(), randomSlice(gen));
    // std::swap_ranges(population[i].begin(), end, population[j].begin());
    //
    // this can and might be worth vectorizing for char, because char has
    // LegacyContiguousIterator and each chromozome has 350 bits
}

void GeneticAlgorithm::hillclimbPopulation()
{
    std::for_each(
        exec::unseq, population.begin(), population.end(),
        [this](auto& chromozome) { hillclimbChromozome(chromozome); });
}

void GeneticAlgorithm::hillclimbChromozome(std::size_t index)
{
    // if not used, remove
    hillclimbChromozome(population[index]);
}

void GeneticAlgorithm::hillclimbChromozome(chromozome& chromozome)
{
    // TODO: Implement
}

void GeneticAlgorithm::printBest() const
{
    const auto bestDecoded = decodeChromozome(bestChromozome);
    std::cout << "Best: " << bestValue << '\n';
    for (const auto x : bestDecoded) {
        std::cout << x << ' ';
    }
    std::cout << '\n';
}

void GeneticAlgorithm::run()
{
    randomizePopulationAndInitBest();
    for (epoch = 0; epoch < maxSteps / populationSize; ++epoch) {
        std::cout << "Epoch: " << epoch << "\tBest: " << bestValue << '\n';
        if (stop()) {
            break;
        }

        mutatePopulation();
        crossoverPopulation();
        evaluatePopulation();
        selectNewPopulation();
    }
    printBest();
}

} // namespace ga
