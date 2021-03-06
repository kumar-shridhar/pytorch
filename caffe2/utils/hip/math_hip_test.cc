#include <iostream>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "caffe2/core/context.h"
#include "caffe2/core/flags.h"
#include "caffe2/core/hip/context_hip.h"
#include "caffe2/operators/utility_ops.h"
#include "caffe2/utils/math.h"

CAFFE2_DECLARE_string(caffe_test_root);

namespace caffe2 {

void executeGpuBinaryOpTest(
    int shapex0,
    int shapex1,
    int shapey,
    std::function<float(int)> input0,
    std::function<float(int)> input1,
    std::function<void(
        int N0,
        int N1,
        const float* src0,
        const float* src1,
        float* dst,
        HIPContext* context)> operation,
    std::function<float(int)> correct_output) {
  if (!HasHipGPU())
    return;
  Workspace ws;
  DeviceOption option;
  option.set_device_type(HIP);
  HIPContext context(option);

  Blob* blobx0 = ws.CreateBlob("X0");
  Blob* blobx1 = ws.CreateBlob("X1");
  Blob* bloby = ws.CreateBlob("Y");
  Blob* bloby_host = ws.CreateBlob("Y_host");

  auto* tensorx0 = blobx0->GetMutable<Tensor<HIPContext>>();
  auto* tensorx1 = blobx1->GetMutable<Tensor<HIPContext>>();
  auto* tensory = bloby->GetMutable<Tensor<HIPContext>>();

  vector<int> shapex0_vector{shapex0};
  vector<int> shapex1_vector{shapex1};
  vector<int> shapey_vector{shapey};

  tensorx0->Resize(shapex0_vector);
  tensorx1->Resize(shapex1_vector);
  tensory->Resize(shapey_vector);

  for (int i = 0; i < shapex0; i++) {
    math::Set<float, HIPContext>(
        1, input0(i), tensorx0->mutable_data<float>() + i, &context);
  }
  for (int i = 0; i < shapex1; i++) {
    math::Set<float, HIPContext>(
        1, input1(i), tensorx1->mutable_data<float>() + i, &context);
  }
  operation(
      shapex0,
      shapex1,
      tensorx0->template data<float>(),
      tensorx1->template data<float>(),
      tensory->mutable_data<float>(),
      &context);
  context.FinishDeviceComputation();

  // Copy result to CPU so we can inspect it
  auto* tensory_host = bloby_host->GetMutable<Tensor<CPUContext>>();
  tensory_host->CopyFrom<HIPContext, HIPContext>(*tensory, &context);
  context.FinishDeviceComputation();

  for (int i = 0; i < shapey; ++i) {
    EXPECT_EQ(tensory_host->data<float>()[i], correct_output(i));
  }
}

TEST(MathUtilGPUTest, testAddStripedBatch) {
  if (!HasHipGPU())
    return;
  Workspace ws;
  DeviceOption option;
  option.set_device_type(HIP);
  HIPContext context(option);
  Blob* blobx = ws.CreateBlob("X");
  Blob* bloby = ws.CreateBlob("Y");
  Blob* bloby_host = ws.CreateBlob("Y_host");

  vector<int> shapex{33 * 9, 25};
  vector<int> shapey{33, 25};

  auto* tensorx = blobx->GetMutable<Tensor<HIPContext>>();
  tensorx->Resize(shapex);
  int stripe = 33 * 25;
  vector<float> tot(33, 0.0);
  for (int j = 0; j < 9; j++) {
    // Have different values for each line
    for (int k = 0; k < 33; k++) {
      math::Set<float, HIPContext>(
          33,
          1.0 + j + k,
          tensorx->mutable_data<float>() + j * stripe + k * 25,
          &context);
      tot[k] += 1.0 + j + k;
    }
  }

  auto* tensory = bloby->GetMutable<Tensor<HIPContext>>();
  tensory->Resize(shapey);
  math::Set<float, HIPContext>(
      stripe, 0.0, tensory->mutable_data<float>(), &context);

  math::AddStripedBatch<float, HIPContext>(
      stripe,
      tensorx->template data<float>(),
      tensory->mutable_data<float>(),
      stripe,
      9,
      &context);
  context.FinishDeviceComputation();

  // Copy result to CPU so we can inspect it
  auto* tensory_host = bloby_host->GetMutable<Tensor<CPUContext>>();
  tensory_host->CopyFrom<HIPContext, HIPContext>(*tensory, &context);
  context.FinishDeviceComputation();

  for (int k = 0; k < 33; k++) {
    for (int i = 0; i < 25; i++) {
      EXPECT_EQ(tensory_host->data<float>()[k * 25 + i], tot[k]);
    }
  }
}

TEST(MathUtilGPUTest, testReduceMin) {
  executeGpuBinaryOpTest(
      6,
      1,
      1,
      [](int /*i*/) { return 11.0f; },
      [](int /*i*/) { return 0.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* /*src1*/,
         float* dst,
         HIPContext* context) {
        Tensor<HIPContext> aux;
        math::ReduceMin<float, HIPContext>(N0, src0, dst, &aux, context);
      },
      [](int /*i*/) { return 11.0f; });
  executeGpuBinaryOpTest(
      6,
      1,
      1,
      [](int i) { return i == 3 ? 11.0f : 17.0f; },
      [](int /*i*/) { return 0.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* /*src1*/,
         float* dst,
         HIPContext* context) {
        Tensor<HIPContext> aux;
        math::ReduceMin<float, HIPContext>(N0, src0, dst, &aux, context);
      },
      [](int /*i*/) { return 11.0f; });
}

TEST(MathUtilGPUTest, testReduceMax) {
  executeGpuBinaryOpTest(
      6,
      1,
      1,
      [](int /*i*/) { return 11.0f; },
      [](int /*i*/) { return 0.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* /*src1*/,
         float* dst,
         HIPContext* context) {
        Tensor<HIPContext> aux;
        math::ReduceMax<float, HIPContext>(N0, src0, dst, &aux, context);
      },
      [](int /*i*/) { return 11.0f; });
  executeGpuBinaryOpTest(
      6,
      1,
      1,
      [](int i) { return i == 3 ? 17.0f : 11.0f; },
      [](int /*i*/) { return 0.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* /*src1*/,
         float* dst,
         HIPContext* context) {
        Tensor<HIPContext> aux;
        math::ReduceMax<float, HIPContext>(N0, src0, dst, &aux, context);
      },
      [](int /*i*/) { return 17.0f; });
}

TEST(MathUtilGPUTest, testElemwiseMax) {
  executeGpuBinaryOpTest(
      13,
      13,
      13,
      [](int i) { return 2.0f - i; },
      [](int i) { return i - 6.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* src1,
         float* dst,
         HIPContext* context) {
        math::ElemwiseMax<float, HIPContext>(N0, src0, src1, dst, context);
      },
      [](int i) { return std::max(2.0f - i, i - 6.0f); });
}

TEST(MathUtilGPUTest, testCopyVector) {
  executeGpuBinaryOpTest(
      6,
      1,
      6,
      [](int i) { return 5.0f - i; },
      [](int /*i*/) { return 0.0f; },
      [](int N0,
         int /*N1*/,
         const float* src0,
         const float* /*src1*/,
         float* dst,
         HIPContext* context) {
        math::CopyVector<float, HIPContext>(N0, src0, dst, context);
      },
      [](int i) { return 5.0f - i; });
}

namespace {

constexpr float kEps = 1e-5;

class GemmBatchedGPUTest
    : public testing::TestWithParam<testing::tuple<bool, bool>> {
 protected:
  void SetUp() override {
    if (!HasHipGPU()) {
      return;
    }
    option_.set_device_type(HIP);
    hip_context_ = make_unique<HIPContext>(option_);
    Blob* X_blob = ws_.CreateBlob("X");
    Blob* W_blob = ws_.CreateBlob("W");
    Blob* Y_blob = ws_.CreateBlob("Y");
    X_ = X_blob->GetMutable<Tensor<HIPContext>>();
    W_ = W_blob->GetMutable<Tensor<HIPContext>>();
    Y_ = Y_blob->GetMutable<Tensor<HIPContext>>();
    X_->Resize(std::vector<TIndex>{3, 5, 10});
    W_->Resize(std::vector<TIndex>{3, 6, 10});
    Y_->Resize(std::vector<TIndex>{3, 5, 6});
    math::Set<float, HIPContext>(
        X_->size(), 1.0f, X_->mutable_data<float>(), hip_context_.get());
    math::Set<float, HIPContext>(
        W_->size(), 1.0f, W_->mutable_data<float>(), hip_context_.get());
    trans_X_ = std::get<0>(GetParam());
    trans_W_ = std::get<1>(GetParam());
  }

  void RunGemmBatched(const float alpha, const float beta) {
    math::GemmBatched(
        trans_X_ ? CblasTrans : CblasNoTrans,
        trans_W_ ? CblasTrans : CblasNoTrans,
        3,
        5,
        6,
        10,
        alpha,
        X_->template data<float>(),
        W_->template data<float>(),
        beta,
        Y_->template mutable_data<float>(),
        hip_context_.get());
  }

  void VerifyOutput(const float value) const {
    TensorCPU Y_cpu(*Y_);
    for (int i = 0; i < Y_cpu.size(); ++i) {
      EXPECT_FLOAT_EQ(value, Y_cpu.template data<float>()[i]);
    }
  }

  Workspace ws_;
  DeviceOption option_;
  std::unique_ptr<HIPContext> hip_context_;
  Tensor<HIPContext>* X_ = nullptr;
  Tensor<HIPContext>* W_ = nullptr;
  Tensor<HIPContext>* Y_ = nullptr;
  bool trans_X_;
  bool trans_W_;
};

TEST_P(GemmBatchedGPUTest, GemmBatchedGPUFloatTest) {
  if (!HasHipGPU()) {
    return;
  }
  RunGemmBatched(1.0f, 0.0f);
  VerifyOutput(10.0f);
  RunGemmBatched(1.0f, 0.5f);
  VerifyOutput(15.0f);
  RunGemmBatched(0.5f, 1.0f);
  VerifyOutput(20.0f);
}

INSTANTIATE_TEST_CASE_P(
    GemmBatchedGPUTrans,
    GemmBatchedGPUTest,
    testing::Combine(testing::Bool(), testing::Bool()));

class ReduceTensorGPUTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!HasHipGPU()) {
      return;
    }
    option_.set_device_type(HIP);
    hip_context_ = make_unique<HIPContext>(option_);
    Blob* blob_x = ws_.CreateBlob("X");
    Blob* blob_y = ws_.CreateBlob("Y");
    X_ = blob_x->GetMutable<Tensor<HIPContext>>();
    Y_ = blob_y->GetMutable<Tensor<HIPContext>>();
  }

  void SetUpData(
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data) {
    std::vector<int> Y_dims = X_dims;
    for (const int axis : axes) {
      Y_dims[axis] = 1;
    }
    X_->Resize(X_dims);
    Y_->Resize(Y_dims);
    ASSERT_EQ(X_data.size(), X_->size());
    hip_context_->Copy<float, CPUContext, HIPContext>(
        X_data.size(), X_data.data(), X_->mutable_data<float>());
  }

  void VerifyResult(const std::vector<float>& expected_output) {
    Blob* blob_y_host = ws_.CreateBlob("Y_host");
    auto* Y_host = blob_y_host->GetMutable<TensorCPU>();
    Y_host->CopyFrom<HIPContext, HIPContext>(*Y_, hip_context_.get());
    hip_context_->FinishDeviceComputation();
    ASSERT_EQ(expected_output.size(), Y_host->size());
    for (std::size_t i = 0; i < expected_output.size(); ++i) {
      EXPECT_FLOAT_EQ(expected_output[i], Y_host->data<float>()[i]);
    }
  }

  template <class ReduceFunc>
  void RunRedcueTensorTest(
      const ReduceFunc& reduce_func,
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data,
      const std::vector<float>& Y_data) {
    SetUpData(X_dims, axes, X_data);
    reduce_func(
        X_dims.size(),
        X_dims.data(),
        axes.size(),
        axes.data(),
        X_->data<float>(),
        Y_->mutable_data<float>(),
        hip_context_.get());
    VerifyResult(Y_data);
  }

  Workspace ws_;
  DeviceOption option_;
  std::unique_ptr<HIPContext> hip_context_;
  Tensor<HIPContext>* X_ = nullptr;
  Tensor<HIPContext>* Y_ = nullptr;
};

TEST_F(ReduceTensorGPUTest, ReduceMinGPUTest) {
  if (!HasHipGPU()) {
    return;
  }
  const auto& reduce_min = [](const int num_dims,
                              const int* dims,
                              const int num_axes,
                              const int* axes,
                              const float* X,
                              float* Y,
                              HIPContext* context) {
    return math::ReduceMin<float, HIPContext>(
        num_dims, dims, num_axes, axes, X, Y, context);
  };
  // Test for 1D tensor.
  RunRedcueTensorTest(reduce_min, {3}, {0}, {1.0f, 2.0f, 3.0f}, {1.0f});

  // Test for 2D Tensor.
  RunRedcueTensorTest(
      reduce_min,
      {2, 3},
      {1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {1.0f, 4.0f});
  RunRedcueTensorTest(
      reduce_min,
      {2, 3},
      {0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {1.0f, 2.0f, 3.0f});
  RunRedcueTensorTest(
      reduce_min, {2, 3}, {0, 1}, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}, {1.0f});

  // Test for 3D tensor.
  RunRedcueTensorTest(
      reduce_min,
      {2, 2, 2},
      {1, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {1.0f, 5.0f});
  RunRedcueTensorTest(
      reduce_min,
      {2, 2, 2},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {1.0f, 2.0f});
  RunRedcueTensorTest(
      reduce_min,
      {2, 2, 2},
      {0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {1.0f, 3.0f});
}

TEST_F(ReduceTensorGPUTest, ReduceMaxGPUTest) {
  if (!HasHipGPU()) {
    return;
  }
  const auto& reduce_max = [](const int num_dims,
                              const int* dims,
                              const int num_axes,
                              const int* axes,
                              const float* X,
                              float* Y,
                              HIPContext* context) {
    return math::ReduceMax<float, HIPContext>(
        num_dims, dims, num_axes, axes, X, Y, context);
  };
  // Test for 1D tensor.
  RunRedcueTensorTest(reduce_max, {3}, {0}, {1.0f, 2.0f, 3.0f}, {3.0f});

  // Test for 2D Tensor.
  RunRedcueTensorTest(
      reduce_max,
      {2, 3},
      {1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {3.0f, 6.0f});
  RunRedcueTensorTest(
      reduce_max,
      {2, 3},
      {0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {4.0f, 5.0f, 6.0f});
  RunRedcueTensorTest(
      reduce_max, {2, 3}, {0, 1}, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}, {6.0f});

  // Test for 3D tensor.
  RunRedcueTensorTest(
      reduce_max,
      {2, 2, 2},
      {1, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {4.0f, 8.0f});
  RunRedcueTensorTest(
      reduce_max,
      {2, 2, 2},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {7.0f, 8.0f});
  RunRedcueTensorTest(
      reduce_max,
      {2, 2, 2},
      {0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {6.0f, 8.0f});
}

TEST_F(ReduceTensorGPUTest, ReduceSumGPUTest) {
  if (!HasHipGPU()) {
    return;
  }
  // Test for 1D tensor.
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>, {3}, {0}, {1.0f, 2.0f, 3.0f}, {6.0f});

  // Test for 2D Tensor.
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 3},
      {1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {6.0f, 15.0f});
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 3},
      {0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {5.0f, 7.0f, 9.0f});
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 3},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {21.0f});

  // Test for 3D tensor.
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 2, 2},
      {1, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {10.0f, 26.0f});
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 2, 2},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {16.0f, 20.0f});
  RunRedcueTensorTest(
      math::ReduceSum<float, HIPContext>,
      {2, 2, 2},
      {0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {14.0f, 22.0f});
}

TEST_F(ReduceTensorGPUTest, ReduceMeanGPUTest) {
  if (!HasHipGPU()) {
    return;
  }
  // Test for 1D tensor.
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {3},
      {0},
      {1.0f, 2.0f, 3.0f},
      {2.0f});

  // Test for 2D Tensor.
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 3},
      {1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {2.0f, 5.0f});
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 3},
      {0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {2.5f, 3.5f, 4.5f});
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 3},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {3.5f});

  // Test for 3D tensor.
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 2, 2},
      {1, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {2.5f, 6.5f});
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 2, 2},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {4.0f, 5.0f});
  RunRedcueTensorTest(
      math::ReduceMean<float, HIPContext>,
      {2, 2, 2},
      {0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {3.5f, 5.5f});
}

class BroadcastGPUTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!HasHipGPU()) {
      return;
    }
    option_.set_device_type(HIP);
    hip_context_ = make_unique<HIPContext>(option_);
    Blob* blob_x = ws_.CreateBlob("X");
    Blob* blob_y = ws_.CreateBlob("Y");
    X_ = blob_x->GetMutable<Tensor<HIPContext>>();
    Y_ = blob_y->GetMutable<Tensor<HIPContext>>();
  }

  void SetUpData(
      const std::vector<int>& X_dims,
      const std::vector<int>& Y_dims,
      const std::vector<float>& X_data) {
    X_->Resize(X_dims);
    Y_->Resize(Y_dims);
    ASSERT_EQ(X_data.size(), X_->size());
    hip_context_->Copy<float, CPUContext, HIPContext>(
        X_data.size(), X_data.data(), X_->mutable_data<float>());
  }

  void VerifyResult(const std::vector<float>& expected_output) {
    Blob* blob_y_host = ws_.CreateBlob("Y_host");
    auto* Y_host = blob_y_host->GetMutable<TensorCPU>();
    Y_host->CopyFrom<HIPContext, HIPContext>(*Y_, hip_context_.get());
    hip_context_->FinishDeviceComputation();
    ASSERT_EQ(expected_output.size(), Y_host->size());
    for (std::size_t i = 0; i < expected_output.size(); ++i) {
      EXPECT_FLOAT_EQ(expected_output[i], Y_host->data<float>()[i]);
    }
  }

  void RunBroadcastTest(
      const std::vector<int>& X_dims,
      const std::vector<int>& Y_dims,
      const std::vector<float>& X_data,
      const std::vector<float>& Y_data) {
    SetUpData(X_dims, Y_dims, X_data);
    math::Broadcast<float, HIPContext>(
        X_dims.size(),
        X_dims.data(),
        Y_dims.size(),
        Y_dims.data(),
        X_->data<float>(),
        Y_->mutable_data<float>(),
        hip_context_.get());
    VerifyResult(Y_data);
  }

  Workspace ws_;
  DeviceOption option_;
  std::unique_ptr<HIPContext> hip_context_;
  Tensor<HIPContext>* X_ = nullptr;
  Tensor<HIPContext>* Y_ = nullptr;
};

TEST_F(BroadcastGPUTest, BroadcastGPUFloatTest) {
  if (!HasHipGPU()) {
    return;
  }
  RunBroadcastTest({2}, {2}, {1.0f, 2.0f}, {1.0f, 2.0f});
  RunBroadcastTest({1}, {2}, {1.0f}, {1.0f, 1.0f});
  RunBroadcastTest({1}, {2, 2}, {1.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
  RunBroadcastTest({2, 1}, {2, 2}, {1.0f, 2.0f}, {1.0f, 1.0f, 2.0f, 2.0f});
  RunBroadcastTest(
      {2, 1},
      {2, 2, 2},
      {1.0f, 2.0f},
      {1.0f, 1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 2.0f, 2.0f});
}

class MomentsGPUTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!HasHipGPU()) {
      return;
    }
    option_.set_device_type(HIP);
    hip_context_ = make_unique<HIPContext>(option_);
    Blob* blob_x = ws_.CreateBlob("X");
    Blob* blob_mean = ws_.CreateBlob("mean");
    Blob* blob_variance = ws_.CreateBlob("variance");
    X_ = blob_x->GetMutable<Tensor<HIPContext>>();
    mean_ = blob_mean->GetMutable<Tensor<HIPContext>>();
    variance_ = blob_variance->GetMutable<Tensor<HIPContext>>();
  }

  void SetUpData(
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data) {
    std::vector<int> Y_dims = X_dims;
    for (const int axis : axes) {
      Y_dims[axis] = 1;
    }
    X_->Resize(X_dims);
    mean_->Resize(Y_dims);
    variance_->Resize(Y_dims);
    ASSERT_EQ(X_data.size(), X_->size());
    hip_context_->Copy<float, CPUContext, HIPContext>(
        X_data.size(), X_data.data(), X_->mutable_data<float>());
  }

  void VerifyResult(
      const std::vector<float>& mean_data,
      const std::vector<float>& variance_data) {
    Blob* blob_mean_host = ws_.CreateBlob("mean_host");
    auto* mean_host = blob_mean_host->GetMutable<TensorCPU>();
    mean_host->CopyFrom<HIPContext, HIPContext>(*mean_, hip_context_.get());
    Blob* blob_variance_host = ws_.CreateBlob("variance_host");
    auto* variance_host = blob_variance_host->GetMutable<TensorCPU>();
    variance_host->CopyFrom<HIPContext, HIPContext>(
        *variance_, hip_context_.get());
    hip_context_->FinishDeviceComputation();

    ASSERT_EQ(mean_data.size(), mean_host->size());
    for (std::size_t i = 0; i < mean_data.size(); ++i) {
      EXPECT_FLOAT_EQ(mean_data[i], mean_host->data<float>()[i]);
    }
    ASSERT_EQ(variance_data.size(), variance_host->size());
    for (std::size_t i = 0; i < variance_data.size(); ++i) {
      EXPECT_NEAR(variance_data[i], variance_host->data<float>()[i], kEps);
    }
  }

  void RunMomentsTest(
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data,
      const std::vector<float>& mean_data,
      const std::vector<float>& variance_data) {
    SetUpData(X_dims, axes, X_data);
    math::Moments<float, HIPContext>(
        X_dims.size(),
        X_dims.data(),
        axes.size(),
        axes.data(),
        X_->data<float>(),
        mean_->mutable_data<float>(),
        variance_->mutable_data<float>(),
        hip_context_.get());
    VerifyResult(mean_data, variance_data);
  }

  Workspace ws_;
  DeviceOption option_;
  std::unique_ptr<HIPContext> hip_context_;
  Tensor<HIPContext>* X_ = nullptr;
  Tensor<HIPContext>* mean_ = nullptr;
  Tensor<HIPContext>* variance_ = nullptr;
};

TEST_F(MomentsGPUTest, MomentsGPUFloatTest) {
  if (!HasHipGPU()) {
    return;
  }
  // Test for 1D tensor.
  RunMomentsTest({3}, {0}, {1.0f, 2.0f, 3.0f}, {2.0f}, {2.0f / 3.0f});

  // Test for 2D Tensor.
  RunMomentsTest(
      {2, 3},
      {1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {2.0f, 5.0f},
      {2.0f / 3.0f, 2.0f / 3.0f});
  RunMomentsTest(
      {2, 3},
      {0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {2.5f, 3.5f, 4.5f},
      {2.25f, 2.25f, 2.25f});
  RunMomentsTest(
      {2, 3},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {3.5f},
      {35.0f / 12.0f});

  // Test for 3D tensor.
  RunMomentsTest(
      {2, 2, 2},
      {1, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {2.5f, 6.5f},
      {1.25, 1.25});
  RunMomentsTest(
      {2, 2, 2},
      {0, 1},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {4.0f, 5.0f},
      {5.0f, 5.0f});
  RunMomentsTest(
      {2, 2, 2},
      {0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {3.5f, 5.5f},
      {4.25, 4.25});
}

class TransposeGPUTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!HasHipGPU()) {
      return;
    }
    option_.set_device_type(HIP);
    hip_context_ = make_unique<HIPContext>(option_);
    Blob* blob_x = ws_.CreateBlob("X");
    Blob* blob_y = ws_.CreateBlob("Y");
    X_ = blob_x->GetMutable<Tensor<HIPContext>>();
    Y_ = blob_y->GetMutable<Tensor<HIPContext>>();
  }

  void SetUpData(
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data) {
    const int ndim = X_dims.size();
    std::vector<int> Y_dims(ndim);
    for (int i = 0; i < ndim; ++i) {
      Y_dims[i] = X_dims[axes[i]];
    }
    X_->Resize(X_dims);
    Y_->Resize(Y_dims);
    ASSERT_EQ(X_data.size(), X_->size());
    hip_context_->Copy<float, CPUContext, HIPContext>(
        X_data.size(), X_data.data(), X_->mutable_data<float>());
  }

  void VerifyResult(const std::vector<float>& expected_output) {
    Blob* blob_y_host = ws_.CreateBlob("Y_host");
    auto* Y_host = blob_y_host->GetMutable<TensorCPU>();
    Y_host->CopyFrom<HIPContext, HIPContext>(*Y_, hip_context_.get());
    hip_context_->FinishDeviceComputation();
    ASSERT_EQ(expected_output.size(), Y_host->size());
    for (std::size_t i = 0; i < expected_output.size(); ++i) {
      EXPECT_FLOAT_EQ(expected_output[i], Y_host->data<float>()[i]);
    }
  }

  void RunTransposeTest(
      const std::vector<int>& X_dims,
      const std::vector<int>& axes,
      const std::vector<float>& X_data,
      const std::vector<float>& Y_data) {
    SetUpData(X_dims, axes, X_data);
    math::Transpose<float, HIPContext>(
        X_dims.size(),
        X_dims.data(),
        axes.data(),
        X_->data<float>(),
        Y_->mutable_data<float>(),
        hip_context_.get());
    hip_context_->FinishDeviceComputation();
    VerifyResult(Y_data);
  }

  Workspace ws_;
  DeviceOption option_;
  std::unique_ptr<HIPContext> hip_context_;
  Tensor<HIPContext>* X_ = nullptr;
  Tensor<HIPContext>* Y_ = nullptr;
};

TEST_F(TransposeGPUTest, TransposeGPUFloatTest) {
  if (!HasHipGPU()) {
    return;
  }
  // Test for 1D transpose.
  RunTransposeTest({3}, {0}, {1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f});

  // Test for 2D transpose.
  RunTransposeTest(
      {2, 3},
      {1, 0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
      {1.0f, 4.0f, 2.0f, 5.0f, 3.0f, 6.0f});

  // Test for 3D transpose.
  RunTransposeTest(
      {2, 2, 2},
      {1, 2, 0},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {1.0f, 5.0f, 2.0f, 6.0f, 3.0f, 7.0f, 4.0f, 8.0f});
  RunTransposeTest(
      {2, 2, 2},
      {1, 0, 2},
      {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
      {1.0f, 2.0f, 5.0f, 6.0f, 3.0f, 4.0f, 7.0f, 8.0f});
}

} // namespace

} // namespace caffe2
