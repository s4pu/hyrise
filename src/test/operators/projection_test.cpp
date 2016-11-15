#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

#include "../../lib/operators/abstract_operator.hpp"
#include "../../lib/operators/get_table.hpp"
#include "../../lib/operators/print.hpp"
#include "../../lib/operators/projection.hpp"
#include "../../lib/storage/storage_manager.hpp"
#include "../../lib/storage/table.hpp"
#include "../../lib/types.hpp"
#include "../common.hpp"

namespace opossum {

class OperatorsProjectionTest : public ::testing::Test {
  virtual void SetUp() {
    _test_table = opossum::loadTable("src/test/int_float.tbl", 2);
    opossum::StorageManager::get().add_table("table_a", std::move(_test_table));
    _gt = std::make_shared<opossum::GetTable>("table_a");
  }

  virtual void TearDown() { opossum::StorageManager::get().drop_table("table_a"); }

 public:
  std::shared_ptr<opossum::Table> _test_table;
  std::shared_ptr<opossum::GetTable> _gt;
};

TEST_F(OperatorsProjectionTest, SingleColumn) {
  std::shared_ptr<opossum::Table> test_result = opossum::loadTable("src/test/int.tbl", 1);

  std::vector<std::string> column_filter = {"a"};
  auto projection = std::make_shared<opossum::Projection>(_gt, column_filter);
  projection->execute();

  EXPECT_TRUE(tablesEqual(*(projection->get_output()), *test_result));
}

TEST_F(OperatorsProjectionTest, DoubleProject) {
  std::shared_ptr<opossum::Table> test_result = opossum::loadTable("src/test/int.tbl", 3);

  std::vector<std::string> column_filter = {"a"};
  auto projection1 = std::make_shared<opossum::Projection>(_gt, column_filter);
  projection1->execute();

  auto projection2 = std::make_shared<opossum::Projection>(projection1, column_filter);
  projection2->execute();

  EXPECT_TRUE(tablesEqual(*(projection2->get_output()), *test_result));
}

TEST_F(OperatorsProjectionTest, AllColumns) {
  std::shared_ptr<opossum::Table> test_result = opossum::loadTable("src/test/int_float.tbl", 2);

  std::vector<std::string> column_filter = {"a", "b"};
  auto projection = std::make_shared<opossum::Projection>(_gt, column_filter);
  projection->execute();

  EXPECT_TRUE(tablesEqual(*(projection->get_output()), *test_result));
}

}  // namespace opossum
