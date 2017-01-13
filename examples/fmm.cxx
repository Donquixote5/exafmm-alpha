#include "args.h"
#include "bound_box.h"
#include "build_tree.h"
#include "dataset.h"
#include "kernel.h"
#include "logger.h"
#include "traversal.h"
#include "up_down_pass.h"
#include "verify.h"
using namespace exafmm;
vec3 KernelBase::Xperiodic = 0;
real_t KernelBase::eps2 = 0.0;
complex_t KernelBase::wavek = complex_t(10.,1.) / real_t(2 * M_PI);

template<typename Kernel>
void fmm(Args args) {
  typedef typename Kernel::Bodies Bodies;                       //!< Vector of bodies
  typedef typename Kernel::Cells Cells;                         //!< Vector of cells
  typedef typename Kernel::B_iter B_iter;                       //!< Iterator of body vector
  typedef typename Kernel::C_iter C_iter;                       //!< Iterator of cell vector

  const vec3 cycle = 2 * M_PI;
  Bodies bodies, bodies2, jbodies, buffer;
  BoundBox<Kernel> boundBox;
  Bounds bounds;
  BuildTree<Kernel> buildTree(args.ncrit);
  Cells cells, jcells;
  Dataset<Kernel> data;
  Kernel kernel;
  Traversal<Kernel> traversal(args.nspawn, args.images, args.path);
  UpDownPass<Kernel> upDownPass(args.theta);
  Verify<Kernel> verify(args.path);
  num_threads(args.threads);

  Kernel::init();
  verify.verbose = args.verbose;
  logger::verbose = args.verbose;
  logger::path = args.path;
  logger::printTitle("FMM Parameters");
  args.print(logger::stringLength);
  bodies = data.initBodies(args.numBodies, args.distribution, 0);
  buffer.reserve(bodies.size());
  if (args.IneJ) {
    for (B_iter B=bodies.begin(); B!=bodies.end(); B++) {
      B->X[0] += M_PI;
      B->X[0] *= 0.5;
    }
    jbodies = data.initBodies(args.numBodies, args.distribution, 1);
    for (B_iter B=jbodies.begin(); B!=jbodies.end(); B++) {
      B->X[0] -= M_PI;
      B->X[0] *= 0.5;
    }
  }
  bool pass = true;
  bool isTime = false;
  for (int t=0; t<args.repeat; t++) {
    logger::printTitle("FMM Profiling");
    logger::startTimer("Total FMM");
    logger::startPAPI();
    logger::startDAG();
    int numIteration = 1;
    if (isTime) numIteration = 10;
    for (int it=0; it<numIteration; it++) {
      std::stringstream title;
      title << "Time average loop " << it;
      logger::printTitle(title.str());
      bounds = boundBox.getBounds(bodies);
      if (args.IneJ) {
        bounds = boundBox.getBounds(jbodies, bounds);
      }
      cells = buildTree.buildTree(bodies, buffer, bounds);
      upDownPass.upwardPass(cells);
      traversal.initListCount(cells);
      traversal.initWeight(cells);
      if (args.IneJ) {
        jcells = buildTree.buildTree(jbodies, buffer, bounds);
        upDownPass.upwardPass(jcells);
        traversal.traverse(cells, jcells, cycle, args.dual);
      } else {
        traversal.traverse(cells, cells, cycle, args.dual);
        jbodies = bodies;
      }
      upDownPass.downwardPass(cells);
    }
    logger::printTitle("Total runtime");
    logger::stopDAG();
    logger::stopPAPI();
    double totalFMM = logger::stopTimer("Total FMM");
    totalFMM /= numIteration;
    logger::resetTimer("Total FMM");
    if (args.write) {
      logger::writeTime();
    }
    traversal.writeList(cells, 0);

    if (!isTime) {
      const int numTargets = 100;
      buffer = bodies;
      data.sampleBodies(bodies, numTargets);
      bodies2 = bodies;
      data.initTarget(bodies);
      logger::startTimer("Total Direct");
      traversal.direct(bodies, jbodies, cycle);
      traversal.normalize(bodies);
      logger::stopTimer("Total Direct");
      double potDif = verify.getDifScalar(bodies, bodies2);
      double potNrm = verify.getNrmScalar(bodies);
      double accDif = verify.getDifVector(bodies, bodies2);
      double accNrm = verify.getNrmVector(bodies);
      double potRel = std::sqrt(potDif/potNrm);
      double accRel = std::sqrt(accDif/accNrm);
      logger::printTitle("FMM vs. direct");
      verify.print("Rel. L2 Error (pot)",potRel);
      verify.print("Rel. L2 Error (acc)",accRel);
      buildTree.printTreeData(cells);
      traversal.printTraversalData();
      logger::printPAPI();
      bodies = buffer;
      pass = verify.regression(args.getKey(), isTime, t, potRel, accRel);
      if (pass) {
        if (verify.verbose) std::cout << "passed accuracy regression at t: " << t << std::endl;
        if (args.accuracy) break;
        t = -1;
        isTime = true;
      }
    } else {
      pass = verify.regression(args.getKey(), isTime, t, totalFMM);
      if (pass) {
        if (verify.verbose) std::cout << "passed time regression at t: " << t << std::endl;
        break;
      }
    }
    data.initTarget(bodies);
  }
  if (!pass) {
    if (verify.verbose) {
      if(!isTime) std::cout << "failed accuracy regression" << std::endl;
      else std::cout << "failed time regression" << std::endl;
    }
    abort();
  }
  if (args.getMatrix) {
    traversal.writeMatrix(bodies, jbodies);
  }
  logger::writeDAG();
  Kernel::finalize();
}

int main(int argc, char ** argv) {
  Args args(argc, argv);                                        // Argument parser class
  switch (args.equation[0]) {                                   // Case switch for equation
  case 'L':                                                     // Laplace equation
    fmm<LaplaceKernel<Pmax> >(args);                                  //  Call Laplace kernel
    break;                                                      // Break Laplace equation
  case 'H':                                                     // Helmholtz equation
    fmm<HelmholtzKernel<2*Pmax> >(args);                              //  Call Helmholtz kernel
    break;                                                      // Break Helmholtz equation
  case 'B':                                                     // Biot-Savart equation
    fmm<BiotSavartKernel<Pmax> >(args);                               //  Call Biot-Savart kernel
    break;                                                      // Break Biot-Savart equation
  default:                                                      // No matching case
    fprintf(stderr,"No matching equation\n");                   //  Print error message
    abort();                                                    //  Abort execution
  }                                                             // End case switch for equation
  return 0;
}
