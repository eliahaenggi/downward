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
 * Constructor for the HTwoHeuristic class.
 */
HTwoHeuristic::HTwoHeuristic(
    const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description,
    utils::Verbosity verbosity)
    : Heuristic(transform, cache_estimates, description, verbosity),
      has_cond_effects(task_properties::has_conditional_effects(task_proxy)),
      goals(task_properties::get_fact_pairs(task_proxy.get_goals())) {
    if (log.is_at_least_normal()) {
        log << "Initializing h^2" << endl;
        log << "The implementation of the h^m heuristic is preliminary." << endl;
    }
    init_operator_caches();
}


/*
 * Computes the h^m value for a given state:
 */
int HTwoHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    if (task_properties::is_goal_state(task_proxy, state)) {
        return 0;
    }
    Tuple state_facts = task_properties::get_fact_pairs(state);
    init_hm_table(state_facts);
    init_operator_queue();
    update_hm_table();
    unordered_set<Pair, PairHash> empty_vector;
    int h = eval(goals, empty_vector);
    if (h == INT_MAX) {
        return DEAD_END;
    }
    return h;
}

/*
 * Initializes h^m table.
 * If entry is contained in init state facts assigns 0, and infinity otherwise.
 * Pair containing variable -1 at second position indicates single fact.
 */
void HTwoHeuristic::init_hm_table(const std::vector<FactPair> &state_facts) {
    unordered_set<FactPair, FactPairHash> state_facts_set(state_facts.begin(), state_facts.end());
    state_facts_set.insert(FactPair(-1, -1));

    int num_variables = task_proxy.get_variables().size();
    for (int i = 0; i < num_variables; ++i) {
        int domain1_size = task_proxy.get_variables()[i].get_domain_size();
        for (int j = 0; j < domain1_size; ++j) {
            Pair single_pair(FactPair(i, j), FactPair(-1, -1));
            hm_table[single_pair] = check_in_initial_state(single_pair, state_facts_set);

            for (int k = i + 1; k < num_variables; ++k) {
                int domain2_size = task_proxy.get_variables()[k].get_domain_size();
                for (int l = 0; l < domain2_size; ++l) {
                    Pair pair(FactPair(i, j), FactPair(k, l));
                    hm_table[pair] = check_in_initial_state(pair, state_facts_set);
                }
            }
        }
    }
}

/*
 * Check if Pair is contained in inital state facts. Unordered set allows constant time lookup.
 */
int HTwoHeuristic::check_in_initial_state(
    const Pair &hm_entry, const std::unordered_set<FactPair, FactPairHash> &state_facts_set) const {
    bool found_first = state_facts_set.find(hm_entry.first) != state_facts_set.end();
    bool found_second = (state_facts_set.find(hm_entry.second) != state_facts_set.end());
    return (found_first && found_second) ? 0 : INT_MAX;
}

/**
* Sets up all auxiliary data structures concerning operators.
*/
void HTwoHeuristic::init_operator_caches() {
  	vector<int> empty_pre_op = {};
    for (auto op : task_proxy.get_operators()) {
        if (op.get_preconditions().empty()) {
            empty_pre_op.push_back(op.get_id());
        }
    }
    int num_variables = task_proxy.get_variables().size();
    for (int i = 0; i < num_variables; ++i) {
        int domain1_size = task_proxy.get_variables()[i].get_domain_size();
        for (int j = 0; j < domain1_size; ++j) {
			op_dict[FactPair(i, j)] = empty_pre_op;
        }
    }
    precondition_cache = {};
    partial_effect_cache = {};
    contradictions_cache.assign(task_proxy.get_operators().size(), std::vector<bool>(task_proxy.get_variables().size(), false));
    op_cost.assign(task_proxy.get_operators().size(), INT_MAX);

	for (OperatorProxy op : task_proxy.get_operators()) {
        // Setup precondition cache
        Tuple preconditions = task_properties::get_fact_pairs(op.get_preconditions());
    	sort(preconditions.begin(), preconditions.end());
    	precondition_cache.push_back(preconditions);
        // Setup op_dict
        for (auto pre : preconditions) {
        	op_dict[pre].push_back(op.get_id());
        }

        // Check for operators without preconditions -> automatically add to op_dict
        if (preconditions.empty()) {
            empty_pre_op.push_back(op.get_id());
        }

        // Setup partial effect cache
    	Tuple effects;
    	for (EffectProxy eff : op.get_effects()) {
        	effects.push_back(eff.get_fact().get_pair());
            contradictions_cache[op.get_id()][eff.get_fact().get_pair().var] = true;
    	}
    	sort(effects.begin(), effects.end());
        vector<Pair> partial_effects = generate_all_pairs(effects);
		partial_effect_cache.push_back(partial_effects);
    }
}

void HTwoHeuristic::init_operator_queue() {
  	op_cost.assign(task_proxy.get_operators().size(), INT_MAX);
    critical_entries.assign(task_proxy.get_operators().size(), unordered_set<Pair, PairHash>());
	for (OperatorProxy op : task_proxy.get_operators()) {
    	// Initialize operator queue with applicable operators
        if (is_op_applicable(op.get_id())) {
            op_queue.push_back(op.get_id());
            is_op_in_queue.insert(op.get_id());
            op_cost[op.get_id()] = 0;
        }

    	unordered_set<Pair, PairHash> filtered_entries;
    	for (auto entry : generate_all_pairs(precondition_cache[op.get_id()])) {
        	if (hm_table.at(entry) != 0) {
            	filtered_entries.insert(entry);
        	}
        }
    	critical_entries[op.get_id()] = filtered_entries;
    }
}

/*
 * Check if op is applicable in initial state. Only works for initial state as it only considers single atom table entries.
 */
bool HTwoHeuristic::is_op_applicable(int op_id) const {
  	const vector<FactPair> &pre = precondition_cache[op_id];
	for (auto fact : pre) {
    	if (hm_table.at(Pair(fact, FactPair(-1, -1))) != 0) {
        	return false;
        }
    }
    return true;
}


/*
 * Updates hm_table until no further improvements are made.
 */
void HTwoHeuristic::update_hm_table() {
     while (!op_queue.empty()) {
         OperatorProxy op = task_proxy.get_operators()[op_queue.front()];
         op_queue.pop_front();
         is_op_in_queue.erase(op.get_id());

         int c1 = op_cost[op.get_id()];
         if (c1 == INT_MAX) {
             continue;
         }
         for (Pair &partial_eff : partial_effect_cache[op.get_id()]) {
             update_hm_entry(partial_eff, c1 + op.get_cost());

             if (partial_eff.second.var == -1) {
                 extend_tuple(partial_eff.first, op, c1);
             }
         }
     }
}


/*
 * Extends given partial effect by adding additional fact.
 */
void HTwoHeuristic::extend_tuple(const FactPair &f, const OperatorProxy &op, int eval) {
	Tuple pre = precondition_cache[op.get_id()];
    int num_variables = task_proxy.get_variables().size();
    for (int i = 0; i < num_variables; ++i) {
        if (contradictions_cache[op.get_id()][i]) {
        	continue;
        }
    	for (int j = 0; j < task_proxy.get_variables()[i].get_domain_size(); ++j) {
        	FactPair extend_fact = FactPair(i, j);
            // Check if extend_fact is reachable
            if (hm_table.at(Pair(extend_fact, FactPair(-1, -1))) == INT_MAX) {
            	continue;
            }
            Pair hm_pair = f.var > extend_fact.var ? Pair(extend_fact, f) : Pair(f, extend_fact);
            // Check if table entry can be updated with current op (without extend_Fact considered)
            if (hm_table.at(hm_pair) <= eval) {
            	continue;
            }
        	int c2 = extend_eval(extend_fact, pre, eval);
        	if (c2 != INT_MAX) {
            	update_hm_entry(hm_pair, c2 + op.get_cost());
        	}
        }
    }
}

/*
 * Evaluates tuple by computing the maximum heuristic value among all its partial tuples. Used for pre(op) and goal.
 */
int HTwoHeuristic::eval(const Tuple &t, unordered_set<Pair, PairHash>& critical_entries) const {
    vector<Pair> pairs = generate_all_pairs(t);
    int max = 0;
    critical_entries.clear();
    for (Pair &pair : pairs) {
        int h = hm_table.at(pair);
        if (h > max) {
            if (h == INT_MAX) {
                critical_entries.clear();
                return INT_MAX;
            }
            max = h;
            critical_entries.clear();
            critical_entries.insert(pair);
        } else if (h == max) {
            critical_entries.insert(pair);
        }
    }
    return max;
}

/*
 * Evaluates extend_fact + pre. pre already evaluated with eval.
 */
int HTwoHeuristic::extend_eval(const FactPair &extend_fact, const Tuple &pre, int eval) const {
    int fact_eval = hm_table.at(Pair(extend_fact, FactPair(-1, -1)));
    int max = eval > fact_eval ? eval : fact_eval;
    for (FactPair fact0 : pre) {
      	if (fact0.var == extend_fact.var) {
            // Check if preconditions contradict extend_fact
          	if (fact0.value != extend_fact.value) {
                  return INT_MAX;
          	}
            // extend_fact ∈ pre
        	continue;
        }
        Pair key = (fact0.var < extend_fact.var) ? Pair(fact0, extend_fact) : Pair(extend_fact, fact0);
        int h = hm_table.at(key);

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
 * Adds operators to the queue if f is precondition and was updated.
 */
void HTwoHeuristic::add_operator_to_queue(const Pair &p, int val) {
    std::vector<int> operator_ids = op_dict[p.first];
    //log << p.first << " " << p.second << " = " << val << endl;

    for (int op_id : operator_ids) {
        auto it = critical_entries[op_id].find(p);
        if (it != critical_entries[op_id].end()) {
            critical_entries[op_id].erase(it);
            unordered_set<Pair, PairHash> new_critical_entries;
            //log << "Critical entries " << critical_entries[op_id].size() << endl;
            if (critical_entries[op_id].empty()) {
                op_cost[op_id] = eval(precondition_cache[op_id], new_critical_entries);
				//log << "Op " << op_id << " new eval " << op_cost[op_id] << " with " << new_critical_entries.size() << " critical entries" << endl;
                critical_entries[op_id] = new_critical_entries;
            }
        }
        if (is_op_in_queue.find(op_id) == is_op_in_queue.end()) {
            op_queue.push_back(op_id);
            is_op_in_queue.insert(op_id);
        }
    }

    if (p.second.var == -1) {
        return;
    }
    operator_ids = op_dict[p.second];
    for (int op_id : operator_ids) {
        if (is_op_in_queue.find(op_id) == is_op_in_queue.end()) {
            op_queue.push_back(op_id);
            is_op_in_queue.insert(op_id);
        }
    }
}

/*
 * Updates heuristic value of a pair in hm_table.
 * Affected operators are added to queue.
 */
int HTwoHeuristic::update_hm_entry(const Pair &p, int val) {
    if (hm_table[p] > val) {
        hm_table[p] = val;
        add_operator_to_queue(p, val);
        return val;
    }
    return -1;
}

/*
 * Generates all partial set of size <= 2 from given base tuple.
 */
vector<HTwoHeuristic::Pair> HTwoHeuristic::generate_all_pairs(const Tuple &base_tuple) const {
	vector<Pair> res;
    res.reserve(base_tuple.size() * (base_tuple.size() + 1) / 2);

    for (size_t i = 0; i < base_tuple.size(); ++i) {
        res.emplace_back(base_tuple[i], FactPair(-1, -1));

        for (size_t j = i + 1; j < base_tuple.size(); ++j) {
            res.emplace_back(base_tuple[i], base_tuple[j]);
        }
    }
    return res;
}

void HTwoHeuristic::print_table() const {
    stringstream ss;
    for (auto entry : hm_table) {
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
            get_heuristic_arguments_from_options(opts)
            );
    }
};

static plugins::FeaturePlugin<HTwoHeuristicFeature> _plugin;
}
