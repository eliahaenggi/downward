#ifndef HEURISTICS_DUAL_HTWO_HEURISTIC_H
#define HEURISTICS_DUAL_HTWO_HEURISTIC_H

#include "../heuristic.h"
#include "../heuristics/htwo_heuristic.h"

#include <unordered_map>



namespace plugins {
class Options;
}

namespace dual_htwo_heuristic {
class DualHTwoHeuristic : public htwo_heuristic::HTwoHeuristic {

protected:
	virtual int compute_heuristic(const State &ancestor_state) override;

	std::shared_ptr<AbstractTask> dual_task;

	std::unordered_map<Pair, int, PairHash> dual_hm_table;


public:
    DualHTwoHeuristic(
        const std::shared_ptr<AbstractTask> &transform,
        bool cache_estimates, const std::string &description,
        utils::Verbosity verbosity);


};
}

#endif
