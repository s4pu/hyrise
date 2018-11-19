#include "cost_model_calibration_query_runner.hpp"

#include "concurrency/transaction_manager.hpp"
#include "cost_model_feature_extractor.hpp"
#include "feature/calibration_example.hpp"
#include "scheduler/current_scheduler.hpp"
#include "sql/sql_pipeline_builder.hpp"
#include "sql/sql_query_cache.hpp"
#include "utils/format_duration.hpp"

namespace opossum {

CostModelCalibrationQueryRunner::CostModelCalibrationQueryRunner(const CalibrationConfiguration configuration)
    : _configuration(configuration) {}

const std::vector<CalibrationExample> CostModelCalibrationQueryRunner::calibrate_query_from_lqp(
    const std::shared_ptr<AbstractLQPNode>& lqp) const {
  //  TODO(Sven): Print query

  //  TODO(Sven): Do we need an optimizer? PredicateNodes might not be in optimal order..
  // but since we only want to calibrate them, it shouldn't make any difference.
  // This will decrease the overall calibration performance
  //  const auto optimizer = Optimizer::create_default_optimizer();
  //  const auto optimized_lqp = optimizer->optimize(query);
  lqp->print();

  LQPTranslator lqp_translator{};
  const auto operators = lqp_translator.translate_node(lqp);

  const auto query_plan = std::make_shared<SQLQueryPlan>(CleanupTemporaries::No);
  auto context = TransactionManager::get().new_transaction_context();
  query_plan->add_tree_by_root(operators);
  query_plan->set_transaction_context(context);

  const auto& root = query_plan->tree_roots().front();
  const auto tasks = OperatorTask::make_tasks_from_operator(root, CleanupTemporaries::No);

  CurrentScheduler::schedule_and_wait_for_tasks(tasks);

  return _evaluate_query_plan({query_plan});
}

const std::vector<CalibrationExample> CostModelCalibrationQueryRunner::calibrate_query_from_sql(
    const std::string& query) const {
  std::cout << query << std::endl;

  SQLQueryCache<SQLQueryPlan>::get().clear();

  auto pipeline_builder = SQLPipelineBuilder{query};
  pipeline_builder.disable_mvcc();
  pipeline_builder.dont_cleanup_temporaries();
  auto pipeline = pipeline_builder.create_pipeline();

  // Execute the query, we don't care about the results
  pipeline.get_result_table();

  const auto query_plans = pipeline.get_query_plans();
  return _evaluate_query_plan(query_plans);
}

const std::vector<CalibrationExample> CostModelCalibrationQueryRunner::_evaluate_query_plan(
    const std::vector<std::shared_ptr<SQLQueryPlan>>& query_plans) const {
  std::vector<CalibrationExample> examples{};
  for (const auto& query_plan : query_plans) {
    for (const auto& root : query_plan->tree_roots()) {
      _traverse(root, examples);
    }
  }

  return examples;
}

void CostModelCalibrationQueryRunner::_traverse(const std::shared_ptr<const AbstractOperator>& op,
                                                std::vector<CalibrationExample>& examples) const {
  if (op->input_left() != nullptr) {
    _traverse(op->input_left(), examples);
  }

  if (op->input_right() != nullptr) {
    _traverse(op->input_right(), examples);
  }

  auto operator_result = CostModelFeatureExtractor::extract_features(op);
  examples.push_back(operator_result);
}

}  // namespace opossum
