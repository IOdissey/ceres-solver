// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2022 Google Inc. All rights reserved.
// http://ceres-solver.org/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: wjr@google.com (William Rucklidge)
//
// This file contains tests for the GradientChecker class.

#include "ceres/gradient_checker.h"

#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include "ceres/cost_function.h"
#include "ceres/problem.h"
#include "ceres/solver.h"
#include "ceres/test_util.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

namespace ceres::internal {

using std::vector;
const double kTolerance = 1e-12;

// We pick a (non-quadratic) function whose derivative are easy:
//
//    f = exp(- a' x).
//   df = - f a.
//
// where 'a' is a vector of the same size as 'x'. In the block
// version, they are both block vectors, of course.
class GoodTestTerm : public CostFunction {
 public:
  template <class UniformRandomFunctor>
  GoodTestTerm(int arity, int const* dim, UniformRandomFunctor&& randu)
      : arity_(arity), return_value_(true) {
    std::uniform_real_distribution distribution(-1.0, 1.0);
    // Make 'arity' random vectors.
    a_.resize(arity_);
    for (int j = 0; j < arity_; ++j) {
      a_[j].resize(dim[j]);
      for (int u = 0; u < dim[j]; ++u) {
        a_[j][u] = randu();
      }
    }

    for (int i = 0; i < arity_; i++) {
      mutable_parameter_block_sizes()->push_back(dim[i]);
    }
    set_num_residuals(1);
  }

  bool Evaluate(double const* const* parameters,
                double* residuals,
                double** jacobians) const override {
    if (!return_value_) {
      return false;
    }
    // Compute a . x.
    double ax = 0;
    for (int j = 0; j < arity_; ++j) {
      for (int u = 0; u < parameter_block_sizes()[j]; ++u) {
        ax += a_[j][u] * parameters[j][u];
      }
    }

    // This is the cost, but also appears as a factor
    // in the derivatives.
    double f = *residuals = exp(-ax);

    // Accumulate 1st order derivatives.
    if (jacobians) {
      for (int j = 0; j < arity_; ++j) {
        if (jacobians[j]) {
          for (int u = 0; u < parameter_block_sizes()[j]; ++u) {
            // See comments before class.
            jacobians[j][u] = -f * a_[j][u];
          }
        }
      }
    }

    return true;
  }

  void SetReturnValue(bool return_value) { return_value_ = return_value; }

 private:
  int arity_;
  bool return_value_;
  vector<vector<double>> a_;  // our vectors.
};

class BadTestTerm : public CostFunction {
 public:
  template <class UniformRandomFunctor>
  BadTestTerm(int arity, int const* dim, UniformRandomFunctor&& randu)
      : arity_(arity) {
    // Make 'arity' random vectors.
    a_.resize(arity_);
    for (int j = 0; j < arity_; ++j) {
      a_[j].resize(dim[j]);
      for (int u = 0; u < dim[j]; ++u) {
        a_[j][u] = randu();
      }
    }

    for (int i = 0; i < arity_; i++) {
      mutable_parameter_block_sizes()->push_back(dim[i]);
    }
    set_num_residuals(1);
  }

  bool Evaluate(double const* const* parameters,
                double* residuals,
                double** jacobians) const override {
    // Compute a . x.
    double ax = 0;
    for (int j = 0; j < arity_; ++j) {
      for (int u = 0; u < parameter_block_sizes()[j]; ++u) {
        ax += a_[j][u] * parameters[j][u];
      }
    }

    // This is the cost, but also appears as a factor
    // in the derivatives.
    double f = *residuals = exp(-ax);

    // Accumulate 1st order derivatives.
    if (jacobians) {
      for (int j = 0; j < arity_; ++j) {
        if (jacobians[j]) {
          for (int u = 0; u < parameter_block_sizes()[j]; ++u) {
            // See comments before class.
            jacobians[j][u] = -f * a_[j][u] + kTolerance;
          }
        }
      }
    }

    return true;
  }

 private:
  int arity_;
  vector<vector<double>> a_;  // our vectors.
};

static void CheckDimensions(const GradientChecker::ProbeResults& results,
                            const std::vector<int>& parameter_sizes,
                            const std::vector<int>& local_parameter_sizes,
                            int residual_size) {
  CHECK_EQ(parameter_sizes.size(), local_parameter_sizes.size());
  int num_parameters = parameter_sizes.size();
  ASSERT_EQ(residual_size, results.residuals.size());
  ASSERT_EQ(num_parameters, results.local_jacobians.size());
  ASSERT_EQ(num_parameters, results.local_numeric_jacobians.size());
  ASSERT_EQ(num_parameters, results.jacobians.size());
  ASSERT_EQ(num_parameters, results.numeric_jacobians.size());
  for (int i = 0; i < num_parameters; ++i) {
    EXPECT_EQ(residual_size, results.local_jacobians.at(i).rows());
    EXPECT_EQ(local_parameter_sizes[i], results.local_jacobians.at(i).cols());
    EXPECT_EQ(residual_size, results.local_numeric_jacobians.at(i).rows());
    EXPECT_EQ(local_parameter_sizes[i],
              results.local_numeric_jacobians.at(i).cols());
    EXPECT_EQ(residual_size, results.jacobians.at(i).rows());
    EXPECT_EQ(parameter_sizes[i], results.jacobians.at(i).cols());
    EXPECT_EQ(residual_size, results.numeric_jacobians.at(i).rows());
    EXPECT_EQ(parameter_sizes[i], results.numeric_jacobians.at(i).cols());
  }
}

TEST(GradientChecker, SmokeTest) {
  // Test with 3 blocks of size 2, 3 and 4.
  int const num_parameters = 3;
  std::vector<int> parameter_sizes(3);
  parameter_sizes[0] = 2;
  parameter_sizes[1] = 3;
  parameter_sizes[2] = 4;

  // Make a random set of blocks.
  FixedArray<double*> parameters(num_parameters);
  std::mt19937 prng;
  std::uniform_real_distribution<double> distribution(-1.0, 1.0);
  auto randu = [&prng, &distribution] { return distribution(prng); };
  for (int j = 0; j < num_parameters; ++j) {
    parameters[j] = new double[parameter_sizes[j]];
    for (int u = 0; u < parameter_sizes[j]; ++u) {
      parameters[j][u] = randu();
    }
  }

  NumericDiffOptions numeric_diff_options;
  GradientChecker::ProbeResults results;

  // Test that Probe returns true for correct Jacobians.
  GoodTestTerm good_term(num_parameters, parameter_sizes.data(), randu);
  std::vector<const Manifold*>* manifolds = nullptr;
  GradientChecker good_gradient_checker(
      &good_term, manifolds, numeric_diff_options);
  EXPECT_TRUE(
      good_gradient_checker.Probe(parameters.data(), kTolerance, nullptr));
  EXPECT_TRUE(
      good_gradient_checker.Probe(parameters.data(), kTolerance, &results))
      << results.error_log;

  // Check that results contain sensible data.
  ASSERT_EQ(results.return_value, true);
  ASSERT_EQ(results.residuals.size(), 1);
  CheckDimensions(results, parameter_sizes, parameter_sizes, 1);
  EXPECT_GE(results.maximum_relative_error, 0.0);
  EXPECT_TRUE(results.error_log.empty());

  // Test that if the cost function return false, Probe should return false.
  good_term.SetReturnValue(false);
  EXPECT_FALSE(
      good_gradient_checker.Probe(parameters.data(), kTolerance, nullptr));
  EXPECT_FALSE(
      good_gradient_checker.Probe(parameters.data(), kTolerance, &results))
      << results.error_log;

  // Check that results contain sensible data.
  ASSERT_EQ(results.return_value, false);
  ASSERT_EQ(results.residuals.size(), 1);
  CheckDimensions(results, parameter_sizes, parameter_sizes, 1);
  for (int i = 0; i < num_parameters; ++i) {
    EXPECT_EQ(results.local_jacobians.at(i).norm(), 0);
    EXPECT_EQ(results.local_numeric_jacobians.at(i).norm(), 0);
  }
  EXPECT_EQ(results.maximum_relative_error, 0.0);
  EXPECT_FALSE(results.error_log.empty());

  // Test that Probe returns false for incorrect Jacobians.
  BadTestTerm bad_term(num_parameters, parameter_sizes.data(), randu);
  GradientChecker bad_gradient_checker(
      &bad_term, manifolds, numeric_diff_options);
  EXPECT_FALSE(
      bad_gradient_checker.Probe(parameters.data(), kTolerance, nullptr));
  EXPECT_FALSE(
      bad_gradient_checker.Probe(parameters.data(), kTolerance, &results));

  // Check that results contain sensible data.
  ASSERT_EQ(results.return_value, true);
  ASSERT_EQ(results.residuals.size(), 1);
  CheckDimensions(results, parameter_sizes, parameter_sizes, 1);
  EXPECT_GT(results.maximum_relative_error, kTolerance);
  EXPECT_FALSE(results.error_log.empty());

  // Setting a high threshold should make the test pass.
  EXPECT_TRUE(bad_gradient_checker.Probe(parameters.data(), 1.0, &results));

  // Check that results contain sensible data.
  ASSERT_EQ(results.return_value, true);
  ASSERT_EQ(results.residuals.size(), 1);
  CheckDimensions(results, parameter_sizes, parameter_sizes, 1);
  EXPECT_GT(results.maximum_relative_error, 0.0);
  EXPECT_TRUE(results.error_log.empty());

  for (int j = 0; j < num_parameters; j++) {
    delete[] parameters[j];
  }
}

/**
 * Helper cost function that multiplies the parameters by the given jacobians
 * and adds a constant offset.
 */
class LinearCostFunction : public CostFunction {
 public:
  explicit LinearCostFunction(Vector residuals_offset)
      : residuals_offset_(std::move(residuals_offset)) {
    set_num_residuals(residuals_offset_.size());
  }

  bool Evaluate(double const* const* parameter_ptrs,
                double* residuals_ptr,
                double** residual_J_params) const final {
    CHECK_GE(residual_J_params_.size(), 0.0);
    VectorRef residuals(residuals_ptr, residual_J_params_[0].rows());
    residuals = residuals_offset_;

    for (size_t i = 0; i < residual_J_params_.size(); ++i) {
      const Matrix& residual_J_param = residual_J_params_[i];
      int parameter_size = residual_J_param.cols();
      ConstVectorRef param(parameter_ptrs[i], parameter_size);

      // Compute residual.
      residuals += residual_J_param * param;

      // Return Jacobian.
      if (residual_J_params != nullptr && residual_J_params[i] != nullptr) {
        Eigen::Map<Matrix> residual_J_param_out(residual_J_params[i],
                                                residual_J_param.rows(),
                                                residual_J_param.cols());
        if (jacobian_offsets_.count(i) != 0) {
          residual_J_param_out = residual_J_param + jacobian_offsets_.at(i);
        } else {
          residual_J_param_out = residual_J_param;
        }
      }
    }
    return true;
  }

  void AddParameter(const Matrix& residual_J_param) {
    CHECK_EQ(num_residuals(), residual_J_param.rows());
    residual_J_params_.push_back(residual_J_param);
    mutable_parameter_block_sizes()->push_back(residual_J_param.cols());
  }

  /// Add offset to the given Jacobian before returning it from Evaluate(),
  /// thus introducing an error in the computation.
  void SetJacobianOffset(size_t index, Matrix offset) {
    CHECK_LT(index, residual_J_params_.size());
    CHECK_EQ(residual_J_params_[index].rows(), offset.rows());
    CHECK_EQ(residual_J_params_[index].cols(), offset.cols());
    jacobian_offsets_[index] = offset;
  }

 private:
  std::vector<Matrix> residual_J_params_;
  std::map<int, Matrix> jacobian_offsets_;
  Vector residuals_offset_;
};

// Helper function to compare two Eigen matrices (used in the test below).
static void ExpectMatricesClose(Matrix p, Matrix q, double tolerance) {
  ASSERT_EQ(p.rows(), q.rows());
  ASSERT_EQ(p.cols(), q.cols());
  ExpectArraysClose(p.size(), p.data(), q.data(), tolerance);
}

// Helper manifold that multiplies the delta vector by the given
// jacobian and adds it to the parameter.
class MatrixManifold : public Manifold {
 public:
  bool Plus(const double* x,
            const double* delta,
            double* x_plus_delta) const final {
    VectorRef(x_plus_delta, AmbientSize()) =
        ConstVectorRef(x, AmbientSize()) +
        (global_to_local_ * ConstVectorRef(delta, TangentSize()));
    return true;
  }

  bool PlusJacobian(const double* /*x*/, double* jacobian) const final {
    MatrixRef(jacobian, AmbientSize(), TangentSize()) = global_to_local_;
    return true;
  }

  bool Minus(const double* y, const double* x, double* y_minus_x) const final {
    LOG(FATAL) << "Should not be called";
    return true;
  }

  bool MinusJacobian(const double* x, double* jacobian) const final {
    LOG(FATAL) << "Should not be called";
    return true;
  }

  int AmbientSize() const final { return global_to_local_.rows(); }
  int TangentSize() const final { return global_to_local_.cols(); }

  Matrix global_to_local_;
};

TEST(GradientChecker, TestCorrectnessWithManifolds) {
  // Create cost function.
  Eigen::Vector3d residual_offset(100.0, 200.0, 300.0);
  LinearCostFunction cost_function(residual_offset);
  Eigen::Matrix<double, 3, 3, Eigen::RowMajor> j0;
  j0.row(0) << 1.0, 2.0, 3.0;
  j0.row(1) << 4.0, 5.0, 6.0;
  j0.row(2) << 7.0, 8.0, 9.0;
  Eigen::Matrix<double, 3, 2, Eigen::RowMajor> j1;
  j1.row(0) << 10.0, 11.0;
  j1.row(1) << 12.0, 13.0;
  j1.row(2) << 14.0, 15.0;

  Eigen::Vector3d param0(1.0, 2.0, 3.0);
  Eigen::Vector2d param1(4.0, 5.0);

  cost_function.AddParameter(j0);
  cost_function.AddParameter(j1);

  std::vector<int> parameter_sizes(2);
  parameter_sizes[0] = 3;
  parameter_sizes[1] = 2;
  std::vector<int> tangent_sizes(2);
  tangent_sizes[0] = 2;
  tangent_sizes[1] = 2;

  // Test cost function for correctness.
  Eigen::Matrix<double, 3, 3, Eigen::RowMajor> j1_out;
  Eigen::Matrix<double, 3, 2, Eigen::RowMajor> j2_out;
  Eigen::Vector3d residual;
  std::vector<const double*> parameters(2);
  parameters[0] = param0.data();
  parameters[1] = param1.data();
  std::vector<double*> jacobians(2);
  jacobians[0] = j1_out.data();
  jacobians[1] = j2_out.data();
  cost_function.Evaluate(parameters.data(), residual.data(), jacobians.data());

  Matrix residual_expected = residual_offset + j0 * param0 + j1 * param1;

  ExpectMatricesClose(j1_out, j0, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(j2_out, j1, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(residual, residual_expected, kTolerance);

  // Create manifold.
  Eigen::Matrix<double, 3, 2, Eigen::RowMajor> global_to_local;
  global_to_local.row(0) << 1.5, 2.5;
  global_to_local.row(1) << 3.5, 4.5;
  global_to_local.row(2) << 5.5, 6.5;

  MatrixManifold manifold;
  manifold.global_to_local_ = global_to_local;

  // Test manifold for correctness.
  Eigen::Vector3d x(7.0, 8.0, 9.0);
  Eigen::Vector2d delta(10.0, 11.0);

  Eigen::Matrix<double, 3, 2, Eigen::RowMajor> global_to_local_out;
  manifold.PlusJacobian(x.data(), global_to_local_out.data());
  ExpectMatricesClose(global_to_local_out,
                      global_to_local,
                      std::numeric_limits<double>::epsilon());

  Eigen::Vector3d x_plus_delta;
  manifold.Plus(x.data(), delta.data(), x_plus_delta.data());
  Eigen::Vector3d x_plus_delta_expected = x + (global_to_local * delta);
  ExpectMatricesClose(x_plus_delta, x_plus_delta_expected, kTolerance);

  // Now test GradientChecker.
  std::vector<const Manifold*> manifolds(2);
  manifolds[0] = &manifold;
  manifolds[1] = nullptr;
  NumericDiffOptions numeric_diff_options;
  GradientChecker::ProbeResults results;
  GradientChecker gradient_checker(
      &cost_function, &manifolds, numeric_diff_options);

  Problem::Options problem_options;
  problem_options.cost_function_ownership = DO_NOT_TAKE_OWNERSHIP;
  problem_options.manifold_ownership = DO_NOT_TAKE_OWNERSHIP;
  Problem problem(problem_options);
  Eigen::Vector3d param0_solver;
  Eigen::Vector2d param1_solver;
  problem.AddParameterBlock(param0_solver.data(), 3, &manifold);
  problem.AddParameterBlock(param1_solver.data(), 2);
  problem.AddResidualBlock(
      &cost_function, nullptr, param0_solver.data(), param1_solver.data());

  // First test case: everything is correct.
  EXPECT_TRUE(gradient_checker.Probe(parameters.data(), kTolerance, nullptr));
  EXPECT_TRUE(gradient_checker.Probe(parameters.data(), kTolerance, &results))
      << results.error_log;

  // Check that results contain correct data.
  ASSERT_EQ(results.return_value, true);
  ExpectMatricesClose(
      results.residuals, residual, std::numeric_limits<double>::epsilon());
  CheckDimensions(results, parameter_sizes, tangent_sizes, 3);
  ExpectMatricesClose(
      results.local_jacobians.at(0), j0 * global_to_local, kTolerance);
  ExpectMatricesClose(results.local_jacobians.at(1),
                      j1,
                      std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(
      results.local_numeric_jacobians.at(0), j0 * global_to_local, kTolerance);
  ExpectMatricesClose(results.local_numeric_jacobians.at(1), j1, kTolerance);
  ExpectMatricesClose(
      results.jacobians.at(0), j0, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(
      results.jacobians.at(1), j1, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(results.numeric_jacobians.at(0), j0, kTolerance);
  ExpectMatricesClose(results.numeric_jacobians.at(1), j1, kTolerance);
  EXPECT_GE(results.maximum_relative_error, 0.0);
  EXPECT_TRUE(results.error_log.empty());

  // Test interaction with the 'check_gradients' option in Solver.
  Solver::Options solver_options;
  solver_options.linear_solver_type = DENSE_QR;
  solver_options.check_gradients = true;
  solver_options.initial_trust_region_radius = 1e10;
  Solver solver;
  Solver::Summary summary;

  param0_solver = param0;
  param1_solver = param1;
  solver.Solve(solver_options, &problem, &summary);
  EXPECT_EQ(CONVERGENCE, summary.termination_type);
  EXPECT_LE(summary.final_cost, 1e-12);

  // Second test case: Mess up reported derivatives with respect to 3rd
  // component of 1st parameter. Check should fail.
  Eigen::Matrix<double, 3, 3, Eigen::RowMajor> j0_offset;
  j0_offset.setZero();
  j0_offset.col(2).setConstant(0.001);
  cost_function.SetJacobianOffset(0, j0_offset);
  EXPECT_FALSE(gradient_checker.Probe(parameters.data(), kTolerance, nullptr));
  EXPECT_FALSE(gradient_checker.Probe(parameters.data(), kTolerance, &results))
      << results.error_log;

  // Check that results contain correct data.
  ASSERT_EQ(results.return_value, true);
  ExpectMatricesClose(
      results.residuals, residual, std::numeric_limits<double>::epsilon());
  CheckDimensions(results, parameter_sizes, tangent_sizes, 3);
  ASSERT_EQ(results.local_jacobians.size(), 2);
  ASSERT_EQ(results.local_numeric_jacobians.size(), 2);
  ExpectMatricesClose(results.local_jacobians.at(0),
                      (j0 + j0_offset) * global_to_local,
                      kTolerance);
  ExpectMatricesClose(results.local_jacobians.at(1),
                      j1,
                      std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(
      results.local_numeric_jacobians.at(0), j0 * global_to_local, kTolerance);
  ExpectMatricesClose(results.local_numeric_jacobians.at(1), j1, kTolerance);
  ExpectMatricesClose(results.jacobians.at(0), j0 + j0_offset, kTolerance);
  ExpectMatricesClose(
      results.jacobians.at(1), j1, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(results.numeric_jacobians.at(0), j0, kTolerance);
  ExpectMatricesClose(results.numeric_jacobians.at(1), j1, kTolerance);
  EXPECT_GT(results.maximum_relative_error, 0.0);
  EXPECT_FALSE(results.error_log.empty());

  // Test interaction with the 'check_gradients' option in Solver.
  param0_solver = param0;
  param1_solver = param1;
  solver.Solve(solver_options, &problem, &summary);
  EXPECT_EQ(FAILURE, summary.termination_type);

  // Now, zero out the manifold Jacobian with respect to the 3rd component of
  // the 1st parameter. This makes the combination of cost function and manifold
  // return correct values again.
  manifold.global_to_local_.row(2).setZero();

  // Verify that the gradient checker does not treat this as an error.
  EXPECT_TRUE(gradient_checker.Probe(parameters.data(), kTolerance, &results))
      << results.error_log;

  // Check that results contain correct data.
  ASSERT_EQ(results.return_value, true);
  ExpectMatricesClose(
      results.residuals, residual, std::numeric_limits<double>::epsilon());
  CheckDimensions(results, parameter_sizes, tangent_sizes, 3);
  ASSERT_EQ(results.local_jacobians.size(), 2);
  ASSERT_EQ(results.local_numeric_jacobians.size(), 2);
  ExpectMatricesClose(results.local_jacobians.at(0),
                      (j0 + j0_offset) * manifold.global_to_local_,
                      kTolerance);
  ExpectMatricesClose(results.local_jacobians.at(1),
                      j1,
                      std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(results.local_numeric_jacobians.at(0),
                      j0 * manifold.global_to_local_,
                      kTolerance);
  ExpectMatricesClose(results.local_numeric_jacobians.at(1), j1, kTolerance);
  ExpectMatricesClose(results.jacobians.at(0), j0 + j0_offset, kTolerance);
  ExpectMatricesClose(
      results.jacobians.at(1), j1, std::numeric_limits<double>::epsilon());
  ExpectMatricesClose(results.numeric_jacobians.at(0), j0, kTolerance);
  ExpectMatricesClose(results.numeric_jacobians.at(1), j1, kTolerance);
  EXPECT_GE(results.maximum_relative_error, 0.0);
  EXPECT_TRUE(results.error_log.empty());

  // Test interaction with the 'check_gradients' option in Solver.
  param0_solver = param0;
  param1_solver = param1;
  solver.Solve(solver_options, &problem, &summary);
  EXPECT_EQ(CONVERGENCE, summary.termination_type);
  EXPECT_LE(summary.final_cost, 1e-12);
}
}  // namespace ceres::internal
