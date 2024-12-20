#include "htwo_heuristic.h"

#include "../plugins/plugin.h"

#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>
#include <climits>
#include <set>

using namespace std;

namespace htwo_heuristic {
/*
 * Constructor for the HMHeuristic class.
 * Precomputes all possible tuples of size <= m.
 */
HTwoHeuristic::HTwoHeuristic(
    int m, const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description,
    utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      m(m),
      has_cond_effects(task_properties::has_conditional_effects(task_proxy)),
      goals(task_properties::get_fact_pairs(task_proxy.get_goals())) {
    if (log.is_at_least_normal()) {
        log << "Using h^" << m << "." << endl;
        log << "The implementation of the h^m heuristic is preliminary." << endl
            << "It is rather slow." << endl
            << "Please do not use this for comparison!" << endl;
    }
}


bool HTwoHeuristic::dead_ends_are_reliable() const {
    return !task_properties::has_axioms(task_proxy) && !has_cond_effects;
}


/*
 * Computes the h^m value for a given state:
 * Checks if state is a goal state (heuristic = 0 if true). Initializes h^m table with state facts.
 * Updates h^m table to propagate values. Evaluates goal facts to compute the heuristic value.
 */
int HTwoHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    if (task_properties::is_goal_state(task_proxy, state)) {
        return 0;
    }
    Tuple state_facts = task_properties::get_fact_pairs(state);

    init_hm_table(state_facts);
    init_partial_effects();
    update_hm_table();
    int h = eval(goals);
    if (h == INT_MAX) {
        return DEAD_END;
    }
    return h;
}

/*
 * Initializes h^m table.
 * If tuple is contained in input tuple assigns 0, and infinity otherwise.
 */
void HTwoHeuristic::init_hm_table(const Tuple &state_facts) {
	int num_variables = task_proxy.get_variables().size();
    for (int i = 0; i < num_variables; ++i) {
      	int domain1_size = task_proxy.get_variables()[i].get_domain_size();
    	for (int j = 0; j < domain1_size; ++j) {
            Pair single_pair(FactPair(i, j), FactPair(-1, -1));
            hm_table[single_pair] = check_in_initial_state(single_pair, state_facts) ? 0 : INT_MAX;
            for (int k = i + 1; k < num_variables; ++k) {
            	int domain2_size = task_proxy.get_variables()[k].get_domain_size();
            	for (int l = 0; l < domain2_size; ++l) {
                  Pair pair(FactPair(i, j), FactPair(k, l));
                  hm_table[pair] = check_in_initial_state(pair, state_facts) ? 0 : INT_MAX;
            	}
            }
    	}
    }
}
/**
* Generates partial effects (size <= 2) of all operators and saves them sorted in a map.
*/
void HTwoHeuristic::init_partial_effects() {
	for (OperatorProxy op : task_proxy.get_operators()) {
		Tuple eff = get_operator_eff(op);
		vector<Pair> partial_effs;
		generate_all_partial_tuples(eff, partial_effs);
		partial_effect_cache[op.get_id()] = partial_effs;
    }
}


/*
 * Iteratively updates the h^m table until no further improvements are made.
 */
void HTwoHeuristic::update_hm_table() {
    do {
        //print_table();
        was_updated = false;
        //log << endl << "New Iteration looü" << endl;
        for (OperatorProxy op : task_proxy.get_operators()) {
            //log << "Operator " << op.get_id() << "with pre: (" << get_operator_pre(op) << "),  eff: (" << get_operator_eff(op) << ")" << endl;
            Tuple pre = get_operator_pre(op);
            int c1 = eval(pre);
            if (c1 == INT_MAX) {
            	continue;
            }
            vector<Pair> partial_effs = partial_effect_cache[op.get_id()];
            for (Pair &partial_eff : partial_effs) {
                //if (c1 + op.get_cost() < hm_table[partial_eff]) {
                    //log << "Eff Update: ([" << partial_eff.first.var << "=" << partial_eff.first.value << "," << partial_eff.second.var << "=" << partial_eff.second.value << "]) = " << c1 << " + " << op.get_cost() << endl;
                //}
                update_hm_entry(partial_eff, c1 + op.get_cost());

                if (partial_eff.second.var == -1) {
                    //log << "Extend Tuple with [" << partial_eff.first.var << "=" << partial_eff.first.value << "]" << endl;
                    extend_tuple(partial_eff, op, c1);
                }
            }
        }
    } while (was_updated);
}


/*
 * Extends given partial effect by adding additional fact.
 */
void HTwoHeuristic::extend_tuple(const Pair &p, const OperatorProxy &op, int eval) {
	Tuple pre = get_operator_pre(op);
    for (const auto &hm_ent : hm_table) {
        const Pair &hm_pair = hm_ent.first;

        FactPair fact = FactPair(-1, -1);
        if (p.first == hm_pair.first) {
            fact = hm_pair.second;
        } else if (p.first == hm_pair.second) {
            fact = hm_pair.first;
        } else {
        	continue;
        }

        if (contradict_effect_of(op, fact)) {
            continue;
        }

        if (hm_pair.second.var == -1) {
            continue;
        }

        int c2 = hm_table_evaluation(pre, fact, eval);
        if (c2 != INT_MAX) {
            //if (c2 + op.get_cost() < hm_table[hm_pair]) {
                //log << "Ext Update: ([" << hm_pair.first.var << "=" << hm_pair.first.value << "," << hm_pair.second.var << "=" << hm_pair.second.value << "]) = " << c2 << " + " << op.get_cost() << endl;
            //}
            update_hm_entry(hm_pair, c2 + op.get_cost());
        }
    }
}

/*
 * Evaluates tuple by computing the maximum heuristic value among all its partial tuples.
 */
int HTwoHeuristic::eval(const Tuple &t) const {
    vector<Pair> partial;
    generate_all_partial_tuples(t, partial);
    int max = 0;
    for (Pair &pair : partial) {
        int h = hm_table.at(pair);

        if (h > max) {
        	if (h == INT_MAX) {
                  return INT_MAX;
            }
        	max = h;
        }
    }
    return max;
}

// Evaluates (t + fact). t-evaluation already given with eval.
int HTwoHeuristic::hm_table_evaluation(const Tuple &t, const FactPair &fact, int eval) const {
    int fact_eval = hm_table.at(pair(fact, FactPair(-1, -1)));
    int max = eval > fact_eval ? eval : fact_eval;
    for (FactPair fact0 : t) {
      	if (fact0.var == fact.var) {
          	if (fact0.value != fact.value) {
                  return INT_MAX;
          	}
        	continue;
        }
      	int h = 0;
        pair key = (fact0.var < fact.var) ? pair(fact0, fact) : pair(fact, fact0);
        h = hm_table.at(key);

        if (h > max) {
        	if (h == INT_MAX) {
                  return INT_MAX;
            }
        	max = h;
        }
    }
    return max;
}

/*
 * Updates the heuristic value of a tuple in the h^m table.
 * Sets "was_updated" flag to true to indicate a change occurred.
 */
int HTwoHeuristic::update_hm_entry(const Pair &p, int val) {
    if (hm_table[p] > val) {
        hm_table[p] = val;
        was_updated = true;
    }
    return val;
}

bool HTwoHeuristic::check_in_initial_state(
    const Pair &hm_entry, const Tuple &state_facts) const {
    bool found_first = false;
    bool found_second = hm_entry.second.var == -1;
    for (auto &fact : state_facts) {
        if (!found_first && fact == hm_entry.first) {
            found_first = true;
        } else if (!found_second && fact == hm_entry.second) {
            found_second = true;
        }
        if (found_first && found_second) {
            return true;
        }
    }
    return false;
}


HTwoHeuristic::Tuple HTwoHeuristic::get_operator_pre(const OperatorProxy &op) const {
    int op_id = op.get_id();

    auto it = precondition_cache.find(op_id);
    if (it != precondition_cache.end()) {
        return it->second;
    }

    Tuple preconditions = task_properties::get_fact_pairs(op.get_preconditions());
    std::sort(preconditions.begin(), preconditions.end());
    precondition_cache[op_id] = preconditions;

    return preconditions;
}


HTwoHeuristic::Tuple HTwoHeuristic::get_operator_eff(const OperatorProxy &op) const {
    Tuple effects;
    for (EffectProxy eff : op.get_effects()) {
        effects.push_back(eff.get_fact().get_pair());
    }
    sort(effects.begin(), effects.end());
    return effects;
}


bool HTwoHeuristic::contradict_effect_of(
    const OperatorProxy &op, FactPair fact) const {
    for (EffectProxy eff : op.get_effects()) {
        FactProxy eff_fact = eff.get_fact();
        if (eff_fact.get_variable().get_id() == fact.var && eff_fact.get_value() != fact.value) {
            return true;
        }
    }
    return false;
}

/*
 * Generates all partial tuples of size <= m from given base tuple.
 */
void HTwoHeuristic::generate_all_partial_tuples(
    const Tuple &base_tuple, vector<Pair> &res) const {
    res.reserve(base_tuple.size() * (base_tuple.size() + 1) / 2);

    for (size_t i = 0; i < base_tuple.size(); ++i) {
        res.emplace_back(base_tuple[i], FactPair(-1, -1));

        for (size_t j = i + 1; j < base_tuple.size(); ++j) {
            res.emplace_back(base_tuple[i], base_tuple[j]);
        }
    }
}

void HTwoHeuristic::print_table() const {
    stringstream ss;
    for (auto entry : hm_table) {
      	if (entry.second == INT_MAX) {
          continue;
      	}
        Pair pair = entry.first;
        ss << "[" << pair.first.var << " = " << pair.first.value << ", " << pair.second.var << " = " << pair.second.value << "] = " << entry.second << endl;
    }
    log << ss.str() << endl;
}

class HTwoHeuristicFeature
    : public plugins::TypedFeature<Evaluator, HTwoHeuristic> {
public:
    HTwoHeuristicFeature() : TypedFeature("h2") {
        document_title("h^2 heuristic");

        add_option<int>("m", "subset size", "2", plugins::Bounds("1", "infinity"));
        add_heuristic_options_to_feature(*this, "h2");

        document_language_support("action costs", "supported");
        document_language_support("conditional effects", "ignored");
        document_language_support("axioms", "ignored");

        document_property(
            "admissible",
            "yes for tasks without conditional effects or axioms");
        document_property(
            "consistent",
            "yes for tasks without conditional effects or axioms");
        document_property(
            "safe",
            "yes for tasks without conditional effects or axioms");
        document_property("preferred operators", "no");
    }

    virtual shared_ptr<HTwoHeuristic> create_component(
        const plugins::Options &opts,
        const utils::Context &) const override {
        return plugins::make_shared_from_arg_tuples<HTwoHeuristic>(
            opts.get<int>("m"),
            get_heuristic_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<HTwoHeuristicFeature> _plugin;
}
