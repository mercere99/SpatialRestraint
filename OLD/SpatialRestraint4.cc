/**
 *  @copyright Copyright (C) Michigan State University, MIT Software license; see doc/LICENSE.md
 *  @date 2020.
 *
 *  @file  SpatialRestraint4.h
 *  @brief Examining the evolution of cellular restraint in a multi-cell
 *  @note Status: BETA
 *
 *  Cells are either RESTRAINED or UNRESTRAINED
 *  Organisms are "multi-cells" that start as a single cell and automatically replicate when full.
 *  Cells replicate within the multicell when they have collected enough resource.
 *  - Restrained cells replicate into an empty neighbor position (or fail if no empty)
 *  - Unrestrained cells replicate into a random position, regardless of if anything is there.
 */

#include <iostream>
#include <fstream>
#include <set>

#include "base/vector.h"
#include "config/command_line.h"
#include "config/SettingCombos.h"
#include "tools/BitVector.h"
#include "tools/Distribution.h"
#include "tools/Random.h"
#include "tools/string_utils.h"
#include "tools/vector_utils.h"

/// Information about a single cell.
struct Cell {
  size_t id;
  double repro_time = 0.0;  ///< When will this cell replicate?
  size_t num_ones = 0;      ///< How many ones in genome?

  bool operator==(const Cell & _in) const { return id == _in.id; }
  bool operator!=(const Cell & _in) const { return id != _in.id; }
  bool operator<(const Cell & _in) const {
    if (repro_time == _in.repro_time) return (id < _in.id);
    return (repro_time < _in.repro_time);
  }
};

/// Results from a single run.
struct Results {
  double run_time;                  ///< What was the replication time of this group?
  emp::vector<double> cell_counts;  ///< How many cells have each bit count?

  Results(const size_t num_bits) : run_time(0.0), cell_counts(num_bits+1, 0.0) { ; }
  Results(const Results &) = default;
  Results(Results &&) = default;

  Results & operator+=(const Results & in) {
    emp_assert(cell_counts.size() == in.cell_counts.size());
    run_time += in.run_time;
    for (size_t i=0; i < cell_counts.size(); i++) cell_counts[i] += in.cell_counts[i];
    return *this;
  }

  Results & operator/=(const double denom) {
    run_time /= denom;
    for (double & x : cell_counts) x /= denom;
    return *this;
  }

  /// Count the total number of cells represented.
  double CountCells() const { return emp::Sum(cell_counts); }

  /// Count the number of cells that exhibit restrained behavior.
  double CountRestrained(size_t threshold) const {
    double total = 0.0;
    for (size_t i = threshold; i < cell_counts.size(); i++) total += cell_counts[i];
    return total;
  }

  /// Count the number of cells that DO NOT exhibit restrained behavior.
  double CountUnrestrained(size_t threshold) const {
    double total = 0.0;
    for (size_t i = 0; i < threshold; i++) total += cell_counts[i];
    return total;
  }
};

struct World {
  emp::Random random;
  emp::SettingCombos combos;
  emp::vector<Cell> cells;
  double time = 0.0;        ///< Current time in multicell
  size_t num_cells = 0;     ///< How many cells are currnetly in the multicell?
  std::string exe_name;     ///< Name of executable used to start this run.
  size_t mask_side = 31;    ///< Bit mask for a side (for id -> x pos)
  size_t log2_side = 5;     ///< Log base 2 of the number of cells on a side (for id -> y pos).

  emp::vector<Cell> cell_queue;  ///< Cells waiting to replicate.
  emp::vector<Cell> cell_buffer; ///< Unsorted cells

  size_t cells_side = 32;   ///< How many cells are on a side of the (square) multi-cell?
  size_t time_range = 1.0;  ///< Replication takes 100.0 + a random value up to time_range.
  size_t neighbors = 8;     ///< Num neighbors to consider for offspring (0=well mixed; 4,6,8 => 2D)
  size_t genome_size = 10;  ///< How many bits in genome?
  size_t birth_tries = 1;   ///< How many attempts should restrained cells make to find empty cell?
  size_t restrain = 5;      ///< How many ones in bit sequence for restraint?
  size_t start_1s = 5;      ///< How many ones in the starting cell?
  double mut_prob = 0.0;    ///< Probability of an offspring being mutated.
  bool print_reps = false;  ///< Should we print results for every replicate?
  bool print_trace = false; ///< Should we show each step of a multicell?

  World(int argc, char* argv[]) {
    emp::vector<std::string> args = emp::cl::args_to_strings(argc, argv);
    exe_name = args[0];

    combos.AddSetting("time_range", "Rep time = 100.0 + random(time_range)", 't', time_range) = { 50 };
    combos.AddSetting("neighbors",  "Neighborhood size for replication", 'n', neighbors) = { 8 };
    combos.AddSetting("cells_side", "Cells on side of (square) multicell", 'c', cells_side) = { 16 };
    combos.AddSetting("genome_size","Number of bits in genome?", 'g', genome_size) = { 10 };
    combos.AddSetting("birth_tries","Restrained attempts to find empty cell", 'b', birth_tries) = { 1 };
    combos.AddSetting("restrain",   "Num ones in genome for restraint?", 'r', restrain) = { 5 };
    combos.AddSetting("initial_1s", "How many 1s in starting cell?", 'i', start_1s) = { 5 };
    combos.AddSetting("mut_prob",   "Probability of mutation in offspring", 'm', mut_prob) = { 0.0 };
    combos.AddSetting<size_t>("data_count", "Number of times to replicate each run", 'd') = { 100 };

    combos.AddAction("help", "Print full list of options", 'h',
                     [this](){
                       combos.PrintHelp(exe_name, " -n 0,4,8 -r 0,1 -t 4,8,16,32 -d 100");
                       exit(1);
                      } );
    combos.AddAction("print_reps", "Should we print timings for each replicates?", 'P',
                     [this](){ print_reps = true; } );
    combos.AddAction("trace", "Should we show each step of a multicell?", 'T',
                     [this](){ print_trace = true; } );

    // Process the command-line options
    args = combos.ProcessOptions(args);

    // Fail if there are any unknown args.
    if (args.size()) {
      std::cerr << "ERROR: Unknown options: " << emp::to_string(args) << "\n";
      exit(2);
    }
  }

  size_t GetSize() const { return cells_side * cells_side; }

  size_t ToPos(size_t x, size_t y) const { return x + y * cells_side; }
  // size_t ToX(size_t pos) const { return pos % cells_side; }
  // size_t ToY(size_t pos) const { return pos / cells_side; }
  size_t ToX(size_t pos) const { return pos & mask_side; }
  size_t ToY(size_t pos) const { return pos >> log2_side; }

  // Convert a resource count to a character.
  static constexpr char ToChar(size_t count) {
    if (count < 10) return '0' + count;
    if (count < 36) return 'a' + (count-10);
    if (count < 62) return 'A' + (count-36);
    return '+';
  }

  // Neighborhood layout:
  //  7 2 4
  //  0 * 1
  //  5 3 6
  //
  // Thus 0-1 is a 1D size 2 neighborhood; 0-3 are a 2D size-4 neighborhood; 0-7 is a 2D size 8
  // neighborhood.  (0-5 behaves like a hex-map) Larger assumes popoulation size and returns the
  // full set.

  size_t RandomNeighbor(size_t pos)
  {
    if (neighbors == 0 || neighbors > 8) {
      return random.GetUInt(GetSize());
    }

    const size_t x = ToX(pos);
    const size_t y = ToY(pos);
    size_t next_x = (size_t) -1;
    size_t next_y = (size_t) -1;

    while (next_x >= cells_side || next_y >= cells_side) {
      const size_t dir = random.GetUInt(neighbors);  // Direction for offspring.
      switch (dir) {
      case 0: case 5: case 7: next_x = x-1; break;
      case 1: case 4: case 6: next_x = x+1; break;
      default: next_x = x;
      };
      switch (dir) {
      case 2: case 4: case 7: next_y = y-1; break;
      case 3: case 5: case 6: next_y = y+1; break;
      default: next_y = y;
      };
    }

    emp_assert(ToPos(next_x, next_y) < GetSize(), next_x, next_y, cells_side);

    return ToPos(next_x, next_y);
  }

  // Print current one-counts in population.
  void Print() {
    emp_assert(cells.size() == GetSize());
    size_t pos = 0;
    for (size_t y = 0; y < cells_side; y++) {
      for (size_t x = 0; x < cells_side; x++) {
        if (cells[pos].repro_time == 0.0) std::cout << " -";
      	else std::cout << " " << ToChar(cells[pos].num_ones);
        pos++;
      }
      std::cout << std::endl;
    }
  }

  // If cell queue is empty, pull more from buffer.
  bool UpdateCellQueue() {
    cell_queue.resize(0);
    if (cell_buffer.size() == 0) return false;  // No cells left!

    // First scan the buffer to determine the earliest time.
    double first_time = cell_buffer[0].repro_time;
    for (const Cell & x : cell_buffer) {
      if (x.repro_time < first_time) first_time = x.repro_time;
    }

    // Move all cells that will replicate in the next 100 timesteps to cell_queue.
    double last_time = first_time + 100.0;
    size_t keep_count = 0;
    for (size_t i = 0; i < cell_buffer.size(); i++) {
      if (cell_buffer[i].repro_time <= last_time) cell_queue.push_back(cell_buffer[i]);
      else cell_buffer[keep_count++] = cell_buffer[i];
    }
    cell_buffer.resize(keep_count);

    // Sort the cell queue so that it's ready to go.
    emp::Sort(cell_queue);

    return true;
  }

  void SetupCell(size_t id) {
    Cell & cell = cells[id];
    cell.repro_time = time + 100.0 + random.GetDouble(time_range);
    cell_buffer.push_back(cell);
  }

  // Setup the new offspring, possibly with mutations.
  void DoBirth(Cell & offspring, const Cell & parent, bool do_mutations=true) {
    if (offspring.repro_time == 0.0) num_cells++;  // If offspring was empty, this is a new cell.
    offspring.num_ones = parent.num_ones;
    if (do_mutations && random.P(mut_prob)) {
      double prob1 = ((double) offspring.num_ones) / (double) genome_size;
      if (random.P(prob1)) offspring.num_ones--;
      else offspring.num_ones++;
    }

    // And launch it in the population.
    SetupCell(offspring.id);
  }

  void Debug() {
    std::cout << "DEBUG: ... cell_buffer=[";
    for (auto x : cell_buffer) { std::cout << " " << x.id << "(" << x.repro_time << ")"; }
    std::cout << "]  cell_queue=[";
    for (auto x : cell_queue) { std::cout << " " << x.id << "(" << x.repro_time << ")"; }
    std::cout << "]\n";
    std::cout << "           cells: ";
    for (size_t pos=0; pos < cells.size(); pos++) {
      if (cells[pos].repro_time) {
        std::cout << pos << " (" << cells[pos].repro_time << ")";
      }
    }
    std::cout << std::endl;
  }

  /// Once we have current settings locked in, reset all non-setting values appropriately.
  void SetupConfig() {
    // Setup initial multicell to be empty; keep count of resources in each cell.
    cells.resize(0);         // Clear out any current cells.
    cells.resize(GetSize());   // Put in new cells initialized to 0.
    for (size_t id = 0; id < cells.size(); id++) {
      cells[id].id = id;
      cells[id].repro_time = 0.0;
    }
    cell_buffer.resize(0);
    cell_queue.resize(0);
    time = 0.0;

    if (emp::count_bits(cells_side) != 1) {
      std::cerr << "\nERROR: Cannot have " << cells_side << "cells on a side; must be a power of 2!\n";
      exit(1);
    }
    mask_side = cells_side - 1;
    log2_side = emp::count_bits(mask_side);
  }

  Results TestMulticell() {
    SetupConfig();

    // Inject a cell in the middle.
    const size_t start_pos = ToPos(cells_side/2, cells_side/2);
    Cell & inject_cell = cells[start_pos];   // Find the cell position to inject.
    inject_cell.num_ones = start_1s;         // Initialize injection to proper default;
    SetupCell(start_pos);                    // Do any extra setup for this cell.
    num_cells = 1;

    // Loop through updates until cell is full.
    size_t last_count = 0;                   // Track cells from last time (for traces)
    while (num_cells < cells.size()) {
      emp_assert(cell_buffer.size()+cell_queue.size() > 0);

      // Loop through all cells in the queue.      
      for (Cell & parent : cell_queue) {
        // If this cell has been updated since being bufferred, skip it.
        if (parent.repro_time != cells[parent.id].repro_time) continue;

        time = parent.repro_time;                   // Update time to when replication occurs.
        SetupCell(parent.id);                          // Reset parent for next replication.
        size_t next_id = RandomNeighbor(parent.id); // Find the placement of the offspring.
        Cell & next_cell = cells[next_id];

        // If the target is empty or we don't restrain, put a new cell there.
        if (next_cell.repro_time == 0.0 || parent.num_ones < restrain) {
          DoBirth(next_cell, parent);
        }

        // Otherwise it is restrained and not empty; keep looking?
        else {
          for (size_t i = 1; i < birth_tries; i++) {
            next_id = RandomNeighbor(parent.id);
            if (cells[next_id].repro_time == 0.0) {
              DoBirth(cells[next_id], parent);
              break;
            }
          }
        }

        // If we are tracing, output data.
        if (print_trace && last_count != num_cells) {
          last_count = num_cells;
          std::cout << "\nTime: " << time
                    << "  Cells: " << last_count
                    << "\n";
         Print();
        }
      }

      // Refill queue before the next loop.
      UpdateCellQueue();
    }

    // Setup the results and return them.
    Results results(genome_size);
    results.run_time = time;
    for (const auto & cell : cells) results.cell_counts[cell.num_ones] += 1.0;

    return results;
  }

  // Run all of the configurations in an entire set.
  void Run(std::ostream & os=std::cout) {
    const size_t num_runs = combos.GetValue<size_t>("data_count");

    // Print column headers.
    os << combos.GetHeaders();
    if (print_reps) {
      for (size_t i=0; i < num_runs; i++) os << ", run" << i;
    }
    os << ", ave_time, frac_restrain" << std::endl;

    // Loop through configuration combonations to test.
    combos.Reset();
    do {
      os << combos.CurString(", ");  // Output current setting combination data.

      // Conduct all replicates and output the information.    
      Results total_results(genome_size);
      for (size_t i = 0; i < num_runs; i++) {
        Results cur_results = TestMulticell();
        if (print_reps) os << ", " << cur_results.run_time;
        total_results += cur_results;
      }
      total_results /= (double) num_runs;
      os << ", " << total_results.run_time
         << ", " << (total_results.CountRestrained(restrain) / (double) GetSize())
         << std::endl;
    } while (combos.Next());
  }
};

int main(int argc, char* argv[])
{
  World world(argc, argv);
  world.Run(std::cout);
}
