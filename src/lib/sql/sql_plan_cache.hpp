#pragma once

#include <memory>
#include <string>

#include "cache/cache.hpp"
#include "storage/prepared_plan.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"

namespace opossum {

class AbstractOperator;
class AbstractLQPNode;


using SQLPhysicalPlanCache = Cache<std::shared_ptr<AbstractOperator>, std::string>;
using SQLLogicalPlanCache = Cache<std::shared_ptr<PreparedPlan>, size_t>;
}
using cache_types::SQLPhysicalPlanCache;
using cache_types::SQLLogicalPlanCache;
}  // namespace opossum
