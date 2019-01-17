#include "file_based_table_generator.hpp"

#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <unordered_set>

#include "benchmark_config.hpp"
#include "benchmark_table_encoder.hpp"
#include "import_export/csv_parser.hpp"
#include "operators/import_binary.hpp"
#include "storage/table.hpp"
#include "utils/format_duration.hpp"
#include "utils/load_table.hpp"
#include "utils/timer.hpp"

using namespace std::string_literals;  // NOLINT

namespace opossum {

FileBasedTableGenerator::FileBasedTableGenerator(const std::shared_ptr<BenchmarkConfig>& benchmark_config,
                                                 const std::string& path)
    : AbstractTableGenerator(benchmark_config), _path(path) {}

std::unordered_map<std::string, BenchmarkTableInfo> FileBasedTableGenerator::generate() {
  Assert(std::filesystem::is_directory(_path), "Table path must be a directory");

  auto table_info_by_name = std::unordered_map<std::string, BenchmarkTableInfo>{};
  const auto table_extensions = std::unordered_set<std::string>{".csv", ".tbl", ".bin"};

  /**
   * 1. Explore the directory and identify tables to be loaded
   * Recursively walk through the specified directory and collect all tables found on the way. A tables name is
   * determined by its filename. Multiple file extensions per table are allowed, for example there could be a CSV and a
   * binary version of a table.
   */
  for (const auto& directory_entry : std::filesystem::recursive_directory_iterator(_path)) {
    if (!std::filesystem::is_regular_file(directory_entry)) continue;

    const auto extension = directory_entry.path().extension();

    if (table_extensions.find(extension) == table_extensions.end()) continue;

    auto table_name = directory_entry.path().filename();
    table_name.replace_extension("");

    auto table_info_by_name_iter = table_info_by_name.find(table_name);

    if (table_info_by_name_iter == table_info_by_name.end()) {
      table_info_by_name_iter = table_info_by_name.emplace(table_name, BenchmarkTableInfo{}).first;
    }

    auto& table_info = table_info_by_name_iter->second;

    if (extension == ".bin") {
      Assert(!table_info.binary_file_path, "Multiple binary files found for table '"s + table_name.string() + "'");
      table_info.binary_file_path = directory_entry.path();
    } else {
      Assert(!table_info.text_file_path, "Multiple text files found for table '"s + table_name.string() + "'");
      table_info.text_file_path = directory_entry.path();
    }
  }

  /**
   * 2. Check for "out of date" binary files, i.e., whether both a binary and textual file exists AND the
   *    binary file is older than the textual file.
   */
  for (auto& [table_name, table_info] : table_info_by_name) {
    if (table_info.binary_file_path && table_info.text_file_path) {
      const auto last_binary_write = std::filesystem::last_write_time(*table_info.binary_file_path);
      const auto last_text_write = std::filesystem::last_write_time(*table_info.text_file_path);

      if (last_binary_write < last_text_write) {
        _benchmark_config->out << "- Binary file '" << (*table_info.binary_file_path)
                               << "' is out of date and needs to be re-exported" << std::endl;
        table_info.binary_file_out_of_date = true;
      }
    }
  }

  /**
   * 3. Actually load the tables. Load from binary file if a up-to-date binary file exists for a Table.
   */
  for (auto& [table_name, table_info] : table_info_by_name) {
    Timer timer;

    _benchmark_config->out << "- Loading table '" << table_name << "' ";

    auto text_file_path = std::filesystem::path{*table_info.text_file_path};

    // Pick a source file to load a table from, prefer the binary version
    if (table_info.binary_file_path && !table_info.binary_file_out_of_date) {
      _benchmark_config->out << "from " << *table_info.binary_file_path << std::flush;
      table_info.table = ImportBinary::read_binary(*table_info.binary_file_path);
      table_info.loaded_from_binary = true;
    } else {
      _benchmark_config->out << "from " << text_file_path << std::flush;
      const auto extension = text_file_path.extension();
      if (extension == ".tbl") {
        table_info.table = load_table(text_file_path, _benchmark_config->chunk_size);
      } else if (extension == ".csv") {
        table_info.table = CsvParser{}.parse(text_file_path, std::nullopt, _benchmark_config->chunk_size);
      } else {
        Fail("Unknown textual file format. This should have been caught earlier.");
      }
    }

    _benchmark_config->out << " - Loaded " << table_info.table->row_count() << " rows in "
                           << format_duration(std::chrono::duration_cast<std::chrono::nanoseconds>(timer.lap()))
                           << std::endl;
  }

  return table_info_by_name;
}
}  // namespace opossum