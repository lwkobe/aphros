// Created by Petr Karnakov on 30.07.2018
// Copyright 2018 ETH Zurich

#include <cmath>
#include <memory>

#include "approx.h"
#include "approx_eb.h"
#include "debug/isnan.h"
#include "geom/mesh.h"
#include "normal.h"
#include "solver.h"
#include "util/height.h"

#if USEFLAG(AVX)
#include "util/avx.h"
#endif

template <class M_>
struct UNormal<M_>::Imp {
  static constexpr size_t dim = M::dim;

  static auto Maxmod(Scal a, Scal b) -> Scal {
    return std::abs(b) < std::abs(a) ? a : b;
  }

  // Computes normal by gradient.
  // uc: volume fraction
  // mfc: boundary conditions for volume fraction
  // Output: modified in cells with msk=1
  // fcn: normal [s]
  static void CalcNormalGrad(
      M& m, const FieldCell<Scal>& uc, const MapEmbed<BCond<Scal>>& mfc,
      FieldCell<Vect>& fcn) {
    auto gc = UEmbed<M>::AverageGradient(UEmbed<M>::Gradient(uc, mfc, m), m);
    for (auto c : m.SuCells()) {
      fcn[c] = gc[c];
    }
  }
  // Computes normal by Youngs' scheme (interpolation of gradient from nodes).
  // fcu: volume fraction
  // fci: interface mask (1: contains interface)
  // Output: modified in cells with fci=1, resized to m
  // fcn: normal with norm1()=1, antigradient of fcu [s]
  // XXX: uses static variables, not suspendable
  // TODO: check non-uniform mesh
  static void CalcNormalYoungs(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci,
      FieldCell<Vect>& fcn) {
    FieldNode<Vect> fng(m, Vect(0));
    for (auto c : m.AllCells()) {
      for (size_t q = 0; q < m.GetNumNodes(c); ++q) {
        const IdxNode n = m.GetNode(c, q);
        for (size_t d = 0; d < dim; ++d) {
          fng[n][d] += ((q >> d) % 2 == 0 ? 1 : -1) * fcu[c];
        }
      }
    }
    fcn.Reinit(m);
    for (auto c : m.SuCells()) {
      if (fci[c]) {
        Vect v(0);
        for (size_t q = 0; q < m.GetNumNodes(c); ++q) {
          v += fng[m.GetNode(c, q)];
        }
        fcn[c] = -v / v.norm1();
      }
    }
  }
  // CalcNormalYoungs: optimized implementation
  static void CalcNormalYoungs1(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci,
      FieldCell<Vect>& fcn) {
    using MIdx = typename M::MIdx;
    auto ic = m.GetIndexCells();
    auto bc = m.GetSuBlockCells();
    MIdx s = ic.GetSize();
    const size_t nx = s[0];
    const size_t ny = s[1];
    // offset
    const size_t fx = 1;
    const size_t fy = nx;
    const size_t fz = ny * nx;

    // index range
    const MIdx wb = bc.GetBegin() - ic.GetBegin();
    const MIdx we = bc.GetEnd() - ic.GetBegin();
    const size_t xb = wb[0], yb = wb[1], zb = wb[2];
    const size_t xe = we[0], ye = we[1], ze = we[2];

    fcn.Reinit(m);

    const Scal* pu = fcu.data();
    Vect* pn = fcn.data();
    const bool* pi = fci.data();
    for (size_t z = zb; z < ze; ++z) {
      for (size_t y = yb; y < ye; ++y) {
        for (size_t x = xb; x < xe; ++x) {
          size_t i = (z * ny + y) * nx + x;
          if (!pi[i]) {
            continue;
          }
          auto q = [i, fy, fz, pu](int dx, int dy, int dz) {
            size_t ii = i + dx * fx + dy * fy + dz * fz;
            return pu[ii];
          };
          // generated by gen/normal.py
          pn[i][0] =
              (-q(-1, -1, -1) - 2 * q(-1, -1, 0) - q(-1, -1, 1) -
               2 * q(-1, 0, -1) - 4 * q(-1, 0, 0) - 2 * q(-1, 0, 1) -
               q(-1, 1, -1) - 2 * q(-1, 1, 0) - q(-1, 1, 1) + q(1, -1, -1) +
               2 * q(1, -1, 0) + q(1, -1, 1) + 2 * q(1, 0, -1) +
               4 * q(1, 0, 0) + 2 * q(1, 0, 1) + q(1, 1, -1) + 2 * q(1, 1, 0) +
               q(1, 1, 1));
          pn[i][1] =
              (-q(-1, -1, -1) - 2 * q(-1, -1, 0) - q(-1, -1, 1) + q(-1, 1, -1) +
               2 * q(-1, 1, 0) + q(-1, 1, 1) - 2 * q(0, -1, -1) -
               4 * q(0, -1, 0) - 2 * q(0, -1, 1) + 2 * q(0, 1, -1) +
               4 * q(0, 1, 0) + 2 * q(0, 1, 1) - q(1, -1, -1) -
               2 * q(1, -1, 0) - q(1, -1, 1) + q(1, 1, -1) + 2 * q(1, 1, 0) +
               q(1, 1, 1));
          pn[i][2] =
              (-q(-1, -1, -1) + q(-1, -1, 1) - 2 * q(-1, 0, -1) +
               2 * q(-1, 0, 1) - q(-1, 1, -1) + q(-1, 1, 1) - 2 * q(0, -1, -1) +
               2 * q(0, -1, 1) - 4 * q(0, 0, -1) + 4 * q(0, 0, 1) -
               2 * q(0, 1, -1) + 2 * q(0, 1, 1) - q(1, -1, -1) + q(1, -1, 1) -
               2 * q(1, 0, -1) + 2 * q(1, 0, 1) - q(1, 1, -1) + q(1, 1, 1));

          pn[i] /= -pn[i].norm1();
        }
      }
    }
  }
#if USEFLAG(AVX)
  static void CalcNormalYoungsAvx(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci,
      FieldCell<Vect>& fcn) {
    (void) fci;
    fcn.Reinit(m, Vect(0));

    const __m256d c2 = _mm256_set1_pd(2);
    const __m256d c4 = _mm256_set1_pd(4);
    const auto& stencil = m.GetStencilOffsets();
    for (auto c : m.SuCells4()) {
      auto q = [c, &fcu, &stencil](int dx, int dy, int dz) -> const Scal* {
        return &fcu[c + stencil[(dx + 1) + (dy + 1) * 3 + (dz + 1) * 3 * 3]];
      };
      __m256d vx = _mm256_setzero_pd();
      __m256d vy = _mm256_setzero_pd();
      __m256d vz = _mm256_setzero_pd();
      auto add = [&vx, &vy, &vz, &q](const __m256d& k, int dx, int dy, int dz) {
        vx = _mm256_fmadd_pd(k, _mm256_loadu_pd(q(dx, dy, dz)), vx);
        vy = _mm256_fmadd_pd(k, _mm256_loadu_pd(q(dz, dx, dy)), vy);
        vz = _mm256_fmadd_pd(k, _mm256_loadu_pd(q(dy, dz, dx)), vz);
      };
      auto sub = [&vx, &vy, &vz, &q](const __m256d& k, int dx, int dy, int dz) {
        vx = _mm256_fnmadd_pd(k, _mm256_loadu_pd(q(dx, dy, dz)), vx);
        vy = _mm256_fnmadd_pd(k, _mm256_loadu_pd(q(dz, dx, dy)), vy);
        vz = _mm256_fnmadd_pd(k, _mm256_loadu_pd(q(dy, dz, dx)), vz);
      };
      auto diff = [&add, &sub](const __m256d& k, int dy, int dz) {
        add(k, +1, dy, dz);
        sub(k, -1, dy, dz);
      };
      auto diff1 = [&vx, &vy, &vz, &q](int dy, int dz) {
        vx = _mm256_add_pd(vx, _mm256_loadu_pd(q(+1, dy, dz)));
        vx = _mm256_sub_pd(vx, _mm256_loadu_pd(q(-1, dy, dz)));
        vy = _mm256_add_pd(vy, _mm256_loadu_pd(q(dz, +1, dy)));
        vy = _mm256_sub_pd(vy, _mm256_loadu_pd(q(dz, -1, dy)));
        vz = _mm256_add_pd(vz, _mm256_loadu_pd(q(dy, dz, +1)));
        vz = _mm256_sub_pd(vz, _mm256_loadu_pd(q(dy, dz, -1)));
      };
      diff1(+1, +1);
      diff1(+1, -1);
      diff1(-1, +1);
      diff1(-1, -1);
      diff(c2, +0, +1);
      diff(c2, +0, -1);
      diff(c2, +1, +0);
      diff(c2, -1, +0);
      diff(c4, +0, +0);

      util::Soa::Normalize1(vx, vy, vz);
      util::Soa::StoreAsAos(vx, vy, vz, (Scal*)(&fcn[c]));
    }
  }
#endif
  // Computes normal and curvature from height functions.
  // fcu: volume fraction
  // fci: interface mask (1: contains interface)
  // edim: effective dimension
  // ow: 1: force overwrite, 0: update only if gives steeper profile
  // Output: modified in cells with fci=1, resized to m
  // fcn: normal, antigradient of fcu, if gives steeper profile or ow=1 [s]
  static void CalcNormalHeight(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci, size_t edim,
      bool force_overwrite, FieldCell<Vect>& fcn) {
    fcn.Reinit(m);

    using Direction = typename M::Direction;
    for (auto c : m.SuCellsM()) {
      if (!fci[c]) {
        continue;
      }
      Vect best_n(0);
      size_t best_dz(0);
      for (size_t ddz : {0, 1, 2}) {
        if (ddz >= edim) {
          continue;
        }
        const Direction dz(ddz);
        const auto dx = dz >> 1;
        const auto dy = dz >> 2;

        auto hh = [&](Direction d) {
          return fcu[c + d - dz] + fcu[c + d] + fcu[c + d + dz];
        };

        Vect n;
        n[dx] = hh(dx) - hh(-dx);
        n[dy] = hh(dy) - hh(-dy);
        n[dz] = (fcu[c + dz] - fcu[c - dz] > 0 ? 2 : -2);
        n /= -n.norm1();
        if (std::abs(best_n[best_dz]) < std::abs(n[dz])) {
          best_n = n;
          best_dz = dz;
        }
      }

      if (force_overwrite ||
          std::abs(best_n[best_dz]) < std::abs(fcn[c][best_dz])) {
        fcn[c] = best_n;
      }
    }
  }
  // Computes heights.
  // S: the number of stages for stencils, column size is [-S*2,S*2]
  // fcu: volume fraction [a]
  // fcud2: volume fraction difference double (xpp-xmm, ypp-ymm, zpp-zmm) [a]
  // fcud4: volume fraction difference triple (xp4-xm4, ...) [a]
  // Output:
  // fch: fch[c][d] is absolute position of the interface
  // from a column in direction d starting from an interfacial cell c
  // otherwise, NaN
  template <size_t S>
  static void CalcHeight(
      M& m, const FieldCell<Scal>& fcu,
      const std::array<const FieldCell<Vect>*, S> vfcud, size_t edim,
      FieldCell<Vect>& fch) {
    auto I = [](Scal a) { return a > 0 && a < 1; }; // interface

    fch.Reinit(m, GetNan<Vect>());

    for (auto c : m.Cells()) {
      if (!I(fcu[c])) {
        continue;
      }
      for (size_t d = 0; d < edim; ++d) {
        IdxCell cm = m.GetCell(c, 2 * d);
        IdxCell cmm = m.GetCell(cm, 2 * d);
        IdxCell cp = m.GetCell(c, 2 * d + 1);
        IdxCell cpp = m.GetCell(cp, 2 * d + 1);

        const size_t si = (S + 1) * 4 + 1;
        const size_t sih = (S + 1) * 2;

        std::array<Scal, si> uu;

        uu[sih] = fcu[c];
        uu[sih - 1] = fcu[cm];
        uu[sih + 1] = fcu[cp];
        uu[sih - 2] = fcu[cmm];
        uu[sih + 2] = fcu[cpp];

        for (size_t s = 0; s < S; ++s) {
          const size_t q = (s + 1) * 2; // difference field half-interval
          const size_t ia = q + 1;
          const size_t ib = q + 2;
          const FieldCell<Vect>& fcud = *vfcud[s];

          uu[sih - ia] = uu[sih - ia + 2 * q] - fcud[cm][d];
          uu[sih + ia] = uu[sih + ia - 2 * q] + fcud[cp][d];
          uu[sih - ib] = uu[sih - ib + 2 * q] - fcud[cmm][d];
          uu[sih + ib] = uu[sih + ib - 2 * q] + fcud[cpp][d];
        }

        // |cm6|cm5|cm4|cm3|cmm| cm| c |cp |cpp|cp3|cp4|cp5|cp6|
        // |   |   |   |   | * |   | c |   | * |   |   |   |   |
        // |   |   | * |   |   |   | c |   |   |   | * |   |   |

        Scal s = UHeight<Scal>::Good(uu);
        fch[c][d] = m.GetCenter(c)[d] + s * m.GetCellSize()[d];
      }
    }
  }
  // Computes curvature from height functions.
  // fcu: volume fraction
  // fcn: normal, antigradient of fcu
  // edim: effective dimension
  // Output: modified in cells with fci=1, resized to m
  // fck: curvature [i]
  static void CalcCurvHeight(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<Vect>& fch,
      const FieldCell<Vect>& fcn, size_t edim, FieldCell<Scal>& fck) {
    using MIdx = typename M::MIdx;
    using Dir = typename M::Dir;
    auto& bc = m.GetIndexCells();

    auto I = [](Scal a) { return a > 0 && a < 1; }; // interface

    (void)edim;

    fck.Reinit(m, GetNan<Scal>());

    for (auto c : m.Cells()) {
      if (!I(fcu[c])) {
        continue;
      }

      size_t di = fcn[c].abs().argmax(); // best direction index
      Dir dn(di); // best direction
      // directions of plane tangents ([d]irection [t]angents)
      Dir dtx((size_t(dn) + 1) % dim);
      Dir dty((size_t(dn) + 2) % dim);

      MIdx w = bc.GetMIdx(c);

      // offset in normal direction
      MIdx on = MIdx(dn);
      // offset in dtx,dty
      MIdx otx = MIdx(dtx);
      MIdx oty = MIdx(dty);
      // mesh step
      const Scal lx = m.GetCellSize()[size_t(dtx)];
      const Scal ly = m.GetCellSize()[size_t(dty)];

      // Evaluates height function from nearest interface
      // o: offset from w
      auto hh = [I, &w, &fch, &di, &bc, &on, &fcu](MIdx o) -> Scal {
        const int si = 5; // stencil size
        const int sih = si / 2;
        int i = sih; // closest interface to center
        while (i < si) {
          auto cn = bc.GetIdx(w + o + on * (i - sih));
          if (I(fcu[cn])) {
            return fch[cn][di];
          }
          if (i > sih) {
            i = si - i - 1;
          } else {
            i = si - i;
          }
        }
        return GetNan<Scal>();
      };

      // height function
      const Scal hcc = hh(MIdx(0));
      const Scal hmc = hh(-otx);
      const Scal hpc = hh(otx);
      const Scal hcm = hh(-oty);
      const Scal hcp = hh(oty);
      // corners: hxy
      const Scal hmm = hh(-otx - oty);
      const Scal hmp = hh(-otx + oty);
      const Scal hpm = hh(otx - oty);
      const Scal hpp = hh(otx + oty);

      // first derivative (slope)
      Scal hx = (hpc - hmc) / (2. * lx); // centered
      Scal hy = (hcp - hcm) / (2. * ly);
      // second derivative
      const Scal fl = 0.2; // filter factor (Basilisk: fl=0.2)
      Scal hxx = ((hpm - 2. * hcm + hmm) * fl + (hpc - 2. * hcc + hmc) +
                  (hpp - 2. * hcp + hmp) * fl) /
                 ((1 + 2 * fl) * lx * lx);
      Scal hyy = ((hmp - 2. * hmc + hmm) * fl + (hcp - 2. * hcc + hcm) +
                  (hpp - 2. * hpc + hpm) * fl) /
                 ((1 + 2 * fl) * ly * ly);
      Scal hxy = ((hpp - hmp) - (hpm - hmm)) / (4. * lx * ly);
      // curvature
      Scal k =
          (2. * hx * hy * hxy - (sqr(hy) + 1.) * hxx - (sqr(hx) + 1.) * hyy) /
          std::pow(sqr(hx) + sqr(hy) + 1., 3. / 2.);

      if (fcn[c][di] < 0) {
        k *= -1;
      }

      // curvature
      fck[c] = k;
    }
  }
  static void GetNormal(
      const Scal* u, Vect& pn, size_t edim, bool force_overwrite,
      const int s[3]) {
    Vect best_n(0);
    size_t best_dz = 0;
    for (size_t dz : {0, 1, 2}) {
      if (dz >= edim) {
        continue;
      }
      const size_t dx = (dz + 1) % dim;
      const size_t dy = (dz + 2) % dim;

      auto hh = [&](int ss) { return u[ss - s[dz]] + u[ss] + u[ss + s[dz]]; };

      Vect n;
      n[dx] = hh(s[dx]) - hh(-s[dx]);
      n[dy] = hh(s[dy]) - hh(-s[dy]);
      n[dz] = (u[s[dz]] - u[-s[dz]] > 0 ? 2 : -2);
      n /= -n.norm1();
      if (std::abs(best_n[best_dz]) < std::abs(n[dz])) {
        best_n = n;
        best_dz = dz;
      }
    }

    if (force_overwrite || std::abs(best_n[best_dz]) < std::abs(pn[best_dz])) {
      pn = best_n;
    }
  }
  // CalcNormalHeight: optimized implementation
  static void CalcNormalHeight1(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci, size_t edim,
      bool force_overwrite, FieldCell<Vect>& fcn) {
    using MIdx = typename M::MIdx;
    const auto indexc = m.GetIndexCells();
    const auto blockc_su = m.GetSuBlockCells();
    const auto s = indexc.GetSize();
    const int nx = s[0];
    const int ny = s[1];
    const int offset[] = {1, nx, ny * nx};

    const MIdx wb = blockc_su.GetBegin() - indexc.GetBegin();
    const MIdx we = blockc_su.GetEnd() - indexc.GetBegin();

    fcn.Reinit(m);

    const Scal* pu = fcu.data();
    Vect* pn = fcn.data();
    const bool* pi = fci.data();
    for (int z = wb[2]; z < we[2]; ++z) {
      for (int y = wb[1]; y < we[1]; ++y) {
        for (int x = wb[0]; x < we[0]; ++x) {
          const int i = (z * ny + y) * nx + x;
          if (!pi[i]) {
            continue;
          }
          GetNormal(&pu[i], pn[i], edim, force_overwrite, offset);
        }
      }
    }
  }
  // Computes normal by combined Young's scheme and height-functions
  // fcu: volume fraction
  // fci: interface mask (1: contains interface)
  // edim: effective dimension
  // Output: set to NaN if fci=0
  // fcn: normal with norm1()=1, antigradient of fcu [s]
  static void CalcNormal(
      M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci, size_t edim,
      FieldCell<Vect>& fcn) {
    fcn.Reinit(m, Vect(GetNan<Scal>()));
    FieldCell<char> fcd(m);
#if USEFLAG(AVX)
    CalcNormalYoungsAvx(m, fcu, fci, fcn);
#else
    CalcNormalYoungs1(m, fcu, fci, fcn);
#endif
    CalcNormalHeight1(m, fcu, fci, edim, false, fcn);
  }

  // u: volume fraction, array of size 3x3x3
  static Vect GetNormalYoungs(const std::array<Scal, 27>& u) {
    auto q = [&u](int dx, int dy, int dz) {
      const int w = 3; // stencil width
      int i = (dx + 1) + (dy + 1) * w + (dz + 1) * w * w;
      return u[i];
    };

    Vect n;
    // generated by gen/normal.py
    n[0] = (1.0 / 32.0) *
           (-q(-1, -1, -1) - 2 * q(-1, -1, 0) - q(-1, -1, 1) -
            2 * q(-1, 0, -1) - 4 * q(-1, 0, 0) - 2 * q(-1, 0, 1) -
            q(-1, 1, -1) - 2 * q(-1, 1, 0) - q(-1, 1, 1) + q(1, -1, -1) +
            2 * q(1, -1, 0) + q(1, -1, 1) + 2 * q(1, 0, -1) + 4 * q(1, 0, 0) +
            2 * q(1, 0, 1) + q(1, 1, -1) + 2 * q(1, 1, 0) + q(1, 1, 1));
    n[1] = (1.0 / 32.0) *
           (-q(-1, -1, -1) - 2 * q(-1, -1, 0) - q(-1, -1, 1) + q(-1, 1, -1) +
            2 * q(-1, 1, 0) + q(-1, 1, 1) - 2 * q(0, -1, -1) - 4 * q(0, -1, 0) -
            2 * q(0, -1, 1) + 2 * q(0, 1, -1) + 4 * q(0, 1, 0) +
            2 * q(0, 1, 1) - q(1, -1, -1) - 2 * q(1, -1, 0) - q(1, -1, 1) +
            q(1, 1, -1) + 2 * q(1, 1, 0) + q(1, 1, 1));
    n[2] = (1.0 / 32.0) *
           (-q(-1, -1, -1) + q(-1, -1, 1) - 2 * q(-1, 0, -1) + 2 * q(-1, 0, 1) -
            q(-1, 1, -1) + q(-1, 1, 1) - 2 * q(0, -1, -1) + 2 * q(0, -1, 1) -
            4 * q(0, 0, -1) + 4 * q(0, 0, 1) - 2 * q(0, 1, -1) +
            2 * q(0, 1, 1) - q(1, -1, -1) + q(1, -1, 1) - 2 * q(1, 0, -1) +
            2 * q(1, 0, 1) - q(1, 1, -1) + q(1, 1, 1));

    n /= -n.norm1();
    return n;
  }
  // u: volume fraction, array of size 3x3x3
  // pn: guess for normal, updated if heights give steeper estimate
  static void GetNormalHeight(const std::array<Scal, 27>& u, Vect& n) {
    const int w = 3;
    const int offset[] = {1, w, w * w};
    GetNormal(&u[1 + w + w * w], n, 3, false, offset);
  }
};

template <class M_>
void UNormal<M_>::CalcNormal(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci, size_t edim,
    FieldCell<Vect>& fcn) {
  Imp::CalcNormal(m, fcu, fci, edim, fcn);
}

template <class M_>
void UNormal<M_>::CalcHeight(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<Vect>& fcdu2, size_t edim,
    FieldCell<Vect>& fch) {
  std::array<const FieldCell<Vect>*, 1> vfcdu = {&fcdu2};
  Imp::CalcHeight(m, fcu, vfcdu, edim, fch);
}

template <class M_>
void UNormal<M_>::CalcHeight(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<Vect>& fcdu2,
    const FieldCell<Vect>& fcdu4, size_t edim, FieldCell<Vect>& fch) {
  std::array<const FieldCell<Vect>*, 2> vfcdu = {&fcdu2, &fcdu4};
  Imp::CalcHeight(m, fcu, vfcdu, edim, fch);
}

template <class M_>
void UNormal<M_>::CalcHeight(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<Vect>& fcdu2,
    const FieldCell<Vect>& fcdu4, const FieldCell<Vect>& fcdu6, size_t edim,
    FieldCell<Vect>& fch) {
  std::array<const FieldCell<Vect>*, 3> vfcdu = {&fcdu2, &fcdu4, &fcdu6};
  Imp::CalcHeight(m, fcu, vfcdu, edim, fch);
}

template <class M_>
void UNormal<M_>::CalcCurvHeight(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<Vect>& fch,
    const FieldCell<Vect>& fcn, size_t edim, FieldCell<Scal>& fck) {
  Imp::CalcCurvHeight(m, fcu, fch, fcn, edim, fck);
}

template <class M_>
void UNormal<M_>::CalcNormalYoungs(
    M& m, const FieldCell<Scal>& fcu, const FieldCell<bool>& fci,
    FieldCell<Vect>& fcn) {
  Imp::CalcNormalYoungs1(m, fcu, fci, fcn);
}

template <class M_>
auto UNormal<M_>::GetNormalYoungs(const std::array<Scal, 27>& u) -> Vect {
  return Imp::GetNormalYoungs(u);
}

template <class M_>
void UNormal<M_>::GetNormalHeight(const std::array<Scal, 27>& u, Vect& n) {
  return Imp::GetNormalHeight(u, n);
}
