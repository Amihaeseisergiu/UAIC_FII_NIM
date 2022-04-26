#include "Swarm.h"

#include <execution>
#include <iostream>
#include <stdexcept>

namespace constants = utils::constants;

namespace swarm {

namespace {

// TODO: use templates and concepts
void randomizeVector(std::vector<double>& v,
                     std::uniform_real_distribution<double>& dist,
                     std::mt19937_64& gen, double l)
{
    std::generate(v.begin(), v.end(), [&, l]() { return dist(gen) * l; });
}

void randomizeVector(std::vector<double>& v,
                     std::uniform_real_distribution<double>& dist,
                     std::mt19937_64& gen)
{
    randomizeVector(v, dist, gen, 1);
}

std::string vecToString(const std::vector<double>& v)
{
    using namespace std::string_literals;
    if (v.empty()) {
        return "[]"s;
    }
    auto ret = "["s + std::to_string(v[0]);
    return std::accumulate(std::next(v.begin()), v.end(), ret,
                           [](auto&& f, const auto x) {
                               return std::move(f) + ","s + std::to_string(x);
                           }) +
           "]"s;
}

} // namespace

Swarm getDefault(int dimensions)
{
    return Swarm{
        dimensions,
        100,                    // populationSize
        100,                    // resetThreshold
        0.3,                    // inertia
        1,                      // cognition
        3.0,                    // social
        0.1,                    // swarmAttraction
        0.001,                  // chaosCoef
        topology::Star,         // topology
        true                    // augment
    };
}

// clang-format off
Swarm::Swarm(
        int dimensions,
        int populationSize,
        int resetThreshold,
        double inertia,
        double cognition,
        double social,
        double chaosCoef,
        double swarmAttraction,
        topology topology,
        bool augment)
    : dimensions{dimensions}
    , resetThreshold{resetThreshold}
    , populationSize{populationSize}
    , inertia{inertia}
    , cognition{cognition}
    , social{social}
    , swarmAttraction{swarmAttraction}
    , chaosCoef{chaosCoef}
    , swarmTopology{topology}
    , augment{augment}
// clang-format on
{
    //Seed here because seed is stupid
    std::random_device seed;
    gen = std::mt19937_64(seed());

    population = std::vector<std::vector<double>>(
        populationSize, std::vector<double>(dimensions));

    //Maybe only one aux for every swarm is enough
    aux = std::vector<std::vector<double>>(populationSize,
                                           std::vector<double>(dimensions));
    populationVelocity = std::vector<std::vector<double>>(
        populationSize, std::vector<double>(dimensions));
    populationPastBests = std::vector<std::vector<double>>(
        populationSize, std::vector<double>(dimensions));
    populationInertia = std::vector<double>(populationSize);
    evaluations = std::vector<double>(populationSize);
    populationPastBestEval = std::vector<double>(
        populationSize, std::numeric_limits<double>::infinity());
    globalBest.resize(dimensions);

    indices.resize(populationSize);
    std::iota(indices.begin(), indices.end(), 0);

    neighbors.resize(populationSize + 2);

    // creating neighbors for ring topology
    std::iota(neighbors.begin(), neighbors.end(), -1);
    neighbors[0] = populationSize - 1;
    neighbors[neighbors.size() - 1] = 0;
}

double Swarm::getVisibleBest(int index, int dimensions)
{
    if (swarmTopology == topology::StaticRing) {
        return getStaticRingBest(index, dimensions);
    }
    if (swarmTopology == topology::Star) {
        return getStarBest(index, dimensions);
    }
    throw std::runtime_error("Not implemented topology");
}

void Swarm::initialize(std::shared_ptr<function_layer::FunctionManager> sharedFunctionManager)
{
    functionManager = sharedFunctionManager;
    resetPopulation();
}

void Swarm::resetPopulation()
{
    std::for_each(indices.begin(), indices.end(), [this](const auto i) {
        randomizeVector(population[i], randomFromDomain, gen);
        randomizeVector(populationVelocity[i], randomFromDomainRange, gen);
        const auto particleValue = functionManager->operator()(population[i], aux[i]);

        if (particleValue < populationPastBestEval[i]) {
            populationPastBests[i] = population[i];
            populationPastBestEval[i] = particleValue;
        }

        if (particleValue < globalBestEval) {
            globalBestEval = particleValue;
            globalBest = population[i];
        }

        populationInertia[i] = inertia;
    });
}

std::string Swarm::getBestVector() const
{
    return vecToString(globalBest);
}

void Swarm::updatePopulation(const std::vector<double>& swarmsBest)
{
    checkForPopulationReset();
    mutate();
    updateVelocity(swarmsBest);
    evaluate();
    updateBest();
    updateInertia();
    endIteration();
}

void Swarm::checkForPopulationReset()
{
    if (lastImprovement > resetThreshold) {
        // std::cout << "Reset at epoch: " << currentEpoch << std::endl;
        resetPopulation();
    }
}

void Swarm::endIteration()
{
    ++currentEpoch;
    ++lastImprovement; // if we had improvement, it was already reset to 0,
                       // now it's 1
}

void Swarm::mutate()
{
    // TODO: generate positions
    if (not augment) {
        return;
    }
    std::for_each(indices.begin(), indices.end(), [this](auto i) {
        std::transform(populationVelocity[i].begin(),
                       populationVelocity[i].end(),
                       populationVelocity[i].begin(), [this](const auto x) {
                           if (randomDouble(gen) < chaosCoef) {
                               return randomFromDomainRange(gen);
                           }
                           return x;
                       });
    });
}

void Swarm::updateVelocity(const std::vector<double>& swarmsBest)
{

    // par_unseq or unseq?
    std::for_each(
        std::execution::par_unseq, indices.begin(), indices.end(),
        [&swarmsBest, this](const auto i) {
            const auto rCognition = randomDouble(gen);
            const auto rSocial = randomDouble(gen);
            const auto rInertia = randomDouble(gen);
            const auto rSwarm = randomDouble(gen);

            for (auto d = 0; d < dimensions; ++d) {
                // TODO: this can be faster if we only do the else and apply the
                // mutation outside when applying the mutation it is not
                // necessary to iterate through all particles all dimensions, we
                // can generate the positions that are going to be mutated
                populationVelocity[i][d] =
                    rInertia * populationInertia[i] *
                        populationVelocity[i][d] +
                    cognition * rCognition *
                        (populationPastBests[i][d] - population[i][d]) +
                    social * rSocial *
                        (getVisibleBest(i, d) - population[i][d])
                    + swarmAttraction * rSwarm * (swarmsBest[d] - population[i][d])
                    ;

                // TODO: Use modulo arithmetics
                if (populationVelocity[i][d] > constants::valuesRange) {
                    populationVelocity[i][d] = constants::valuesRange;
                } else if (populationVelocity[i][d] < -constants::valuesRange) {
                    populationVelocity[i][d] = -constants::valuesRange;
                }

                // TODO: Add strategy (clipping to domain or reflection)

                // TODO: See if modulo arithmetic can be used in this case.
                // How would this work: use an usigned to represent [minimum,
                // maximum] and do operations for unsigneds then convert to
                // double
                population[i][d] += populationVelocity[i][d];
                while (population[i][d] < constants::minimum or
                       population[i][d] > constants::maximum) {
                    if (population[i][d] < constants::minimum) {
                        population[i][d] =
                            2 * constants::minimum - population[i][d];
                    }
                    if (population[i][d] > constants::maximum) {
                        population[i][d] =
                            2 * constants::maximum - population[i][d];
                    }
                }
            }
        });
}

void Swarm::evaluate()
{
    // cannot be parallelized because exception triggers std::terminate
    std::transform(population.begin(), population.end(), aux.begin(),
                   evaluations.begin(),
                   [this](const auto& particle, auto& aux) {
                       return functionManager->operator()(particle, aux);
                   });
}

void Swarm::updateBest()
{
    std::for_each(indices.begin(), indices.end(), [this](const auto i) {
        if (evaluations[i] < populationPastBestEval[i]) {
            populationPastBestEval[i] = evaluations[i];
            populationPastBests[i] = population[i];

            // TODO: maybe do update best outside loop
            if (evaluations[i] < globalBestEval) {
                // std::cout << functionManager.getFunctionName()
                //           << " Epoch: " << currentEpoch << " BEST: " <<
                //           current
                //           << '\n';
                globalBestEval = evaluations[i];
                globalBest = population[i];

                lastImprovement = 0;
            }
        }
    });
}

void Swarm::updateInertia()
{
    std::transform(std::execution::unseq, evaluations.begin(),
                   evaluations.end(), populationInertia.begin(),
                   [this](const auto evaluation) {
                       return (inertia + (1.0 - (globalBestEval / evaluation)) *
                                             (1.0 - inertia)) *
                              randomDouble(gen);
                   });
}

double Swarm::getStarBest([[maybe_unused]] std::size_t index,
                        std::size_t dimension) const
{
    return globalBest[dimension];
}

double Swarm::getStaticRingBest(std::size_t index, std::size_t dimension) const
{
    const auto leftIndex = neighbors[index];
    const auto rightIndex = neighbors[index + 2];
    const auto leftBest = populationPastBestEval[leftIndex];
    const auto rightBest = populationPastBestEval[rightIndex];
    const auto currentBest = populationPastBestEval[index];

    if (leftBest < currentBest and leftBest < rightBest) {
        return populationPastBests[leftIndex][dimension];
    } else if (rightBest < currentBest and rightBest < leftBest) {
        return populationPastBests[rightIndex][dimension];
    }
    return populationPastBests[index][dimension];
}

double Swarm::getBestEvaluation()
{
    return globalBestEval;
}

const std::vector<double>& Swarm::getBestParticle()
{
    return globalBest;
}

} // namespace swarm
