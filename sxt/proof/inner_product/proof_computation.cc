#include "sxt/proof/inner_product/proof_computation.h"

#include <cassert>
#include <vector>

#include "sxt/base/num/ceil_log2.h"
#include "sxt/curve21/operation/add.h"
#include "sxt/curve21/operation/scalar_multiply.h"
#include "sxt/curve21/type/element_p3.h"
#include "sxt/proof/inner_product/driver.h"
#include "sxt/proof/inner_product/proof_descriptor.h"
#include "sxt/proof/inner_product/workspace.h"
#include "sxt/proof/transcript/transcript_utility.h"
#include "sxt/ristretto/operation/compression.h"
#include "sxt/ristretto/type/compressed_element.h"
#include "sxt/scalar25/type/element.h"

namespace sxt::prfip {
//--------------------------------------------------------------------------------------------------
// init_transcript
//--------------------------------------------------------------------------------------------------
static void init_transcript(prft::transcript& transcript, uint64_t n) noexcept {
  prft::set_domain(transcript, "inner product proof v1");
  prft::append_value(transcript, "n", n);
}

//--------------------------------------------------------------------------------------------------
// compute_round_challenge
//--------------------------------------------------------------------------------------------------
static void compute_round_challenge(s25t::element& x, prft::transcript& transcript,
                                    const rstt::compressed_element& l_value,
                                    const rstt::compressed_element& r_value) noexcept {
  prft::append_value(transcript, "L", l_value);
  prft::append_value(transcript, "R", r_value);

  prft::challenge_value(x, transcript, "x");
}

//--------------------------------------------------------------------------------------------------
// prove_inner_product
//--------------------------------------------------------------------------------------------------
void prove_inner_product(basct::span<rstt::compressed_element> l_vector,
                         basct::span<rstt::compressed_element> r_vector, s25t::element& ap_value,
                         prft::transcript& transcript, const driver& drv,
                         const proof_descriptor& descriptor,
                         basct::cspan<s25t::element> a_vector) noexcept {
  auto n = a_vector.size();
  auto n_lg2 = static_cast<size_t>(basn::ceil_log2(n));
  auto np = 1ull << n_lg2;
  auto num_rounds = n_lg2;
  // clang-format off
  assert(
    l_vector.size() == num_rounds &&
    r_vector.size() == num_rounds &&
    descriptor.b_vector.size() == n &&
    descriptor.g_vector.size() == np &&
    a_vector.size() == n
  );
  // clang-format on

  init_transcript(transcript, n);

  if (n == 1) {
    ap_value = a_vector[0];
    return;
  }

  auto workspace = drv.make_workspace(descriptor, a_vector);
  size_t round_index = 0;
  while (np > 1) {
    auto& l_value = l_vector[round_index];
    auto& r_value = r_vector[round_index];
    drv.commit_to_fold(l_value, r_value, *workspace);

    s25t::element x;
    compute_round_challenge(x, transcript, l_value, r_value);

    drv.fold(*workspace, x);

    np /= 2;
    ++round_index;
  }

  workspace->ap_value(ap_value);
}

//--------------------------------------------------------------------------------------------------
// verify_inner_product
//--------------------------------------------------------------------------------------------------
bool verify_inner_product(prft::transcript& transcript, const driver& drv,
                          const proof_descriptor& descriptor, const s25t::element& product,
                          const c21t::element_p3& a_commit,
                          basct::cspan<rstt::compressed_element> l_vector,
                          basct::cspan<rstt::compressed_element> r_vector,
                          const s25t::element& ap_value) noexcept {
  auto n = descriptor.b_vector.size();
  auto n_lg2 = static_cast<size_t>(basn::ceil_log2(n));
  auto np = 1ull << n_lg2;
  auto num_rounds = n_lg2;
  // clang-format off
  assert(
    descriptor.b_vector.size() == n &&
    descriptor.g_vector.size() == np
  );
  // clang-format on

  if (l_vector.size() != num_rounds || r_vector.size() != num_rounds) {
    return false;
  }

  init_transcript(transcript, n);

  std::vector<s25t::element> x_vector(num_rounds);

  for (size_t round_index = 0; round_index < num_rounds; ++round_index) {
    compute_round_challenge(x_vector[round_index], transcript, l_vector[round_index],
                            r_vector[round_index]);
  }

  rstt::compressed_element expected_commit;
  drv.compute_expected_commitment(expected_commit, descriptor, l_vector, r_vector, x_vector,
                                  ap_value);

  c21t::element_p3 commit;
  c21o::scalar_multiply(commit, product, *descriptor.q_value);
  c21o::add(commit, commit, a_commit);
  rstt::compressed_element commit_p;
  rsto::compress(commit_p, commit);

  return commit_p == expected_commit;
}
} // namespace sxt::prfip