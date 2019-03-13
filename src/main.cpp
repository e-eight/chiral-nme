#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include "basis/lsjt_operator.h"
#include "chiral.h"
#include "constants.h"
#include "CLI11.hpp"
#include "charge_radius.h"

int main(int argc, char** argv)
{
  //////////////////////////////////////////////////////////////////////////////
  /////////////// Application description and input parameters /////////////////
  //////////////////////////////////////////////////////////////////////////////

  CLI::App app("Generates CEFT reduced matrix elements in HO basis.");

  // flags
  std::string name = "identity";
  std::string order = "lo";
  double hw = 0;
  int Nmax = 0;
  int Jmax = 0;
  int Tmin = 0;
  int Tmax = 0;

  app.add_option("-n,--name", name, "Name of operator.");
  app.add_option("-o,--order", order, "Chiral order of operator.");
  app.add_option("-E,--hw", hw, "Oscillator energy of basis.");
  app.add_option("-N,--Nmax", Nmax, "Nmax truncation of basis.");
  app.add_option("-J,--Jmax", Jmax, "Jmax truncation of basis.");
  app.add_option("-t,--Tmin", Tmin, "Minimum isospin of basis.");
  app.add_option("-T,--Tmax", Tmax, "Maximum isospin of basis.");
  app.set_config("-c,--config");

  // Parse input
  try
    {
      app.parse(argc, argv);
    }
  catch (const CLI::ParseError &e)
    {
      return app.exit(e);
    }

  //////////////////////////////////////////////////////////////////////////////
  /////////////////// Create chiral operator from input ////////////////////////
  //////////////////////////////////////////////////////////////////////////////

  auto op = chiral::Operator::make(name);

  // Print Header
  std::cout << "\n";
  std::cout << "Generating " << name << " matrix elements..." << "\n";


  //////////////////////////////////////////////////////////////////////////////
  ///////////////////////// Create relative basis //////////////////////////////
  //////////////////////////////////////////////////////////////////////////////

  std::cout << "Beginning RelativeLSJT operator basis setup..." << "\n";

  // Set operator and file header parameters
  basis::OperatorLabelsJT op_labels(op->J0(), op->G0(), Tmin, Tmax,
                                    basis::SymmetryPhaseMode::kHermitian);
  basis::RelativeOperatorParametersLSJT op_params(op_labels, Nmax, Jmax);

  // Set up relative space
  basis::RelativeSpaceLSJT space(op_params.Nmax, op_params.Jmax);

  // Set up operator containers
  // These are arrays to store T0 = 0/1/2 components.
  std::array<basis::RelativeSectorsLSJT, 3> sectors;
  std::array<basis::OperatorBlocks<double>, 3> matrices;

  // Populate operator containers
  basis::ConstructZeroOperatorRelativeLSJT(op_params, space, sectors, matrices);


  //////////////////////////////////////////////////////////////////////////////
  /////////////////////// Generate matrix elements /////////////////////////////
  //////////////////////////////////////////////////////////////////////////////

  // Operator diagnostics
  std::cout << "Truncation:" << " Nmax " << op_params.Nmax
            << " Jmax " << op_params.Jmax
            << " T0_max " << op_params.T0_max << "\n";

  std::cout << "Matrix elements:";
  for (auto T0 = op_params.T0_min; T0 <= op_params.T0_max; ++T0)
    std::cout << " " << basis::UpperTriangularEntries(sectors[T0]);
  std::cout << "\n";
  std::cout << "Allocated:";
  for (auto T0 = op_params.T0_min; T0 <= op_params.T0_max; ++T0)
    std::cout << " " << basis::AllocatedEntries(matrices[T0]);
  std::cout << "\n";

  // Populate matrix elements

  // Oscillator length scale
  const auto hbarc2 = constants::hbarc * constants::hbarc;
  const auto osc_b = std::sqrt(hbarc2 / constants::reduced_nucleon_mass_MeV / hw);
  // Oscillator energy string for filename
  std::ostringstream hw_ostr;
  hw_ostr << hw;
  auto hw_str = hw_ostr.str();

  // Get time for output filename
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::string time_str = std::to_string(time);

  // Temporary matrices for storing matrix elements of each order
  std::array<basis::OperatorBlocks<double>, 3> temp_matrices = matrices;

  // Iterate over chiral orders
  for (auto it = chiral::order_map.begin(); it != chiral::order_map.end(); ++it)
    {
      // Iterate over isospin
      for (auto T0 = op_params.T0_min; T0 <= op_params.T0_max; ++T0)
        {
          // Iterate over sectors
          for (int sector_index = 0; sector_index < sectors[T0].size(); ++sector_index)
            {
              // Get bra and ket subspaces
              const basis::RelativeSectorsLSJT::SectorType& sector =
                sectors[T0].GetSector(sector_index);
              const basis::RelativeSectorsLSJT::SubspaceType& bra_subspace =
                sector.bra_subspace();
              const basis::RelativeSectorsLSJT::SubspaceType& ket_subspace =
                sector.ket_subspace();

              // Get states
              for (int bra_index = 0; bra_index < bra_subspace.size(); ++bra_index)
                {
                  const basis::RelativeStateLSJT bra_state(bra_subspace, bra_index);
                  for (int ket_index = 0; ket_index < ket_subspace.size(); ++ket_index)
                    {
                      const basis::RelativeStateLSJT ket_state(ket_subspace, ket_index);

                      // Calculate matrix element
                      auto order_e = it->second;

                      temp_matrices[T0][sector_index](bra_index, ket_index) =
                        op->ReducedMatrixElement(order_e, bra_state, ket_state, osc_b);

                      matrices[T0][sector_index](bra_index, ket_index) +=
                        op->ReducedMatrixElement(order_e, bra_state, ket_state, osc_b);
                    }
                }
            }

        }
      // Write the contributions at each order
      std::string order_file =
        name + "_2b_rel_" + it->first + "_N" + std::to_string(op_params.Nmax)
        + "_J" + std::to_string(op_params.Jmax) + "_hw" + hw_str
        + "_" + time_str + ".txt";
      basis::WriteRelativeOperatorLSJT(order_file, space, op_labels,
                                       sectors, temp_matrices, true);

      // Break if required order has been reached
      if (it == chiral::order_map.find(order))
        break;
    }

  // Write cumulative contribution
  std::string cumulative_file =
    name + "_2b_rel_" + order + "_cumulative"
    + "_N" + std::to_string(op_params.Nmax)
    + "_J" + std::to_string(op_params.Jmax)
    + "_hw" + hw_str + "_" + time_str + ".txt";
  basis::WriteRelativeOperatorLSJT(cumulative_file, space, op_labels,
                                   sectors, matrices, true);
}