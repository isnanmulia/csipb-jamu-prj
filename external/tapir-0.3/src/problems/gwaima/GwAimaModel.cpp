/** @file GwAimaModel.cpp
 *
 * Contains the implementations for the core functionality of the GwAima POMDP.
 */
#include "GwAimaModel.hpp"

#include <cmath>                        // for floor, pow
#include <cstddef>                      // for size_t
#include <cstdlib>                      // for exit

#include <memory>
#include <fstream>                      // for ifstream, basic_istream, basic_istream<>::__istream_type
#include <iomanip>                      // for operator<<, setw
#include <iostream>                     // for cout
#include <random>                       // for uniform_int_distribution, bernoulli_distribution
#include <unordered_map>                // for _Node_iterator, operator!=, unordered_map<>::iterator, _Node_iterator_base, unordered_map
#include <utility>                      // for make_pair, move, pair

#include "global.hpp"                     // for RandomGenerator, make_unique
#include "problems/shared/GridPosition.hpp"  // for GridPosition, operator==, operator!=, operator<<
#include "problems/shared/ModelWithProgramOptions.hpp"  // for ModelWithProgramOptions

#include "solver/abstract-problem/Action.hpp"            // for Action
#include "solver/abstract-problem/Model.hpp"             // for Model::StepResult, Model
#include "solver/abstract-problem/Observation.hpp"       // for Observation
#include "solver/abstract-problem/State.hpp"             // for State, operator<<, operator==

#include "solver/changes/ChangeFlags.hpp"        // for ChangeFlags

#include "solver/indexing/FlaggingVisitor.hpp"
#include "solver/indexing/RTree.hpp"
#include "solver/indexing/SpatialIndexVisitor.hpp"             // for State, operator<<, operator==

#include "solver/mappings/actions/enumerated_actions.hpp"
#include "solver/mappings/observations/discrete_observations.hpp"

#include "solver/ActionNode.hpp"
#include "solver/BeliefNode.hpp"
#include "solver/StatePool.hpp"

#include "GwAimaAction.hpp"
#include "GwAimaObservation.hpp"
#include "GwAimaOptions.hpp"
#include "GwAimaState.hpp"                 // for GwAimaState
#include "GwAimaTextSerializer.hpp"

using std::cout;
using std::endl;

namespace gwaima {

GwAimaUBParser::GwAimaUBParser(GwAimaModel *model) :
        model_(model) {
}

solver::HeuristicFunction GwAimaUBParser::parse(solver::Solver * /*solver*/, std::vector<std::string> /*args*/) {
    return [this] (solver::HistoryEntry const *, solver::State const *state,
            solver::HistoricalData const *) {
        return model_->getUpperBoundHeuristicValue(*state);
    };
}

GwAimaModel::GwAimaModel(RandomGenerator *randGen, std::unique_ptr<GwAimaOptions> options) :
            ModelWithProgramOptions("GwAima", randGen, std::move(options)),
            options_(const_cast<GwAimaOptions *>(static_cast<GwAimaOptions const *>(getOptions()))),
            moveCost_(options_->moveCost),
            nRows_(0), // to be updated
            nCols_(0), // to be updated
            startPos_(), // update
            goalPositions_(), // push goals
            mapText_(), // will be pushed to
            envMap_(), // will be pushed to
            nActions_(5),
            mdpSolver_(nullptr),
            pairwiseDistances_() {
    options_->numberOfStateVariables = 5;//TODO why 5?
    options_->minVal = -boomPenalty_ / (1 - options_->discountFactor);
    options_->maxVal = goalReward_;

    // Register the upper bound heuristic parser.
    registerHeuristicParser("upper", std::make_unique<GwAimaUBParser>(this));
    // Register the exact MDP heuristic parser.
    registerHeuristicParser("exactMdp", std::make_unique<GwAimaMdpParser>(this));

    // Read the map from the file.
    std::ifstream inFile;
    inFile.open(options_->mapPath);
    if (!inFile.is_open()) {
        std::ostringstream message;
        message << "ERROR: Failed to open " << options_->mapPath;
        debug::show_message(message.str());
        std::exit(1);
    }
    inFile >> nRows_ >> nCols_;
    std::string tmp;
    getline(inFile, tmp);
    for (long i = 0; i < nRows_; i++) {
        getline(inFile, tmp);
        mapText_.push_back(tmp);
    }
    inFile.close();

    initialize();
    if (options_->hasVerboseOutput) {
        cout << "Constructed the GwAimaModel" << endl;
        cout << "Discount: " << options_->discountFactor << endl;
        cout << "Size: " << nRows_ << " by " << nCols_ << endl;
        cout << "move cost: " << moveCost_ << endl;
        cout << "nActions: " << nActions_ << endl;
        cout << "nStVars: " << options_->numberOfStateVariables << endl;
        cout << "minParticleCount: " << options_->minParticleCount << endl;
        cout << "Environment:" << endl << endl;
        drawEnv(cout);
    }
}

int GwAimaModel::getMapDistance(GridPosition p1, GridPosition p2) {
    return pairwiseDistances_[p1.i][p1.j][p2.i][p2.j];
}

double GwAimaModel::getTransitionProbability(GridPosition nextRobotPos, 
                                             GridPosition robotPos, ActionType actionType) {
    int drow, dcol;
    drow = nextRobotPos.i - robotPos.i;
    dcol = nextRobotPos.j - robotPos.j;

    switch (static_cast<int>(actionType)) {
        case ActionType::NORTH: {
            if (drow > 0 ) return 0.0;
            else            
        }
    }

    if (drow > 0) {
        switch (static_cast<int>(actionType)) {
            case ActionType::NORTH:
                return 

        }
    }

    if (dcol > 0) {

    }
}

void GwAimaModel::calculateDistancesFrom(GridPosition position) {
    auto &distanceGrid = pairwiseDistances_[position.i][position.j];
    // Fill the grid with "-1", for inaccessible cells.
    for (auto &row : distanceGrid) {
        for (auto &cell : row) {
            cell = -1;
        }
    }
    if (envMap_[position.i][position.j] == GwAimaCellType::WALL) {
        return;
    }

    std::queue<GridPosition> queue;
    // Start at 0 for the current position.
    distanceGrid[position.i][position.j] = 0;
    queue.push(position);
    while (!queue.empty()) {
        GridPosition pos = queue.front();
        queue.pop();
        int distance = distanceGrid[pos.i][pos.j] + 1;
        for (ActionType direction : { ActionType::NORTH, ActionType::SOUTH, ActionType::WEST,
                ActionType::EAST }) {
            GridPosition nextPos;
            bool isLegal;
            std::tie(nextPos, isLegal) = getMovedPos(pos, direction);
            // If it's legal and it's an improvement it needs to be queued.
            if (isLegal) {
                int &nextPosDistance = distanceGrid[nextPos.i][nextPos.j];
                if (nextPosDistance == -1 || nextPosDistance > distance) {
                    nextPosDistance = distance;
                    queue.push(nextPos);
                }
            }
        }
    }
}

void GwAimaModel::calculatePairwiseDistances() {
    for (int i = 0; i < nRows_; i++) {
        for (int j = 0; j < nCols_; j++) {
            calculateDistancesFrom(GridPosition(i, j));
        }
    }
}

void GwAimaModel::initialize() {
    GridPosition p;
    envMap_.resize(nRows_);
    for (p.i = nRows_ - 1; p.i >= 0; p.i--) {
        envMap_[p.i].resize(nCols_);
        for (p.j = 0; p.j < nCols_; p.j++) {
            char c = mapText_[p.i][p.j];
            GwAimaCellType cellType;
            if (c == 'X') {
                cellType = GwAimaCellType::WALL;
            } else if (c == 'G') {
                cellType = GwAimaCellType::GOAL;
                goalPositions_.push_back(p);
            } else if (c == 'B') {
                cellType = GwAimaCellType::BOOM;   
                boomPositions_.push_back(p);
            } else {
                cellType = GwAimaCellType::EMPTY;
            }
            envMap_[p.i][p.j] = cellType;
        }
    }

    pairwiseDistances_.resize(nRows_);
    for (auto &rowOfGrids : pairwiseDistances_) {
        rowOfGrids.resize(nCols_);
        for (auto &grid : rowOfGrids) {
            grid.resize(nRows_);
            for (auto &row : grid) {
                row.resize(nCols_);
            }
        }
    }

    calculatePairwiseDistances();
}

GridPosition GwAimaModel::randomEmptyCell() {
    GridPosition pos;
    while (true) {
        pos.i = std::uniform_int_distribution<long>(0, nRows_ - 1)(
                *getRandomGenerator());
        pos.j = std::uniform_int_distribution<long>(0, nCols_ - 1)(
                *getRandomGenerator());
        if (envMap_[pos.i][pos.j] == GwAimaCellType::EMPTY) {
            break;
        }
    }
    return pos;
}

std::vector<GridPosition> GwAimaModel::getEmptyCells() {
    std::vector<GridPosition> emptyCells;
    for (long row = 0; row < this->getNRows(); row++) {
        for (long col = 0; col < this->getNCols(); col++) {
            // Ignore impossible states.
            if (envMap_[row][col] == GwAimaCellType::EMPTY) {
                emptyCells.emplace_back(row, col);
            }
        }
    }
    return emptyCells;
}

/* --------------- The model interface proper ----------------- */
std::unique_ptr<solver::State> GwAimaModel::sampleAnInitState() {
    return sampleStateUninformed();
}

std::unique_ptr<solver::State> GwAimaModel::sampleStateUninformed() {
    GridPosition robotPos = randomEmptyCell();
    return std::make_unique<GwAimaState>(robotPos);
}

bool GwAimaModel::isTerminal(solver::State const &state) {
    return (static_cast<GwAimaState const &>(state)==GwAimaState(goalPositions_[0])
            or 
            static_cast<GwAimaState const &>(state)==GwAimaState(boomPositions_[0])
           );
}

bool GwAimaModel::isValid(solver::State const &state) {
    GwAimaState const gwAimaState = static_cast<GwAimaState const &>(state);
    return isValid(gwAimaState.getRobotPosition());
}

/* -------------------- Black box dynamics ---------------------- */
std::pair<std::unique_ptr<GwAimaState>, bool> GwAimaModel::makeNextState(
        solver::State const &state, solver::Action const &action) {
	GwAimaState const &gwAimaState = static_cast<GwAimaState const &>(state);
	GwAimaAction const &gwAimaAction = static_cast<GwAimaAction const &>(action);
    
    GridPosition robotPos = gwAimaState.getRobotPosition();

    GridPosition newRobotPos;
    bool wasValid;
    std::tie(newRobotPos, wasValid) = sampleNextRobotPosition(robotPos, gwAimaAction);
        
    return std::make_pair(std::make_unique<GwAimaState>(newRobotPos), wasValid);
}

std::pair<GridPosition, bool> sampleNextRobotPosition(GridPosition robotPos, 
                                                      GwAimaAction desiredAction) {
    
    // Take into account the transition probability via the actual action selection
    // For example, if the desired action is North, then
    // the probability of actual action  Get robot action based on transition probability
    std::discrete_distribution<int> distribution {0.8, 0.1, 0.1, 0.0};
    unsigned int actionNo = distribution( *getRandomGenerator() );

    const unsigned int nActions = 4;
    GwAimaAction actualAction;
    switch (actionNo) {
        case 0: { // go straight
            actualAction = desiredAction;
            break;
        }
        case 1: { // turn right
            actualAction = desiredAction + 1;
            actualAction %= nActions; 
            break;
        }
        case 2: { // turn left
            if (static_cast<int>(desiredAction)==0)
                actualAction = ActionType::WEST;
            else
                actualAction = desiredAction - 1;
            break;
        }
        case 3: { // turn back
            break;
        }
    }

    return getMovedPos(robotPos, actualAction);
}

std::pair<GridPosition, bool> GwAimaModel::getMovedPos(GridPosition const &position,
                                                       ActionType action) {
    GridPosition movedPos = position;
    switch (action) {
        case ActionType::NORTH:
            movedPos.i -= 1;
            break;
        case ActionType::EAST:
            movedPos.j += 1;
            break;
        case ActionType::SOUTH:
            movedPos.i += 1;
            break;
        case ActionType::WEST:
            movedPos.j -= 1;
            break;
        default:
            std::ostringstream message;
            message << "Invalid action: " << (long) action;
            debug::show_message(message.str());
            break;
    }

    bool wasValid = isValid(movedPos);
    if (!wasValid) {
        movedPos = position;
    }

    return std::make_pair(movedPos, wasValid);
}

bool GwAimaModel::isValid(GridPosition const &position) {
    return (position.i >= 0 && position.i < nRows_ && position.j >= 0
            && position.j < nCols_ && envMap_[position.i][position.j] != TagCellType::WALL);
}

}// namespace gwaima
