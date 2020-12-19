#include "phonon_transport_app.h"
#include "bandstructure.h"
#include "context.h"
#include "drift.h"
#include "exceptions.h"
#include "full_points.h"
#include "ifc3_parser.h"
#include "observable.h"
#include "ph_scattering.h"
#include "phonon_thermal_cond.h"
#include "phonon_viscosity.h"
#include "qe_input_parser.h"
#include "specific_heat.h"
#include "wigner_phonon_thermal_cond.h"
#include <iomanip>

void PhononTransportApp::run(Context &context) {

  // Read the necessary input files

  auto tup = QEParser::parsePhHarmonic(context);
  auto crystal = std::get<0>(tup);
  auto phononH0 = std::get<1>(tup);

  // first we make compute the band structure on the fine grid

  FullPoints fullPoints(crystal, context.getQMesh());

  if (mpi->mpiHead()) {
    std::cout << "\nConstructing the band structure" << std::endl;
  }
  auto tup1 = ActiveBandStructure::builder(context, phononH0, fullPoints);
  auto bandStructure = std::get<0>(tup1);
  auto statisticsSweep = std::get<1>(tup1);
  if (mpi->mpiHead()) {
    std::cout << "Band structure done!\n" << std::endl;
  }

  // load the 3phonon coupling
  auto coupling3Ph = IFC3Parser::parse(context, crystal);

  // build/initialize the scattering matrix and the smearing
  PhScatteringMatrix scatteringMatrix(context, statisticsSweep, bandStructure,
                                      bandStructure, &coupling3Ph, &phononH0);
  scatteringMatrix.setup();

  // solve the BTE at the relaxation time approximation level
  // we always do this, as it's the cheapest solver and is required to know
  // the diagonal for the exact method.

  if (mpi->mpiHead()) {
    std::cout << "\n" << std::string(80, '-') << "\n\n"
              << "Solving BTE within the relaxation time approximation."
              << std::endl;
  }

  // compute the phonon populations in the relaxation time approximation.
  // Note: this is the total phonon population n (n != f(1+f) Delta n)

  auto dimensionality = context.getDimensionality();
  BulkTDrift drift(statisticsSweep, bandStructure, dimensionality);
  VectorBTE phononRelTimes = scatteringMatrix.getSingleModeTimes();
  VectorBTE popRTA = drift * phononRelTimes;

  // output relaxation times
  scatteringMatrix.outputToJSON("rta_ph_relaxation_times.json");

  // compute the thermal conductivity
  PhononThermalConductivity phTCond(context, statisticsSweep, crystal,
                                    bandStructure);
  phTCond.calcFromPopulation(popRTA);
  phTCond.print();
  phTCond.outputToJSON("rta_phonon_thermal_cond.json");

  // compute the Wigner thermal conductivity
  WignerPhononThermalConductivity phTCondWigner(
      context, statisticsSweep, crystal, bandStructure, phononRelTimes);
  phTCondWigner.calcFromPopulation(popRTA);
  phTCondWigner.print();
  phTCond.outputToJSON("wigner_phonon_thermal_cond.json");

  // compute the thermal conductivity
  PhononViscosity phViscosity(context, statisticsSweep, crystal, bandStructure);
  phViscosity.calcRTA(phononRelTimes);
  phViscosity.print();
  phViscosity.outputToJSON("rta_phonon_viscosity.json");

  // compute the specific heat
  SpecificHeat specificHeat(context, statisticsSweep, crystal, bandStructure);
  specificHeat.calc();
  specificHeat.print();
  specificHeat.outputToJSON("specific_heat.json");

  if (mpi->mpiHead()) {
    std::cout << "\n" << std::string(80, '-') << "\n" << std::endl;
  }

  // if requested, we solve the BTE exactly

  std::vector<std::string> solverBTE = context.getSolverBTE();

  bool doIterative = false;
  bool doVariational = false;
  bool doRelaxons = false;
  for (auto s : solverBTE) {
    if (s.compare("iterative") == 0)
      doIterative = true;
    if (s.compare("variational") == 0)
      doVariational = true;
    if (s.compare("relaxons") == 0)
      doRelaxons = true;
  }

  // here we do validation of the input, to check for consistency
  if (doRelaxons && !context.getScatteringMatrixInMemory()) {
    Error e("Relaxons require matrix kept in memory");
  }
  if (context.getScatteringMatrixInMemory() &&
      statisticsSweep.getNumCalcs() != 1) {
    Error e("If scattering matrix is kept in memory, only one "
            "temperature/chemical potential is allowed in a run");
  }

  mpi->barrier();

  if (doIterative) {

    if (mpi->mpiHead()) {
      std::cout << "Starting Omini Sparavigna BTE solver\n" << std::endl;
    }

    // initialize the (old) thermal conductivity
    PhononThermalConductivity phTCondOld = phTCond;

    VectorBTE fNext(statisticsSweep, bandStructure, dimensionality);
    VectorBTE sMatrixDiagonal = scatteringMatrix.diagonal();

    // from n, we get f, such that n = bose(bose+1)f
    VectorBTE fRTA = popRTA;
    fRTA.population2Canonical();
    VectorBTE fOld = fRTA;

    auto threshold = context.getConvergenceThresholdBTE();

    for (int iter = 0; iter < context.getMaxIterationsBTE(); iter++) {

      fNext = scatteringMatrix.offDiagonalDot(fOld) / sMatrixDiagonal;
      fNext = fRTA - fNext;

      phTCond.calcFromCanonicalPopulation(fNext);
      phTCond.print(iter);

      // this exit condition must be improved
      // different temperatures might converge differently
      auto diff = phTCond - phTCondOld;
      if (diff.getNorm().maxCoeff() < threshold) {
        break;
      } else {
        phTCondOld = phTCond;
        fOld = fNext;
      }

      if (iter == context.getMaxIterationsBTE() - 1) {
        Error e("Reached max BTE iterations without convergence");
      }
    }
    phTCond.print();
    phTCond.outputToJSON("omini_phonon_thermal_cond.json");

    if (mpi->mpiHead()) {
      std::cout << "Finished Omini Sparavigna BTE solver\n\n";
      std::cout << std::string(80, '-') << "\n" << std::endl;
    }
  }

  if (doVariational) {
    if (mpi->mpiHead()) {
      std::cout << "Starting variational BTE solver\n" << std::endl;
    }

    // note: each iteration should take approximately twice as long as
    // the iterative method above (in the way it's written here.

    // initialize the (old) thermal conductivity
    PhononThermalConductivity phTCondOld = phTCond;

    // load the conjugate gradient rescaling factor
    VectorBTE sMatrixDiagonalSqrt = scatteringMatrix.diagonal().sqrt();
    VectorBTE sMatrixDiagonal = scatteringMatrix.diagonal();

    // set the initial guess to the RTA solution
    VectorBTE fNew = popRTA;
    // from n, we get f, such that n = bose(bose+1)f
    fNew.population2Canonical();
    // CG rescaling
    fNew = fNew * sMatrixDiagonalSqrt; // CG scaling

    // save the population of the previous step
    VectorBTE fOld = fNew;

    // do the conjugate gradient method for thermal conductivity.
    //		auto gOld = scatteringMatrix.dot(fNew) - fOld;
    auto gOld = scatteringMatrix.dot(fNew);
    gOld = gOld / sMatrixDiagonal; // CG scaling
    gOld = gOld - fOld;
    auto hOld = -gOld;

    auto tOld = scatteringMatrix.dot(hOld);
    tOld = tOld / sMatrixDiagonal; // CG scaling

    double threshold = context.getConvergenceThresholdBTE();

    for (int iter = 0; iter < context.getMaxIterationsBTE(); iter++) {
      // execute CG step, as in

      Eigen::MatrixXd alpha = (gOld.dot(hOld)).array() / hOld.dot(tOld).array();
      fNew = hOld * alpha;
      fNew = fOld - fNew;
      VectorBTE gNew = tOld * alpha;
      gNew = gOld - gNew;

      Eigen::MatrixXd beta = // (numCalcs,3)
          (gNew.dot(gNew)).array() / (gOld.dot(gOld)).array();
      VectorBTE hNew = hOld * beta;
      hNew = -gNew + hNew;

      std::vector<VectorBTE> inVectors;
      inVectors.push_back(fNew);
      inVectors.push_back(hNew); // note: at next step hNew is hOld -> gives tOld
      std::vector<VectorBTE> outVectors = scatteringMatrix.dot(inVectors);
      tOld = outVectors[1];
      tOld = tOld / sMatrixDiagonal; // CG scaling

      phTCond.calcVariational(outVectors[0], fNew, sMatrixDiagonalSqrt);
      phTCond.print(iter);

      // decide whether to exit or run the next iteration
      auto diff = phTCond - phTCondOld;
      if (diff.getNorm().maxCoeff() < threshold) {
        break;
      } else {
        phTCondOld = phTCond;
        fOld = fNew;
        gOld = gNew;
        hOld = hNew;
      }

      if (iter == context.getMaxIterationsBTE() - 1) {
        Error e("Reached max BTE iterations without convergence");
      }
    }

    // nice formatting of the thermal conductivity at the last step
    phTCond.print();
    phTCond.outputToJSON("variational_phonon_thermal_cond.json");

    if (mpi->mpiHead()) {
      std::cout << "Finished variational BTE solver\n\n";
      std::cout << std::string(80, '-') << "\n" << std::endl;
    }
  }

  if (doRelaxons) {
    if (mpi->mpiHead()) {
      std::cout << "Starting relaxons BTE solver" << std::endl;
    }
    scatteringMatrix.a2Omega();
    auto tup2 = scatteringMatrix.diagonalize();
    auto eigenvalues = std::get<0>(tup2);
    auto eigenvectors = std::get<1>(tup2);
    // EV such that Omega = V D V^-1
    // eigenvectors(phonon index, eigenvalue index)

    phTCond.calcFromRelaxons(context, statisticsSweep, eigenvectors,
                             scatteringMatrix, eigenvalues);
    phTCond.print();
    phTCond.outputToJSON("relaxons_phonon_thermal_cond.json");
    // output relaxation times
    scatteringMatrix.outputToJSON("relaxons_relaxation_times.json");

    if (!context.getUseSymmetries()) {
      Vector0 boseEigenvector(statisticsSweep, bandStructure, specificHeat);
      phViscosity.calcFromRelaxons(boseEigenvector, eigenvalues,
                                   scatteringMatrix, eigenvectors);
      phViscosity.print();
      phViscosity.outputToJSON("relaxons_phonon_viscosity.json");
    }

    if (mpi->mpiHead()) {
      std::cout << "Finished relaxons BTE solver\n\n";
      std::cout << std::string(80, '-') << "\n" << std::endl;
    }
  }
  mpi->barrier();
}

void PhononTransportApp::checkRequirements(Context &context) {
  throwErrorIfUnset(context.getPhD2FileName(), "PhD2FileName");
  throwErrorIfUnset(context.getQMesh(), "qMesh");
  throwWarningIfUnset(context.getSumRuleD2(), "sumRuleD2");
  throwErrorIfUnset(context.getPhD3FileName(), "PhD3FileName");
  throwErrorIfUnset(context.getTemperatures(), "temperatures");
  throwErrorIfUnset(context.getSmearingMethod(), "smearingMethod");
  throwErrorIfUnset(context.getSmearingWidth(), "smearingWidth");
}
