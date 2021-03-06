// Created by Petr Karnakov on 30.05.2018
// Copyright 2018 ETH Zurich

#include "distr/distrsolver.h"
#include "kernel/hydro.h"
#include "kernel/kernelmeshpar.h"

template <size_t dim>
void Run(MPI_Comm comm, Vars& var) {
  using M = MeshStructured<double, dim>;
  using K = Hydro<M>;
  using Par = typename K::Par;
  Par par;

  DistrSolver<M, K> ds(comm, var, par);
  ds.Run();
}

void Main(MPI_Comm comm, Vars& var) {
  FORCE_LINK(init_contang);
  FORCE_LINK(init_vel);

  const int dim = var.Int("spacedim", 3);
  switch (dim) {
#if USEFLAG(DIM1)
    case 1:
      Run<1>(comm, var);
      break;
#endif
#if USEFLAG(DIM2)
    case 2:
      Run<2>(comm, var);
      break;
#endif
#if USEFLAG(DIM3)
    case 3:
      Run<3>(comm, var);
      break;
#endif
#if USEFLAG(DIM4)
    case 4:
      Run<4>(comm, var);
      break;
#endif
    default:
      fassert(false, "Unknown dim=" + std::to_string(dim));
  }
}

int main(int argc, const char** argv) {
  return RunMpi(argc, argv, Main);
}
